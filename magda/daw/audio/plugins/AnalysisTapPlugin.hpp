#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

#include "analysis/AudioTapBuffer.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

/**
 * @brief Base for passthrough analysis devices (Oscilloscope, Spectrum Analyzer).
 *
 * Transparent: applyToBuffer() copies a mono downmix of the incoming audio into
 * a lock-free AudioTapBuffer for the UI to read, and does NOT modify the audio.
 * Subclasses exist only to give each device a distinct identity (name /
 * xmlTypeName) so they register and dispatch to their own UI; the tap logic is
 * shared here.
 *
 * These are DeviceType::Analysis devices - they expose no macros or mods (see
 * the macro/mod gating keyed off DeviceType::Analysis).
 */
class AnalysisTapPlugin : public MagdaDevice {
  public:
    explicit AnalysisTapPlugin(int ringCapacity = 65536, int defaultTraceColour = 0)
        : tap_(ringCapacity), traceColour_(defaultTraceColour) {}

    /** Message thread. Lock-free read access for the UI analyzer. */
    const AudioTapBuffer& getTapBuffer() const {
        return tap_;
    }

    /** Sample rate the tap is fed at; used by the spectrum UI for its freq axis. */
    double getSampleRate() const {
        return sampleRate_.load(std::memory_order_relaxed);
    }

    /** Display trace colour index (into the analyzer palette); persisted setting. */
    int getTraceColourIndex() const {
        return traceColour_.load(std::memory_order_relaxed);
    }
    void setTraceColourIndex(int index) {
        traceColour_.store(index, std::memory_order_relaxed);
    }

    void prepare(const DevicePrepareContext& context) override {
        monoScratch_.assign(static_cast<size_t>(juce::jmax(0, context.maximumBlockSize)), 0.0f);
        sampleRate_.store(context.sampleRate, std::memory_order_relaxed);
    }
    void release() override {}
    void reset() override {}

    void process(DeviceProcessContext& context) override {
        // Transparent passthrough: read the buffer, never write it.
        if (context.audio == nullptr || context.numSamples <= 0)
            return;
        // Don't feed the visualisation during an offline render (export / mix
        // analysis): audio still passes through untouched, but tapping it would
        // make the live scope/spectrum displays twitch to the render's audio.
        if (context.isRendering)
            return;
        const int n = context.numSamples;
        if (static_cast<int>(monoScratch_.size()) < n)
            return;  // block exceeds initialised size; skip tap (audio still passes through)
        const int numCh = context.audio->getNumChannels();
        if (numCh <= 0)
            return;

        std::fill_n(monoScratch_.data(), n, 0.0f);
        for (int ch = 0; ch < numCh; ++ch) {
            const float* src = context.audio->getReadPointer(ch, context.startSample);
            for (int i = 0; i < n; ++i)
                monoScratch_[static_cast<size_t>(i)] += src[i];
        }
        const float inv = 1.0f / static_cast<float>(numCh);
        for (int i = 0; i < n; ++i)
            monoScratch_[static_cast<size_t>(i)] *= inv;

        tap_.write(monoScratch_.data(), n);
    }

    void flushState(juce::ValueTree& state) override {
        state.setProperty("traceColour", getTraceColourIndex(), nullptr);
    }

    void restoreState(const juce::ValueTree& state) override {
        if (state.hasProperty("traceColour"))
            setTraceColourIndex(state["traceColour"]);
    }

  protected:
    // Sized by each subclass via the ctor: the oscilloscope needs seconds of
    // history for long timebases; the spectrum only needs <= one FFT frame.
    AudioTapBuffer tap_;
    std::vector<float> monoScratch_;  // audio-thread downmix scratch, sized in prepare()
    std::atomic<double> sampleRate_{44100.0};
    std::atomic<int> traceColour_{0};  // index into the analyzer colour palette
};

}  // namespace magda::daw::audio
