#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "analysis/AudioTapBuffer.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

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
class AnalysisTapPlugin : public te::Plugin {
  public:
    explicit AnalysisTapPlugin(const te::PluginCreationInfo& info, int ringCapacity = 65536,
                               int defaultTraceColour = 0)
        : te::Plugin(info), tap_(ringCapacity) {
        // defaultTraceColour comes from the per-device-kind Config default, so a
        // fresh device adopts the user's last-used colour; a restored device's
        // saved property overrides this in copyPropertiesToCachedValues().
        traceColourValue_.referTo(state, juce::Identifier("traceColour"), getUndoManager(),
                                  defaultTraceColour);
    }
    ~AnalysisTapPlugin() override {
        notifyListenersOfDeletion();
    }

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
        return traceColourValue_.get();
    }
    void setTraceColourIndex(int index) {
        traceColourValue_ = index;
    }

    void initialise(const te::PluginInitialisationInfo& info) override {
        monoScratch_.assign(static_cast<size_t>(juce::jmax(0, info.blockSizeSamples)), 0.0f);
        sampleRate_.store(info.sampleRate, std::memory_order_relaxed);
    }
    void deinitialise() override {}
    void reset() override {}

    void applyToBuffer(const te::PluginRenderContext& fc) override {
        // Transparent passthrough: read the buffer, never write it.
        if (!fc.destBuffer || fc.bufferNumSamples <= 0)
            return;
        const int n = fc.bufferNumSamples;
        if (static_cast<int>(monoScratch_.size()) < n)
            return;  // block exceeds initialised size; skip tap (audio still passes through)
        const int numCh = fc.destBuffer->getNumChannels();
        if (numCh <= 0)
            return;

        std::fill_n(monoScratch_.data(), n, 0.0f);
        for (int ch = 0; ch < numCh; ++ch) {
            const float* src = fc.destBuffer->getReadPointer(ch, fc.bufferStartSample);
            for (int i = 0; i < n; ++i)
                monoScratch_[static_cast<size_t>(i)] += src[i];
        }
        const float inv = 1.0f / static_cast<float>(numCh);
        for (int i = 0; i < n; ++i)
            monoScratch_[static_cast<size_t>(i)] *= inv;

        tap_.write(monoScratch_.data(), n);
    }

    bool takesMidiInput() override {
        return false;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }
    // Restore persisted settings (e.g. on preset load, or when a fresh plugin is
    // created then restored). Subclasses override to also restore their own
    // CachedValues, calling this base first.
    void restorePluginStateFromValueTree(const juce::ValueTree& v) override {
        tracktion::copyPropertiesToCachedValues(v, traceColourValue_);
    }

  protected:
    // Sized by each subclass via the ctor: the oscilloscope needs seconds of
    // history for long timebases; the spectrum only needs <= one FFT frame.
    AudioTapBuffer tap_;
    std::vector<float> monoScratch_;  // audio-thread downmix scratch, sized in initialise()
    std::atomic<double> sampleRate_{44100.0};
    juce::CachedValue<int> traceColourValue_;  // index into the analyzer colour palette
};

}  // namespace magda::daw::audio
