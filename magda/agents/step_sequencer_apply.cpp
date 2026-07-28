#include "step_sequencer_apply.hpp"

#include "api/plugin_api.hpp"

namespace magda {

juce::String applyStepSequencerPresetToPath(PluginApi& plugins,
                                            const StepSequencerAgent::Preset& preset,
                                            const ChainNodePath& path) {
    StepSequencerPattern pattern{
        .description = preset.description,
        .numSteps = preset.numSteps,
        .rate = preset.rate,
        .swing = preset.swing,
        .gateLength = preset.gateLength,
    };
    pattern.steps.reserve(preset.steps.size());
    for (const auto& step : preset.steps) {
        pattern.steps.push_back({
            .index = step.index,
            .noteNumber = step.noteNumber,
            .octaveShift = step.octaveShift,
            .gate = step.gate,
            .accent = step.accent,
            .glide = step.glide,
            .tie = step.tie,
        });
    }
    return plugins.applyStepSequencerPattern(path, pattern);
}

}  // namespace magda
