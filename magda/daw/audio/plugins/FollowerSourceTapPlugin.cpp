#include "plugins/FollowerSourceTapPlugin.hpp"

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
}

void FollowerSourceTapPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough - never modify audio.
    if (!realtimeContext_ || !fc.destBuffer || fc.bufferNumSamples <= 0) {
        return;
    }

    const int numSamples = fc.bufferNumSamples;
    if (static_cast<int>(monoScratch_.size()) < numSamples) {
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

    realtimeContext_->pushFollowerSourceBuffer(sourceTrackId_, mono, numSamples, sampleRate_);
}

void FollowerSourceTapPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void FollowerSourceTapPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
}

}  // namespace magda
