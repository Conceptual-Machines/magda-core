#include "plugins/AudioSidechainMonitorPlugin.hpp"

#include "modifiers/ADSRDebugLog.hpp"
#include "plugin_manager/PluginManager.hpp"
#include "plugins/SidechainTriggerBus.hpp"

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
    MAGDA_ADSR_AUDIO_LOG("monitor initialise sourceTrack="
                         << sourceTrackId_ << " blockSamples=" << info.blockSizeSamples
                         << " sampleRate=" << info.sampleRate << " attackCoeff=" << attackCoeff_
                         << " releaseCoeff=" << releaseCoeff_);
}

void AudioSidechainMonitorPlugin::deinitialise() {}

void AudioSidechainMonitorPlugin::reset() {
    MAGDA_ADSR_AUDIO_LOG("monitor reset sourceTrack=" << sourceTrackId_);
    envLevel_ = 0.0f;
    gateOpen_ = false;
    lastBlockHadCandidate_ = false;
}

void AudioSidechainMonitorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough — don't modify audio or MIDI

    ++heartbeatCount_;

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

    // Note: envelope followers are fed separately by FollowerSourceTapPlugin
    // (post-FX, band-limited). This monitor only drives note/gate triggers for
    // LFO/ADSR modifiers below.

    const float envBefore = envLevel_;
    const bool gateBefore = gateOpen_;

    // Envelope follower: smooth the peak level for stable gate behaviour
    if (peak > envLevel_)
        envLevel_ += attackCoeff_ * (peak - envLevel_);
    else
        envLevel_ += releaseCoeff_ * (peak - envLevel_);

    const bool rawCandidate = peak > kThreshold;
    const bool envCandidate = envLevel_ > kThreshold;
    if (rawCandidate || envCandidate || lastBlockHadCandidate_) {
        MAGDA_ADSR_AUDIO_LOG("monitor block sourceTrack="
                             << sourceTrackId_ << " peak=" << peak << " envBefore=" << envBefore
                             << " envAfter=" << envLevel_
                             << " rawCandidate=" << static_cast<int>(rawCandidate)
                             << " envCandidate=" << static_cast<int>(envCandidate) << " gateBefore="
                             << static_cast<int>(gateBefore) << " heartbeat=" << heartbeatCount_);
    }
    lastBlockHadCandidate_ = rawCandidate || envCandidate;

    // Gate detection uses the smoothed envelope to avoid false triggers
    // from transient peaks and chattering at the threshold boundary.
    if (!gateOpen_ && envLevel_ > kThreshold) {
        gateOpen_ = true;
        MAGDA_ADSR_AUDIO_LOG("monitor gate-open sourceTrack="
                             << sourceTrackId_ << " peak=" << peak << " env=" << envLevel_
                             << " threshold=" << kThreshold << " heartbeat=" << heartbeatCount_);
        if (pluginManager_)
            pluginManager_->triggerSidechainNoteOn(sourceTrackId_, LFOTriggerMode::Audio);
        else
            MAGDA_ADSR_AUDIO_LOG(
                "monitor gate-open has no PluginManager sourceTrack=" << sourceTrackId_);
    } else if (gateOpen_ && envLevel_ < kThreshold) {
        gateOpen_ = false;
        MAGDA_ADSR_AUDIO_LOG("monitor gate-close sourceTrack="
                             << sourceTrackId_ << " peak=" << peak << " env=" << envLevel_
                             << " threshold=" << kThreshold << " heartbeat=" << heartbeatCount_);
        if (pluginManager_)
            pluginManager_->gateSidechainLFOs(sourceTrackId_);
        else
            MAGDA_ADSR_AUDIO_LOG(
                "monitor gate-close has no PluginManager sourceTrack=" << sourceTrackId_);
    } else if (rawCandidate && !envCandidate) {
        MAGDA_ADSR_AUDIO_LOG("monitor raw-hit-no-trigger sourceTrack="
                             << sourceTrackId_ << " peak=" << peak << " env=" << envLevel_
                             << " gateOpen=" << static_cast<int>(gateOpen_)
                             << " threshold=" << kThreshold << " heartbeat=" << heartbeatCount_);
    } else if (rawCandidate && gateOpen_) {
        MAGDA_ADSR_AUDIO_LOG("monitor raw-hit-gate-already-open sourceTrack="
                             << sourceTrackId_ << " peak=" << peak << " env=" << envLevel_
                             << " threshold=" << kThreshold << " heartbeat=" << heartbeatCount_);
    }
}

void AudioSidechainMonitorPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void AudioSidechainMonitorPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
    MAGDA_ADSR_AUDIO_LOG("monitor set sourceTrack=" << sourceTrackId_);
}

}  // namespace magda
