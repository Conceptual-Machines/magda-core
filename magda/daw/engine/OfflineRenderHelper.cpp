#include "OfflineRenderHelper.hpp"

#include "../audio/AudioBridge.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda {

void prepareEditForOfflineRender(tracktion::Edit& edit) {
    auto& transport = edit.getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    tracktion::freePlaybackContextIfNotRecording(transport);

    // Nothing here may touch plugin enablement (#1880). Every TE plugin's
    // enabled flag already mirrors TrackManager::isDeviceEffectivelyEnabled,
    // so powered-off devices must stay off through the render and stay off
    // afterwards.
}

void preparePluginsForOfflineRender(TracktionEngineWrapper& engine) {
    if (auto* bridge = engine.getAudioBridge())
        bridge->getPluginManager().prepareForRendering();
}

void restorePluginsAfterOfflineRender(TracktionEngineWrapper& engine) {
    if (auto* bridge = engine.getAudioBridge())
        bridge->getPluginManager().restoreAfterRendering();
}

tracktion::Renderer::Parameters makeOfflineRenderParameters(
    tracktion::Edit& edit, const OfflineRenderParametersOptions& options) {
    tracktion::Renderer::Parameters params(edit);
    params.destFile = options.destFile;
    params.audioFormat = options.audioFormat;
    params.bitDepth = options.bitDepth;
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio = options.blockSize;
    params.shouldNormalise = options.shouldNormalise;
    params.normaliseToLevelDb = options.normaliseToLevelDb;
    params.useMasterPlugins = options.useMasterPlugins;
    params.usePlugins = options.usePlugins;
    params.checkNodesForAudio = options.checkNodesForAudio;
    params.realTimeRender = options.realTimeRender;
    if (options.time)
        params.time = *options.time;
    return params;
}

}  // namespace magda
