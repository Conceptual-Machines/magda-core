#include "io/CaptureHistory.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

constexpr auto kFramesPerPeak = CaptureRing::kFramesPerPeak;

}  // namespace

CaptureHistory::CaptureHistory(const CaptureRing& ring)
    : ring_(ring), numChannels_(ring.numChannels()), capacity_(ring.capacityFrames()) {}

CaptureHistory::CaptureHistory(const CaptureRing& ring, juce::File file,
                               std::unique_ptr<juce::FileOutputStream> out, std::int64_t diskFrames)
    : ring_(ring),
      file_(std::move(file)),
      out_(std::move(out)),
      numChannels_(ring.numChannels()),
      diskCapacity_(std::max(diskFrames, ring.capacityFrames())),
      capacity_(diskCapacity_),
      peaks_(std::make_unique<std::atomic<float>[]>(
          static_cast<std::size_t>(diskCapacity_ / kFramesPerPeak + 1))),
      peakSlots_(diskCapacity_ / kFramesPerPeak + 1),
      block_(numChannels_, static_cast<int>(kDrainFrames)),
      interleaved_(static_cast<std::size_t>(kDrainFrames * numChannels_)) {
    const auto start = ring.oldestReadable();
    diskFrom_.store(start, std::memory_order_relaxed);
    diskWritten_.store(start, std::memory_order_relaxed);
}

CaptureHistory::~CaptureHistory() {
    out_.reset();
    if (file_ != juce::File())
        file_.deleteFile();
}

bool CaptureHistory::drain() {
    if (!onDisk() || failed())
        return false;

    auto written = diskWritten_.load(std::memory_order_relaxed);
    if (const auto ringOldest = ring_.oldestReadable(); written < ringOldest) {
        // Fell behind the ring: what it overwrote is a gap, and the file starts again after it.
        diskFrom_.store(ringOldest, std::memory_order_release);
        diskWritten_.store(ringOldest, std::memory_order_release);
        written = ringOldest;
        chunkPeak_ = 0.0f;
    }

    const auto frames = static_cast<int>(std::min(ring_.writtenFrames() - written, kDrainFrames));
    if (frames < kMinDrainFrames)
        return false;
    // Lapped while copying: the next round finds itself behind and skips past it.
    if (!ring_.read(written, frames, block_))
        return true;

    for (int i = 0; i < frames; ++i) {
        for (int channel = 0; channel < numChannels_; ++channel)
            chunkPeak_ = std::max(chunkPeak_, std::abs(block_.getSample(channel, i)));
        if (const auto frame = written + i; (frame + 1) % kFramesPerPeak == 0) {
            peaks_[static_cast<std::size_t>((frame / kFramesPerPeak) % peakSlots_)].store(
                chunkPeak_, std::memory_order_relaxed);
            chunkPeak_ = 0.0f;
        }
    }

    if (!writeDisk(written, frames)) {
        failed_.store(true, std::memory_order_relaxed);
        return false;
    }
    diskWritten_.store(written + frames, std::memory_order_release);
    return true;
}

bool CaptureHistory::writeDisk(std::int64_t firstFrame, int frames) {
    for (int i = 0; i < frames; ++i)
        for (int channel = 0; channel < numChannels_; ++channel)
            interleaved_[static_cast<std::size_t>(i * numChannels_ + channel)] =
                block_.getSample(channel, i);

    const auto bytesPerFrame = static_cast<std::int64_t>(numChannels_ * sizeof(float));
    auto slot = firstFrame % diskCapacity_;
    for (int done = 0; done < frames;) {
        const auto count =
            static_cast<int>(std::min<std::int64_t>(frames - done, diskCapacity_ - slot));
        if (!out_->setPosition(slot * bytesPerFrame) ||
            !out_->write(interleaved_.data() + static_cast<std::ptrdiff_t>(done) * numChannels_,
                         static_cast<std::size_t>(count * bytesPerFrame)))
            return false;
        done += count;
        slot = 0;
    }
    out_->flush();
    return out_->getStatus().wasOk();
}

std::int64_t CaptureHistory::oldestOnDisk() const {
    // A drain may be overwriting the slots of up to kDrainFrames past what it has published.
    const auto written = diskWritten_.load(std::memory_order_acquire);
    return std::max({diskFrom_.load(std::memory_order_acquire),
                     written + kDrainFrames - diskCapacity_, std::int64_t{0}});
}

std::int64_t CaptureHistory::oldestReadable() const {
    const auto ringOldest = ring_.oldestReadable();
    if (!onDisk())
        return ringOldest;
    // The file only extends the ring while it reaches it.
    const auto reachesRing = diskWritten_.load(std::memory_order_acquire) >= ringOldest;
    return reachesRing ? std::min(ringOldest, oldestOnDisk()) : ringOldest;
}

