// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ranged_download.cpp
 * @brief Parallel ranges, reconnects and resume for the Local LLM download (#1830).
 */

#include <juce_core/juce_core.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <mutex>

#include "magda/agents/ranged_download.hpp"

using magda::download::Chunk;
using magda::download::DownloadTiming;
using magda::download::RangedDownload;
using magda::download::ResumeState;

namespace {

constexpr DownloadTiming kFast{0.05, 1, 3};

juce::MemoryBlock randomBytes(size_t size) {
    juce::MemoryBlock data(size);
    juce::Random(1830).fillBitsRandomly(data.getData(), data.getSize());
    return data;
}

class FakeSource;

class FakeStream final : public magda::download::Stream {
  public:
    FakeStream(const FakeSource& source, juce::int64 from, juce::int64 to);

    bool connect() override;
    int read(void* dest, int maxBytes) override;

    void cancel() override {
        cancelled_ = true;
        cancelSignal_.signal();
    }

  private:
    const FakeSource& source_;
    juce::int64 position_;
    juce::int64 to_;
    juce::int64 sent_ = 0;
    std::atomic<bool> cancelled_{false};
    juce::WaitableEvent cancelSignal_;
};

class FakeSource final : public magda::download::Source {
  public:
    explicit FakeSource(juce::MemoryBlock data) : data(std::move(data)) {}

    juce::String url() const override {
        return name;
    }

    std::optional<Info> probe() override {
        if (!reachable)
            return std::nullopt;
        return Info{static_cast<juce::int64>(data.getSize()), acceptsRanges};
    }

    void cancel() override {}

    std::unique_ptr<magda::download::Stream> open(juce::int64 from, juce::int64 to) override {
        std::lock_guard lock(opensLock);
        opens.push_back(from);
        return std::make_unique<FakeStream>(*this, from, to);
    }

    std::vector<juce::int64> openedAt() {
        std::lock_guard lock(opensLock);
        auto sorted = opens;
        std::ranges::sort(sorted);
        return sorted;
    }

    juce::MemoryBlock data;
    juce::String name = "https://example.test/model.gguf";
    bool reachable = true;
    bool acceptsRanges = true;
    bool refuses = false;
    /// A connection ends after this many bytes; -1 for never.
    juce::int64 bytesPerConnection = -1;
    /// At that limit, wait to be cancelled instead of ending.
    bool hangAtLimit = false;

  private:
    std::mutex opensLock;
    std::vector<juce::int64> opens;
};

FakeStream::FakeStream(const FakeSource& source, juce::int64 from, juce::int64 to)
    : source_(source), position_(from), to_(to) {}

bool FakeStream::connect() {
    return !source_.refuses && (source_.acceptsRanges || position_ == 0);
}

int FakeStream::read(void* dest, int maxBytes) {
    auto allowed = std::min<juce::int64>(maxBytes, to_ - position_);
    if (source_.bytesPerConnection >= 0)
        allowed = std::min(allowed, source_.bytesPerConnection - sent_);

    if (cancelled_ || allowed <= 0) {
        if (!cancelled_ && source_.hangAtLimit && position_ < to_)
            cancelSignal_.wait(-1);
        return 0;
    }

    std::memcpy(dest, static_cast<const char*>(source_.data.getData()) + position_,
                static_cast<size_t>(allowed));
    position_ += allowed;
    sent_ += allowed;
    return static_cast<int>(allowed);
}

/** @brief A partial file and its state file in a fresh temporary folder. */
struct Files {
    Files()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("magda-ranged-download", "")) {
        folder.createDirectory();
    }

    ~Files() {
        folder.deleteRecursively();
    }

    juce::MemoryBlock partBytes() const {
        juce::MemoryBlock bytes;
        part.loadFileAsData(bytes);
        return bytes;
    }

    std::optional<ResumeState> state() const {
        return magda::download::resumeStateFromJson(stateFile.loadFileAsString());
    }

    juce::File folder;
    juce::File part = folder.getChildFile("model.gguf.part");
    juce::File stateFile = folder.getChildFile("model.gguf.part.json");
};

}  // namespace

TEST_CASE("A file splits into ranges that cover it in order", "[agents][download][1830]") {
    CHECK(magda::download::planChunks(10, 3) ==
          std::vector<Chunk>{{0, 3, 0}, {3, 6, 0}, {6, 10, 0}});
    CHECK(magda::download::planChunks(2, 6).size() == 2);
    CHECK(magda::download::planChunks(0, 4).empty());
}

