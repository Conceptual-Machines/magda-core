// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ranged_download.hpp
 * @brief A file fetched over several byte ranges at once, resumable across runs (#1830).
 */

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace magda::download {

/** @brief Bytes [start, end) of the file, @p done of them already on disk. */
struct Chunk {
    juce::int64 start = 0;
    juce::int64 end = 0;
    juce::int64 done = 0;

    [[nodiscard]] juce::int64 remaining() const noexcept {
        return end - start - done;
    }

    bool operator==(const Chunk&) const = default;
};

/** @brief Split @p size bytes into at most @p count ranges that cover it in order. */
std::vector<Chunk> planChunks(juce::int64 size, int count);

/** @brief What a stopped download leaves beside its partial file. */
struct ResumeState {
    juce::String url;
    juce::int64 size = 0;
    std::vector<Chunk> chunks;

    [[nodiscard]] juce::int64 bytesDone() const noexcept;
};

juce::String toJson(const ResumeState& state);

/** @brief The state in @p json, or nothing when it is unreadable or its ranges do not tile. */
std::optional<ResumeState> resumeStateFromJson(const juce::String& json);

/** @brief One request for a range of the file. */
class Stream {
  public:
    virtual ~Stream() = default;

    /// False when the server cannot be reached or will not serve the range.
    virtual bool connect() = 0;

    /// Bytes read into @p dest; 0 at the end of the body or on a dropped connection.
    virtual int read(void* dest, int maxBytes) = 0;

    /// Unblocks a pending connect or read. Any thread.
    virtual void cancel() = 0;
};

/** @brief Where the bytes come from: the server, or a fake in tests. */
class Source {
  public:
    struct Info {
        juce::int64 size = 0;
        bool acceptsRanges = false;
    };

    virtual ~Source() = default;

    /// What a partial file is resumed under.
    [[nodiscard]] virtual juce::String url() const = 0;

    virtual std::optional<Info> probe() = 0;

    /// Abandons a probe in progress, and any after it. Any thread.
    virtual void cancel() = 0;

    /// A request for bytes [from, to), not yet connected.
    virtual std::unique_ptr<Stream> open(juce::int64 from, juce::int64 to) = 0;
};

/** @brief @p url over HTTP, ranges through a `Range:` request and a 206. */
std::unique_ptr<Source> httpSource(const juce::String& url);

/** @brief How often a download saves its state, and how it retries a range. */
struct DownloadTiming {
    double saveIntervalSeconds = 2.0;
    int retryDelayMs = 2000;
    /// Attempts in a row at a range that bring no bytes before the download fails.
    int maxAttempts = 5;
};

/** @brief Fetches a Source into a partial file, keeping a state file current so it resumes. */
class RangedDownload {
  public:
    enum class Outcome { Complete, Stopped, Failed };
    enum class Failure { None, Unreachable, Network, Disk };

    struct Result {
        Outcome outcome = Outcome::Failed;
        Failure failure = Failure::None;
    };

    RangedDownload(Source& source, juce::File partFile, juce::File stateFile, int connections,
                   DownloadTiming timing = {});
    ~RangedDownload();

    RangedDownload(const RangedDownload&) = delete;
    RangedDownload& operator=(const RangedDownload&) = delete;

    /**
     * @brief Blocks until every range is on disk, stop() is called, or a range gives up.
     *
     * Picks up the ranges in the state file when it matches the source; otherwise starts over.
     * @p onTick runs on this thread about four times a second.
     */
    Result run(const std::function<void()>& onTick = {});

    /// Any thread. run() returns once its connections close.
    void stop();

    [[nodiscard]] juce::int64 bytesDone() const noexcept {
        return bytesDone_.load();
    }
    [[nodiscard]] juce::int64 bytesTotal() const noexcept {
        return bytesTotal_.load();
    }

  private:
    std::vector<Chunk> chunksToFetch(const Source::Info& info);
    void fetch(size_t index);
    bool pump(size_t index, Stream& stream, juce::HeapBlock<char>& buffer);
    void fail(Failure failure);
    bool watch(Stream* stream);
    void unwatch(Stream* stream);
    void saveState();

    Source& source_;
    juce::File partFile_;
    juce::File stateFile_;
    int connections_;
    DownloadTiming timing_;

    std::mutex writeLock_;
    std::unique_ptr<juce::FileOutputStream> out_;
    std::vector<Chunk> chunks_;

    std::mutex streamsLock_;
    std::vector<Stream*> openStreams_;

    std::atomic<bool> stopping_{false};
    std::atomic<Failure> failure_{Failure::None};
    std::atomic<int> running_{0};
    juce::WaitableEvent workerExited_;
    juce::WaitableEvent stopSignal_{true};
    std::atomic<juce::int64> bytesDone_{0};
    std::atomic<juce::int64> bytesTotal_{0};
};

}  // namespace magda::download