bool CaptureHistory::readDisk(std::int64_t firstFrame, std::int64_t frames,
                              juce::AudioBuffer<float>& out, int outOffset) const {
    juce::FileInputStream in(file_);
    if (in.failedToOpen())
        return false;

    std::vector<float> data(static_cast<std::size_t>(frames * numChannels_));
    const auto bytesPerFrame = static_cast<std::int64_t>(numChannels_ * sizeof(float));
    auto slot = firstFrame % diskCapacity_;
    for (std::int64_t done = 0; done < frames;) {
        const auto count = std::min(frames - done, diskCapacity_ - slot);
        const auto bytes = static_cast<int>(count * bytesPerFrame);
        if (!in.setPosition(slot * bytesPerFrame) ||
            in.read(data.data() + done * numChannels_, bytes) != bytes)
            return false;
        done += count;
        slot = 0;
    }

    for (int channel = 0; channel < numChannels_; ++channel) {
        auto* dest = out.getWritePointer(channel, outOffset);
        for (std::int64_t i = 0; i < frames; ++i)
            dest[i] = data[static_cast<std::size_t>(i * numChannels_ + channel)];
    }
    return true;
}

bool CaptureHistory::read(std::int64_t firstFrame, std::int64_t frames,
                          juce::AudioBuffer<float>& out) const {
    if (!onDisk() || firstFrame >= ring_.oldestReadable())
        return ring_.read(firstFrame, frames, out);
    if (frames <= 0 || firstFrame + frames > ring_.writtenFrames())
        return false;

    const auto diskEnd = diskWritten_.load(std::memory_order_acquire);
    if (firstFrame < oldestOnDisk() || firstFrame >= diskEnd)
        return false;

    const auto fromDisk = std::min(frames, diskEnd - firstFrame);
    out.setSize(numChannels_, static_cast<int>(frames), false, false, true);
    if (!readDisk(firstFrame, fromDisk, out, 0))
        return false;
    if (fromDisk < frames) {
        juce::AudioBuffer<float> rest;
        if (!ring_.read(firstFrame + fromDisk, frames - fromDisk, rest))
            return false;
        for (int channel = 0; channel < numChannels_; ++channel)
            out.copyFrom(channel, static_cast<int>(fromDisk), rest, channel, 0,
                         rest.getNumSamples());
    }

    // A drain may have lapped the oldest frames while they were copied.
    return firstFrame >= oldestOnDisk();
}

std::vector<float> CaptureHistory::peaks(std::int64_t first, std::int64_t end, int slices) const {
    if (!onDisk())
        return ring_.peaks(first, end, slices);

    slices = std::max(1, slices);
    std::vector<float> out(static_cast<std::size_t>(slices), 0.0f);
    const auto span = std::max<std::int64_t>(1, end - first);
    const auto ringOldest = ring_.oldestReadable();

    // Closer than a chunk a slice, in what has left the ring: the samples, from the file.
    if (span / slices < kFramesPerPeak && first < ringOldest) {
        const auto from = std::max(first, oldestReadable());
        const auto to = std::min(end, ring_.writtenFrames());
        juce::AudioBuffer<float> audio;
        if (to > from && read(from, to - from, audio)) {
            for (std::int64_t frame = from; frame < to; ++frame) {
                auto& peak = out[static_cast<std::size_t>((frame - first) * slices / span)];
                for (int channel = 0; channel < numChannels_; ++channel)
                    peak = std::max(
                        peak, std::abs(audio.getSample(channel, static_cast<int>(frame - from))));
            }
            return out;
        }
    }

    const auto diskEnd = diskWritten_.load(std::memory_order_acquire);
    const auto diskOldest = oldestOnDisk();
    for (int slice = 0; slice < slices; ++slice) {
        const auto from = first + span * slice / slices;
        const auto to = first + span * (slice + 1) / slices;
        auto peak = 0.0f;
        for (auto chunk = std::max(from, diskOldest) / kFramesPerPeak;
             chunk * kFramesPerPeak < std::min(to, ringOldest) &&
             (chunk + 1) * kFramesPerPeak <= diskEnd;
             ++chunk)
            peak = std::max(peak, peaks_[static_cast<std::size_t>(chunk % peakSlots_)].load(
                                      std::memory_order_relaxed));
        if (const auto inRing = std::max(from, ringOldest); to > inRing)
            peak = std::max(peak, ring_.peaks(inRing, to, 1).front());
        out[static_cast<std::size_t>(slice)] = peak;
    }
    return out;
}

CaptureMeter CaptureHistory::meter(int slices) const {
    if (!onDisk())
        return ring_.meter(slices);

    const auto written = ring_.writtenFrames();
    CaptureMeter out{
        .writtenFrames = written, .capacityFrames = capacity_, .oldestReadable = oldestReadable()};
    out.peaks = peaks(written - capacity_, written, slices);
    applyMarks(out, ring_.marks());
    return out;
}

}  // namespace magda::engine
