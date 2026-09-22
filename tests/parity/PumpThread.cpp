#include "PumpThread.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace magda::parity {

PumpThread::PumpThread(PumpBackend& backend, double sampleRate, int blockSize, double speed)
    : juce::Thread("Parity Pump"),
      backend_(backend),
      sampleRate_(sampleRate),
      blockSize_(blockSize),
      speed_(speed) {
    const auto options =
        juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime(blockSize, sampleRate);
    if (!startRealtimeThread(options))
        startThread(juce::Thread::Priority::highest);
}

PumpThread::~PumpThread() {
    stopThread(2000);
}

void PumpThread::measure(std::int64_t warmupSamples, std::int64_t measuredSamples) {
    const auto blocks = static_cast<std::size_t>(measuredSamples / blockSize_ + 2);
    times_ = std::make_unique<BlockTimes>(blocks);
    span_ = {};
    span_.envelope.reserve(blocks / 100 + 2);
    warmupRemaining_ = warmupSamples;
    measuredRemaining_ = measuredSamples;
    phase_.store(Phase::Warmup, std::memory_order_release);
}

std::optional<SpanResult> PumpThread::result() const {
    if (phase_.load(std::memory_order_acquire) != Phase::Done)
        return std::nullopt;

    auto result = span_;
    result.blocks = times_->summary();
    if (result.blocks.blocks > 0)
        result.processCpuUsPerBlock /= result.blocks.blocks;
    return result;
}

void PumpThread::run() {
    using Clock = std::chrono::steady_clock;

    juce::AudioBuffer<float> buffer(kPumpOutputChannels, blockSize_);
    const auto blockLength = std::chrono::duration<double>(blockSize_ / sampleRate_);
    const auto period = std::chrono::duration_cast<Clock::duration>(blockLength / speed_);

    auto next = Clock::now();
    while (!threadShouldExit()) {
        if (auto* device = backend_.device(); device != nullptr && device->isStarted()) {
            const auto elapsed = device->pull(buffer);

            switch (phase_.load(std::memory_order_acquire)) {
                case Phase::Warmup:
                    warmupRemaining_ -= blockSize_;
                    if (warmupRemaining_ <= 0) {
                        cpuAtStart_ = processCpuSeconds();
                        phase_.store(Phase::Measuring, std::memory_order_release);
                    }
                    break;

                case Phase::Measuring:
                    times_->record(elapsed);
                    if (elapsed > blockLength)
                        ++span_.overruns;
                    {
                        const auto peak = buffer.getMagnitude(0, blockSize_);
                        span_.outputPeak = std::max(span_.outputPeak, peak);
                        const auto bucket = static_cast<std::size_t>(span_.blocks.blocks++ / 100);
                        if (bucket >= span_.envelope.size() &&
                            span_.envelope.size() < span_.envelope.capacity())
                            span_.envelope.push_back(0.0f);
                        if (bucket < span_.envelope.size())
                            span_.envelope[bucket] = std::max(span_.envelope[bucket], peak);
                    }

                    measuredRemaining_ -= blockSize_;
                    if (measuredRemaining_ <= 0) {
                        // Summed here, divided by the block count on the message thread.
                        span_.processCpuUsPerBlock = (processCpuSeconds() - cpuAtStart_) * 1.0e6;
                        phase_.store(Phase::Done, std::memory_order_release);
                    }
                    break;

                case Phase::Idle:
                case Phase::Done:
                    break;
            }
        }

        next = std::max(next + period, Clock::now());
        std::this_thread::sleep_until(next);
    }
}

}  // namespace magda::parity
