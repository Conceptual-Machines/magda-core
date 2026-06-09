#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>

#include "analysis/BandSpectrum.hpp"
#include "analysis/TrackMeasurer.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief Always-on, insert-independent per-track measurement tap (issue #1388).
 *
 * Auto-inserted post-fader on every track and the master by the measurement
 * manager (not user-insertable; showInBrowser=false). Transparent passthrough:
 * applyToBuffer() never modifies the audio. It feeds the stereo block to a
 * TrackMeasurer only while a consumer (a visible Levels meter or the mixing
 * agent mid-pass) has enabled it - otherwise it returns immediately so the
 * idle cost on un-metered tracks is a single branch per block.
 *
 * True-peak oversampling is the one heavy measurement, so it is opt-in per
 * instance (persisted): the manager enables it on the master only, where
 * inter-sample peaks matter, and leaves per-track on sample peak.
 */
class TrackMeasurementPlugin : public te::Plugin {
  public:
    explicit TrackMeasurementPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {
        measureTruePeakValue_.referTo(state, juce::Identifier("measureTruePeak"), getUndoManager(),
                                      false);
    }
    ~TrackMeasurementPlugin() override {
        notifyListenersOfDeletion();
    }

    static const char* getPluginName() {
        return "Track Measurement";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Meas";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    /// Dormant-until-consumed. Cheap to flip from the message thread; the audio
    /// thread reads it relaxed each block.
    void setMeasurementEnabled(bool shouldMeasure) noexcept {
        if (shouldMeasure && !enabled_.exchange(true, std::memory_order_acq_rel))
            pendingReset_.store(true, std::memory_order_release);
        else
            enabled_.store(shouldMeasure, std::memory_order_release);
    }
    bool isMeasurementEnabled() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    /// Persisted: run the oversampled true-peak path (master-only by policy).
    /// Takes effect on the next initialise(); the manager sets it at creation.
    void setMeasureTruePeak(bool shouldMeasure) {
        measureTruePeakValue_ = shouldMeasure;
    }
    bool getMeasureTruePeak() const {
        return measureTruePeakValue_.get();
    }

    /// Message thread. Latest measurements (lock-free).
    TrackMeasurementSnapshot getSnapshot() const noexcept {
        return measurer_.read();
    }

    /// Enable/disable mono signal capture for masking band analysis (heavier; on
    /// only during a masking pass). Message thread.
    void setSpectrumCaptureEnabled(bool shouldCapture) noexcept {
        measurer_.setSpectrumCaptureEnabled(shouldCapture);
    }

    /// Message thread. Compute the current per-band energy (dB) for masking.
    void getMaskingBandsDb(std::array<float, kNumMaskingBands>& out) const {
        computeMaskingBandsDb(measurer_.getSpectrumRing(), measurer_.sampleRate(), out);
    }

    /// Message thread. Copy the latest numSamples of captured mono signal (the
    /// masking spectrum ring) into dest, for a full-resolution overlay FFT
    /// (#1400). Returns the ring's running sample count (0 while still empty).
    size_t readLatestSpectrumSamples(float* dest, int numSamples) const noexcept {
        return measurer_.getSpectrumRing().readLatest(dest, numSamples);
    }
    double getSampleRate() const noexcept {
        return measurer_.sampleRate();
    }

    void initialise(const te::PluginInitialisationInfo& info) override {
        measurer_.prepare(info.sampleRate, juce::jmax(1, info.blockSizeSamples),
                          measureTruePeakValue_.get());
    }
    void deinitialise() override {}
    void reset() override {
        measurer_.reset();
    }

    void applyToBuffer(const te::PluginRenderContext& fc) override {
        // Transparent: read only, never write the buffer or MIDI.
        if (!enabled_.load(std::memory_order_acquire))
            return;  // dormant: single branch, no measurement cost
        if (!fc.destBuffer || fc.bufferNumSamples <= 0)
            return;

        if (pendingReset_.exchange(false, std::memory_order_acq_rel))
            measurer_.reset();  // fresh integration window each time a consumer re-enables

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
    void restorePluginStateFromValueTree(const juce::ValueTree& v) override {
        tracktion::copyPropertiesToCachedValues(v, measureTruePeakValue_);
    }

  private:
    TrackMeasurer measurer_;
    std::atomic<bool> enabled_{false};       // dormant until a consumer wants data
    std::atomic<bool> pendingReset_{false};  // re-enable -> clear gating history on audio thread
    juce::CachedValue<bool> measureTruePeakValue_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackMeasurementPlugin)
};

}  // namespace magda::daw::audio
