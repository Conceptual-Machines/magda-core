#pragma once

#include <atomic>

#include "analysis/TrackMeasurer.hpp"
#include "plugins/AnalysisTelemetry.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

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
class LevelsPlugin : public MagdaDevice, public LevelsTelemetry {
  public:
    LevelsPlugin() = default;

    static const char* getPluginName() {
        return "Levels";
    }
    static const char* xmlTypeName;

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Levels",
        };
    }

    /// Message thread. Called by LevelsUI on show/hide to gate measurement cost.
    void setActive(bool shouldMeasure) override {
        if (shouldMeasure && !active_.exchange(true, std::memory_order_acq_rel))
            pendingReset_.store(true, std::memory_order_release);
        else
            active_.store(shouldMeasure, std::memory_order_release);
    }

    /// Message thread. Applied at the top of the next processed block.
    void requestReset() override {
        pendingReset_.store(true, std::memory_order_release);
    }

    /// Message thread. Latest measurements (lock-free).
    TrackMeasurementSnapshot snapshot() const override {
        return measurer_.read();
    }

    std::string_view telemetryKey() const override {
        return kKey;
    }

    DeviceTelemetry* telemetry(std::string_view key) override {
        return key == kKey ? this : nullptr;
    }

    const DeviceTelemetry* telemetry(std::string_view key) const override {
        return key == kKey ? this : nullptr;
    }

    void prepare(const DevicePrepareContext& context) override {
        measurer_.prepare(context.sampleRate, juce::jmax(1, context.maximumBlockSize),
                          /*enableTruePeak=*/true);
    }

    void reset() override {
        measurer_.reset();
    }

    void process(DeviceProcessContext& context) override {
        // Transparent: read only, never modify the buffer or MIDI.
        //
        // Integrated LUFS, true peak and the derived PLR are hold figures by
        // definition - they accumulate and never fall back on their own. Restart
        // them whenever the transport rolls, the way hardware loudness meters do,
        // so each pass reads the material just played rather than the whole
        // session (issue #1967). Tracked ahead of the active_ gate so a meter
        // shown mid-session still restarts on the next roll.
        const bool playStarted = context.isPlaying && !wasPlaying_;
        wasPlaying_ = context.isPlaying;
        if (playStarted)
            pendingReset_.store(true, std::memory_order_release);

        if (!active_.load(std::memory_order_acquire))
            return;  // not showing: no measurement cost beyond the transport edge
        if (context.audio == nullptr || context.numSamples <= 0)
            return;
        // Fresh integration window each time the meter opens, the transport rolls
        // or the user hits Reset.
        if (pendingReset_.exchange(false, std::memory_order_acq_rel))
            measurer_.reset();

        const int numCh = juce::jmin(context.audio->getNumChannels(), 2);
        if (numCh <= 0)
            return;
        const float* ptrs[2] = {nullptr, nullptr};
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[ch] = context.audio->getReadPointer(ch, context.startSample);
        measurer_.process(ptrs, numCh, context.numSamples);
    }

  private:
    TrackMeasurer measurer_;
    std::atomic<bool> active_{false};        // measure only while the UI is showing
    std::atomic<bool> pendingReset_{false};  // clear gating history on (re)open / roll / Reset
    bool wasPlaying_ = false;                // audio thread only: transport edge detection

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelsPlugin)
};

}  // namespace magda::daw::audio
