#include "plugins/FollowerSourceTapPlugin.hpp"

#include <atomic>

#include "modifiers/ADSRDebugLog.hpp"
#include "plugin_manager/PluginManager.hpp"

namespace magda {

const char* FollowerSourceTapPlugin::xmlTypeName = "followersourcetap";

FollowerSourceTapPlugin::FollowerSourceTapPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    auto um = getUndoManager();
    sourceTrackIdValue.referTo(state, juce::Identifier("sourceTrackId"), um, INVALID_TRACK_ID);
    sourceTrackId_ = sourceTrackIdValue.get();
}

FollowerSourceTapPlugin::~FollowerSourceTapPlugin() {
    notifyListenersOfDeletion();
}

void FollowerSourceTapPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    monoScratch_.assign(static_cast<size_t>(juce::jmax(1, info.blockSizeSamples)), 0.0f);
    MAGDA_ADSR_AUDIO_LOG("follower-tap initialise sourceTrack="
                         << sourceTrackId_ << " blockSamples=" << info.blockSizeSamples
                         << " sampleRate=" << info.sampleRate);
}

void FollowerSourceTapPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough - never modify audio.
    if (!pluginManager_ || !fc.destBuffer || fc.bufferNumSamples <= 0) {
        static std::atomic<int> skippedLogThrottle{0};
        if ((skippedLogThrottle.fetch_add(1, std::memory_order_relaxed) % 200) == 0) {
            MAGDA_ADSR_AUDIO_LOG("follower-tap skip sourceTrack="
                                 << sourceTrackId_
                                 << " hasPm=" << static_cast<int>(pluginManager_ != nullptr)
                                 << " hasBuffer=" << static_cast<int>(fc.destBuffer != nullptr)
                                 << " numSamples=" << fc.bufferNumSamples);
        }
        return;
    }

    const int numSamples = fc.bufferNumSamples;
    if (static_cast<int>(monoScratch_.size()) < numSamples) {
        MAGDA_ADSR_AUDIO_LOG("follower-tap skip-too-large sourceTrack="
                             << sourceTrackId_
                             << " scratch=" << static_cast<int>(monoScratch_.size())
                             << " numSamples=" << numSamples);
        return;  // Block larger than prepared scratch; skip this block's detection.
    }

    // Linear mono downmix (mean of channels) - preserve sign so the per-follower
    // band-limit filters see real frequency content. Rectification happens after
    // filtering, inside pushFollowerSourceBuffer.
    const int numChannels = fc.destBuffer->getNumChannels();
    float* mono = monoScratch_.data();
    if (numChannels <= 0) {
        std::fill(mono, mono + numSamples, 0.0f);
    } else {
        const float scale = 1.0f / static_cast<float>(numChannels);
        for (int i = 0; i < numSamples; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += fc.destBuffer->getSample(ch, fc.bufferStartSample + i);
            mono[i] = sum * scale;
        }
    }

    static std::atomic<int> blockLogThrottle{0};
    if ((blockLogThrottle.fetch_add(1, std::memory_order_relaxed) % 100) == 0) {
        float monoPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            monoPeak = std::max(monoPeak, std::abs(mono[i]));
        MAGDA_ADSR_AUDIO_LOG("follower-tap block sourceTrack="
                             << sourceTrackId_ << " channels=" << numChannels
                             << " start=" << fc.bufferStartSample << " samples=" << numSamples
                             << " monoPeak=" << monoPeak << " sampleRate=" << sampleRate_);
    }

    pluginManager_->pushFollowerSourceBuffer(sourceTrackId_, mono, numSamples, sampleRate_);
}

void FollowerSourceTapPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void FollowerSourceTapPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
    MAGDA_ADSR_AUDIO_LOG("follower-tap set-source sourceTrack=" << sourceTrackId_);
}

}  // namespace magda
