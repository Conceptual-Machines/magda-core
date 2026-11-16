// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ranged_download.hpp"

#include <algorithm>
#include <numeric>
#include <thread>

namespace magda::download {

namespace {

constexpr int kBufferSize = 256 * 1024;
constexpr int kConnectTimeoutMs = 30000;
constexpr int kTickMs = 250;

class HttpStream final : public Stream {
  public:
    HttpStream(const juce::String& url, juce::int64 from, juce::int64 to)
        : stream_(juce::URL(url), false), from_(from) {
        stream_.withExtraHeaders("Range: bytes=" + juce::String(from) + "-" + juce::String(to - 1))
            .withConnectionTimeout(kConnectTimeoutMs)
            .withNumRedirectsToFollow(5);
    }

    bool connect() override {
        if (!stream_.connect(nullptr))
            return false;

        // A server that ignores Range sends the whole file with a 200.
        const int status = stream_.getStatusCode();
        return status == 206 || (status == 200 && from_ == 0);
    }

    int read(void* dest, int maxBytes) override {
        return std::max(0, stream_.read(dest, maxBytes));
    }

    void cancel() override {
        stream_.cancel();
    }

    juce::WebInputStream& web() {
        return stream_;
    }

  private:
    juce::WebInputStream stream_;
    juce::int64 from_;
};

class HttpSource final : public Source {
  public:
    explicit HttpSource(juce::String url) : url_(std::move(url)) {}

    juce::String url() const override {
        return url_;
    }

    std::optional<Info> probe() override {
        auto stream = std::make_unique<HttpStream>(url_, 0, 1);
        {
            std::lock_guard lock(lock_);
            if (cancelled_)
                return std::nullopt;
            probing_ = stream.get();
        }

        const bool connected = stream->web().connect(nullptr);
        std::optional<Info> info;

        if (connected && stream->web().getStatusCode() == 206) {
            // Content-Range: bytes 0-0/<total>
            const auto range = stream->web().getResponseHeaders()["Content-Range"];
            const auto total = range.fromLastOccurrenceOf("/", false, false).trim();
            if (total.getLargeIntValue() > 0)
                info = Info{total.getLargeIntValue(), true};
        } else if (connected && stream->web().getStatusCode() == 200 &&
                   stream->web().getTotalLength() > 0) {
            info = Info{stream->web().getTotalLength(), false};
        }

        std::lock_guard lock(lock_);
        probing_ = nullptr;
        return cancelled_ ? std::nullopt : info;
    }

    void cancel() override {
        std::lock_guard lock(lock_);
        cancelled_ = true;
        if (probing_ != nullptr)
            probing_->cancel();
    }

    std::unique_ptr<Stream> open(juce::int64 from, juce::int64 to) override {
        return std::make_unique<HttpStream>(url_, from, to);
    }

  private:
    juce::String url_;
    std::mutex lock_;
    HttpStream* probing_ = nullptr;
    bool cancelled_ = false;
};

}  // namespace

std::vector<Chunk> planChunks(juce::int64 size, int count) {
    if (size <= 0)
        return {};

    const auto n = std::clamp<juce::int64>(count, 1, size);
    const auto step = size / n;
    std::vector<Chunk> chunks;
    for (juce::int64 i = 0; i < n; ++i)
        chunks.push_back({i * step, i == n - 1 ? size : (i + 1) * step, 0});
    return chunks;
}

juce::int64 ResumeState::bytesDone() const noexcept {
    return std::accumulate(chunks.begin(), chunks.end(), juce::int64{0},
                           [](juce::int64 sum, const Chunk& chunk) { return sum + chunk.done; });
}

juce::String toJson(const ResumeState& state) {
    juce::Array<juce::var> chunks;
    for (const auto& chunk : state.chunks)
        chunks.add(juce::Array<juce::var>{chunk.start, chunk.end, chunk.done});

    auto* object = new juce::DynamicObject();
    object->setProperty("url", state.url);
    object->setProperty("size", state.size);
    object->setProperty("chunks", chunks);
    return juce::JSON::toString(juce::var(object));
}

std::optional<ResumeState> resumeStateFromJson(const juce::String& json) {
    const auto parsed = juce::JSON::parse(json);
    const auto* chunks = parsed["chunks"].getArray();
    if (chunks == nullptr)
        return std::nullopt;

    ResumeState state{parsed["url"].toString(), static_cast<juce::int64>(parsed["size"]), {}};
    juce::int64 next = 0;
    for (const auto& entry : *chunks) {
        const auto* fields = entry.getArray();
        if (fields == nullptr || fields->size() != 3)
            return std::nullopt;

        const Chunk chunk{static_cast<juce::int64>((*fields)[0]),
                          static_cast<juce::int64>((*fields)[1]),
                          static_cast<juce::int64>((*fields)[2])};
        if (chunk.start != next || chunk.end <= chunk.start || chunk.done < 0 ||
            chunk.remaining() < 0)
            return std::nullopt;

        state.chunks.push_back(chunk);
        next = chunk.end;
    }

    if (state.chunks.empty() || next != state.size)
        return std::nullopt;
    return state;
}

std::unique_ptr<Source> httpSource(const juce::String& url) {
    return std::make_unique<HttpSource>(url);
}

// ============================================================================

RangedDownload::RangedDownload(Source& source, juce::File partFile, juce::File stateFile,
                               int connections, DownloadTiming timing)
    : source_(source),
      partFile_(std::move(partFile)),
      stateFile_(std::move(stateFile)),
      connections_(connections),
      timing_(timing) {}

RangedDownload::~RangedDownload() = default;

RangedDownload::Result RangedDownload::run(const std::function<void()>& onTick) {
    const auto info = source_.probe();
    if (stopping_)
        return {Outcome::Stopped};
    if (!info || info->size <= 0)
        return {Outcome::Failed, Failure::Unreachable};

    chunks_ = chunksToFetch(*info);
    bytesTotal_ = info->size;
    bytesDone_ = ResumeState{{}, info->size, chunks_}.bytesDone();

    partFile_.getParentDirectory().createDirectory();
    out_ = std::make_unique<juce::FileOutputStream>(partFile_);
    if (!out_->openedOk()) {
        out_.reset();
        return {Outcome::Failed, Failure::Disk};
    }
    saveState();

    std::vector<std::thread> workers;
    for (size_t i = 0; i < chunks_.size(); ++i) {
        if (chunks_[i].remaining() == 0)
            continue;
        ++running_;
        workers.emplace_back([this, i] { fetch(i); });
    }

    auto lastSave = juce::Time::getMillisecondCounterHiRes();
    while (running_.load() > 0) {
        workerExited_.wait(kTickMs);
        if (onTick)
            onTick();

        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastSave >= timing_.saveIntervalSeconds * 1000.0) {
            saveState();
            lastSave = now;
        }
    }

