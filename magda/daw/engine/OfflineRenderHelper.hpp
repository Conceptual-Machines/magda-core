#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

#include <optional>

namespace magda {

class TracktionEngineWrapper;

struct OfflineRenderParametersOptions {
    juce::File destFile;
    juce::AudioFormat* audioFormat = nullptr;
    int bitDepth = 24;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool shouldNormalise = false;
    float normaliseToLevelDb = 0.0f;
    bool useMasterPlugins = true;
    bool usePlugins = true;
    bool checkNodesForAudio = false;
    bool realTimeRender = false;
    std::optional<tracktion::TimeRange> time;
};

void prepareEditForOfflineRender(tracktion::Edit& edit);
void preparePluginsForOfflineRender(TracktionEngineWrapper& engine);
void restorePluginsAfterOfflineRender(TracktionEngineWrapper& engine);

tracktion::Renderer::Parameters makeOfflineRenderParameters(
    tracktion::Edit& edit, const OfflineRenderParametersOptions& options);

}  // namespace magda