TEST_CASE("The resume state reads back, and a state whose ranges do not tile is refused",
          "[agents][download][1830]") {
    const ResumeState state{"https://example.test/a", 10, {{0, 4, 4}, {4, 10, 2}}};
    const auto read = magda::download::resumeStateFromJson(magda::download::toJson(state));
    REQUIRE(read.has_value());
    CHECK(read->url == state.url);
    CHECK(read->size == 10);
    CHECK(read->chunks == state.chunks);
    CHECK(read->bytesDone() == 6);

    const ResumeState gap{"u", 10, {{0, 4, 0}, {5, 10, 0}}};
    CHECK_FALSE(magda::download::resumeStateFromJson(magda::download::toJson(gap)));
    const ResumeState overrun{"u", 10, {{0, 4, 5}, {4, 10, 0}}};
    CHECK_FALSE(magda::download::resumeStateFromJson(magda::download::toJson(overrun)));
    CHECK_FALSE(magda::download::resumeStateFromJson("not json"));
}

TEST_CASE("A download over several connections writes the whole file", "[agents][download][1830]") {
    Files files;
    FakeSource source(randomBytes(1 << 20));
    RangedDownload download(source, files.part, files.stateFile, 4, kFast);

    const auto result = download.run();

    CHECK(result.outcome == RangedDownload::Outcome::Complete);
    CHECK(files.partBytes() == source.data);
    CHECK(source.openedAt() == std::vector<juce::int64>{0, 1 << 18, 2 << 18, 3 << 18});
    REQUIRE(files.state().has_value());
    CHECK(files.state()->bytesDone() == 1 << 20);
}

TEST_CASE("A dropped connection picks the range up where it stopped", "[agents][download][1830]") {
    Files files;
    FakeSource source(randomBytes(200000));
    source.bytesPerConnection = 30000;
    RangedDownload download(source, files.part, files.stateFile, 2, kFast);

    CHECK(download.run().outcome == RangedDownload::Outcome::Complete);
    CHECK(files.partBytes() == source.data);
    CHECK(source.openedAt() ==
          std::vector<juce::int64>{0, 30000, 60000, 90000, 100000, 130000, 160000, 190000});
}

TEST_CASE("A stopped download resumes from its state file", "[agents][download][1830]") {
    Files files;
    const auto data = randomBytes(1 << 20);
    constexpr juce::int64 kSent = 50000;

    {
        FakeSource first(data);
        first.bytesPerConnection = kSent;
        first.hangAtLimit = true;
        RangedDownload download(first, files.part, files.stateFile, 4, kFast);

        const auto result = download.run([&download] {
            if (download.bytesDone() >= 4 * kSent)
                download.stop();
        });

        CHECK(result.outcome == RangedDownload::Outcome::Stopped);
        REQUIRE(files.state().has_value());
        CHECK(files.state()->bytesDone() == 4 * kSent);
    }

    FakeSource second(data);
    RangedDownload download(second, files.part, files.stateFile, 4, kFast);

    CHECK(download.run().outcome == RangedDownload::Outcome::Complete);
    CHECK(files.partBytes() == data);
    CHECK(second.openedAt() ==
          std::vector<juce::int64>{kSent, (1 << 18) + kSent, (2 << 18) + kSent, (3 << 18) + kSent});
}

TEST_CASE("A partial file left by another download starts over", "[agents][download][1830]") {
    Files files;
    FakeSource source(randomBytes(100000));
    files.part.replaceWithText("left over");
    files.stateFile.replaceWithText(
        magda::download::toJson({"https://example.test/other.gguf", 100000, {{0, 100000, 9}}}));

    RangedDownload download(source, files.part, files.stateFile, 2, kFast);

    CHECK(download.run().outcome == RangedDownload::Outcome::Complete);
    CHECK(files.partBytes() == source.data);
    CHECK(source.openedAt() == std::vector<juce::int64>{0, 50000});
}

TEST_CASE("A server without ranges is fetched over one connection", "[agents][download][1830]") {
    Files files;
    FakeSource source(randomBytes(100000));
    source.acceptsRanges = false;
    RangedDownload download(source, files.part, files.stateFile, 4, kFast);

    CHECK(download.run().outcome == RangedDownload::Outcome::Complete);
    CHECK(files.partBytes() == source.data);
    CHECK(source.openedAt() == std::vector<juce::int64>{0});
}

TEST_CASE("A download fails when the server cannot be reached or stops serving",
          "[agents][download][1830]") {
    Files files;
    FakeSource source(randomBytes(1000));

    SECTION("unreachable") {
        source.reachable = false;
        RangedDownload download(source, files.part, files.stateFile, 2, kFast);
        const auto result = download.run();
        CHECK(result.outcome == RangedDownload::Outcome::Failed);
        CHECK(result.failure == RangedDownload::Failure::Unreachable);
    }

    SECTION("every range refused, after the retries") {
        source.refuses = true;
        RangedDownload download(source, files.part, files.stateFile, 2, kFast);
        const auto result = download.run();
        CHECK(result.outcome == RangedDownload::Outcome::Failed);
        CHECK(result.failure == RangedDownload::Failure::Network);
        CHECK(source.openedAt().size() >= static_cast<size_t>(kFast.maxAttempts));
        CHECK(files.state().has_value());
    }
}