    for (auto& worker : workers)
        worker.join();
    saveState();
    out_.reset();

    if (const auto failure = failure_.load(); failure != Failure::None)
        return {Outcome::Failed, failure};

    const bool complete =
        std::ranges::all_of(chunks_, [](const Chunk& chunk) { return chunk.remaining() == 0; });
    return {complete ? Outcome::Complete : Outcome::Stopped};
}

void RangedDownload::stop() {
    stopping_ = true;
    stopSignal_.signal();
    source_.cancel();

    std::lock_guard lock(streamsLock_);
    for (auto* stream : openStreams_)
        stream->cancel();
}

std::vector<Chunk> RangedDownload::chunksToFetch(const Source::Info& info) {
    if (info.acceptsRanges && partFile_.existsAsFile()) {
        if (const auto saved = resumeStateFromJson(stateFile_.loadFileAsString());
            saved && saved->url == source_.url() && saved->size == info.size)
            return saved->chunks;
    }

    partFile_.deleteFile();
    return planChunks(info.size, info.acceptsRanges ? connections_ : 1);
}

void RangedDownload::fetch(size_t index) {
    juce::HeapBlock<char> buffer(kBufferSize);
    int attempts = 0;

    while (!stopping_) {
        juce::int64 from = 0;
        juce::int64 to = 0;
        {
            std::lock_guard lock(writeLock_);
            const auto& chunk = chunks_[index];
            if (chunk.remaining() == 0)
                break;
            from = chunk.start + chunk.done;
            to = chunk.end;
        }

        auto stream = source_.open(from, to);
        bool progressed = false;
        if (watch(stream.get())) {
            if (stream->connect())
                progressed = pump(index, *stream, buffer);
            unwatch(stream.get());
        }

        attempts = progressed ? 0 : attempts + 1;
        if (attempts >= timing_.maxAttempts) {
            fail(Failure::Network);
            break;
        }

        // A connection that dropped mid-range reconnects at once; one that brought nothing waits.
        if (!progressed)
            stopSignal_.wait(timing_.retryDelayMs);
    }

    --running_;
    workerExited_.signal();
}

bool RangedDownload::pump(size_t index, Stream& stream, juce::HeapBlock<char>& buffer) {
    bool progressed = false;

    while (!stopping_) {
        juce::int64 remaining = 0;
        {
            std::lock_guard lock(writeLock_);
            remaining = chunks_[index].remaining();
        }
        if (remaining == 0)
            break;

        const int read =
            stream.read(buffer, static_cast<int>(std::min<juce::int64>(kBufferSize, remaining)));
        if (read <= 0)
            break;

        std::lock_guard lock(writeLock_);
        auto& chunk = chunks_[index];
        const auto bytes = std::min<juce::int64>(read, chunk.remaining());
        if (!out_->setPosition(chunk.start + chunk.done) ||
            !out_->write(buffer, static_cast<size_t>(bytes))) {
            fail(Failure::Disk);
            break;
        }

        chunk.done += bytes;
        bytesDone_ += bytes;
        progressed = true;
    }

    return progressed;
}

void RangedDownload::fail(Failure failure) {
    auto none = Failure::None;
    failure_.compare_exchange_strong(none, failure);
    stop();
}

bool RangedDownload::watch(Stream* stream) {
    std::lock_guard lock(streamsLock_);
    if (stopping_)
        return false;
    openStreams_.push_back(stream);
    return true;
}

void RangedDownload::unwatch(Stream* stream) {
    std::lock_guard lock(streamsLock_);
    std::erase(openStreams_, stream);
}

void RangedDownload::saveState() {
    ResumeState state{source_.url(), bytesTotal_.load(), {}};
    {
        std::lock_guard lock(writeLock_);
        if (out_ == nullptr)
            return;

        // The state names only bytes already on disk.
        out_->flush();
        if (out_->getStatus().failed()) {
            fail(Failure::Disk);
            return;
        }
        state.chunks = chunks_;
    }

    juce::TemporaryFile temp(stateFile_);
    if (temp.getFile().replaceWithText(toJson(state)))
        temp.overwriteTargetFileWithTemporary();
}

}  // namespace magda::download
