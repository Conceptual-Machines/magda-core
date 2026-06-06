#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>

#include "analysis/TrackMeasurer.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief "Levels" loudness/level/stereo meter device (issue #1389).
 *
 * User-insertable analysis device (DeviceType::Analysis, showInBrowser) that
 * meters the signal at its own insertion point - drop it anywhere in a chain or
 * on the master to read what is flowing there. Transparent passthrough wrapping
 * a TrackMeasurer (the same DSP core the agent's hidden tap uses), so it reports
 * BS.1770 LUFS, true-peak, stereo correlation/width and PLR/PSR.
 *
 * Unlike the system TrackMeasurementPlugin this is a single, explicit, user-
 * placed instance, so it runs the heavier true-peak oversampler. It is still
 * cost-gated: LevelsUI flips it active only while the meter is actually showing
 * (setActive); collapsed/hidden it falls back to a single branch per block.
 */
class LevelsPlugin : public te::Plugin {
  public:
    explicit LevelsPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {}
    ~LevelsPlugin() override {
        notifyListenersOfDeletion();
    }

    static const char* getPluginName() {
        return "Levels";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Levels";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    /// Message thread. Called by LevelsUI on show/hide to gate measurement cost.
    void setActive(bool shouldMeasure) noexcept {
        if (shouldMeasure && !active_.exchange(true, std::memory_order_acq_rel))
            pendingReset_.store(true, std::memory_order_release);
        else
            active_.store(shouldMeasure, std::memory_order_release);
    }

    /// Message thread. Latest measurements (lock-free).
    TrackMeasurementSnapshot getSnapshot() const noexcept {
        return measurer_.read();
    }

    void initialise(const te::PluginInitialisationInfo& info) override {
        measurer_.prepare(info.sampleRate, juce::jmax(1, info.blockSizeSamples),
                          /*enableTruePeak=*/true);
    }
    void deinitialise() override {}
    void reset() override {
        measurer_.reset();
    }

    void applyToBuffer(const te::PluginRenderContext& fc) override {
        // Transparent: read only, never modify the buffer or MIDI.
        if (!active_.load(std::memory_order_acquire))
            return;  // not showing: single branch, no measurement cost
        if (!fc.destBuffer || fc.bufferNumSamples <= 0)
            return;
        if (pendingReset_.exchange(false, std::memory_order_acq_rel))
            measurer_.reset();  // fresh integration window each time the meter opens

        const int numCh = juce::jmin(fc.destBuffer->getNumChannels(), 2);
        if (numCh <= 0)
            return;
        const float* ptrs[2] = {nullptr, nullptr};
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[ch] = fc.destBuffer->getReadPointer(ch, fc.bufferStartSample);
        measurer_.process(ptrs, numCh, fc.bufferNumSamples);
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
    void restorePluginStateFromValueTree(const juce::ValueTree&) override {}

  private:
    TrackMeasurer measurer_;
    std::atomic<bool> active_{false};        // measure only while the UI is showing
    std::atomic<bool> pendingReset_{false};  // clear gating history on (re)open

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelsPlugin)
};

}  // namespace magda::daw::audio
