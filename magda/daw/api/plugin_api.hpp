#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../core/ChainNodePath.hpp"
#include "../core/DeviceInfo.hpp"

namespace magda {

struct StepSequencerStep {
    int index = 0;
    int noteNumber = 60;
    int octaveShift = 0;
    bool gate = true;
    bool accent = false;
    bool glide = false;
    bool tie = false;
};

struct StepSequencerPattern {
    juce::String description;
    int numSteps = -1;
    int rate = -1;
    float swing = -1.0f;
    float gateLength = -1.0f;
    std::vector<StepSequencerStep> steps;
};

struct PolySequencerNote {
    int noteNumber = 60;
    int velocityOverride = 0;
};

struct PolySequencerStep {
    int index = 0;
    bool gate = true;
    bool tie = false;
    float probability = 1.0f;
    int velocity = 100;
    std::vector<PolySequencerNote> notes;
};

struct PolySequencerPattern {
    juce::String description;
    int numSteps = -1;
    int rate = -1;
    float swing = -1.0f;
    float gateLength = -1.0f;
    std::vector<PolySequencerStep> steps;
};

struct SequencerRuntimeContext {
    int numSteps = 0;
    int rate = 0;
    float swing = 0.0f;
    float gateLength = 0.0f;
    juce::String viewMode;
    std::vector<std::pair<int, juce::String>> laneNames;
};

struct FourOscUpdate {
    juce::String name;
    juce::String category;
    std::map<juce::String, float> parameters;
    std::map<int, juce::String> waves;
    juce::String filterType;
    juce::String voiceMode;
    std::map<juce::String, bool> effects;
};

/** Engine-neutral plugin catalog and live-device operations exposed through MagdaApi. */
class PluginApi {
  public:
    virtual ~PluginApi() = default;

    /** Scanned external plugins, filtered by the user's preferred host format. */
    virtual std::vector<DeviceInfo> getExternalPlugins() const = 0;

    /** All scanned external plugin variants, without preferred-format filtering. */
    virtual std::vector<DeviceInfo> getAllExternalPlugins() const = 0;

    virtual std::optional<SequencerRuntimeContext> getStepSequencerContext(
        const ChainNodePath& path) const = 0;
    virtual std::optional<SequencerRuntimeContext> getPolySequencerContext(
        const ChainNodePath& path) const = 0;

    virtual juce::String applyStepSequencerPattern(const ChainNodePath& path,
                                                   const StepSequencerPattern& pattern) = 0;
    virtual juce::String applyPolySequencerPattern(const ChainNodePath& path,
                                                   const PolySequencerPattern& pattern) = 0;
    virtual juce::String applyFourOscUpdate(const ChainNodePath& path,
                                            const FourOscUpdate& update) = 0;
    virtual juce::String applyFaustSource(const ChainNodePath& path,
                                          const juce::String& displayName,
                                          const juce::String& source, bool verified) = 0;
};

}  // namespace magda
