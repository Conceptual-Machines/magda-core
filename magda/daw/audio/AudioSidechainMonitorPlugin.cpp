#include "AudioSidechainMonitorPlugin.hpp"

#include "PluginManager.hpp"
#include "SidechainTriggerBus.hpp"

namespace magda {

const char* AudioSidechainMonitorPlugin::xmlTypeName = "audiosidechainmonitor";

AudioSidechainMonitorPlugin::AudioSidechainMonitorPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    auto um = getUndoManager();
    sourceTrackIdValue.referTo(state, juce::Identifier("sourceTrackId"), um, INVALID_TRACK_ID);
    sourceTrackId_ = sourceTrackIdValue.get();
}

AudioSidechainMonitorPlugin::~AudioSidechainMonitorPlugin() {
    notifyListenersOfDeletion();
}

void AudioSidechainMonitorPlugin::initialise(const te::PluginInitialisationInfo& info) {
    // Precompute per-block envelope coefficients from sample rate and block size.
    // Using block-based timing (not wall clock) ensures correct behavior during
    // offline rendering where blocks advance faster than real-time.
    double blockDurationMs = (info.blockSizeSamples / info.sampleRate) * 1000.0;
    if (blockDurationMs > 0.0) {
        attackCoeff_ = 1.0f - std::exp(static_cast<float>(-blockDurationMs / kAttackMs));
        releaseCoeff_ = 1.0f - std::exp(static_cast<float>(-blockDurationMs / kReleaseMs));
    }
}

void AudioSidechainMonitorPlugin::deinitialise() {}

void AudioSidechainMonitorPlugin::reset() {
    envLevel_ = 0.0f;
    gateOpen_ = false;
}

void AudioSidechainMonitorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough — don't modify audio or MIDI

    ++heartbeatCount_;
    if (heartbeatCount_ == 1 || heartbeatCount_ % 430 == 0) {
        DBG("[ASC-MON] applyToBuffer srcTrack=" << sourceTrackId_ << " beat=" << heartbeatCount_
                                                << " envLevel=" << envLevel_
                                                << " gateOpen=" << (int)gateOpen_);
    }

    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Compute peak amplitude from the audio buffer
    float peak = 0.0f;
    int numChannels = fc.destBuffer->getNumChannels();
    for (int ch = 0; ch < numChannels; ++ch) {
        float chPeak = fc.destBuffer->getMagnitude(ch, fc.bufferStartSample, fc.bufferNumSamples);
        peak = std::max(peak, chPeak);
    }

    // Write peak to SidechainTriggerBus for UI metering
    SidechainTriggerBus::getInstance().setAudioPeakLevel(sourceTrackId_, peak);

    // Envelope follower: smooth the peak level
    if (peak > envLevel_)
        envLevel_ += attackCoeff_ * (peak - envLevel_);
    else
        envLevel_ += releaseCoeff_ * (peak - envLevel_);

    // Threshold-based gate detection
    if (!gateOpen_ && peak > kThreshold) {
        gateOpen_ = true;
        DBG("[ASC-MON] gate OPEN srcTrack=" << sourceTrackId_ << " peak=" << peak);
        if (pluginManager_)
            pluginManager_->triggerSidechainNoteOn(sourceTrackId_, LFOTriggerMode::Audio);
    } else if (gateOpen_ && peak < kThreshold) {
        gateOpen_ = false;
        DBG("[ASC-MON] gate CLOSE srcTrack=" << sourceTrackId_ << " peak=" << peak);
        if (pluginManager_)
            pluginManager_->gateSidechainLFOs(sourceTrackId_);
    }
}

void AudioSidechainMonitorPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void AudioSidechainMonitorPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
}

}  // namespace magda
