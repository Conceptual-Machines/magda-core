#include "poly_step_sequencer_apply.hpp"

#include "api/plugin_api.hpp"

namespace magda {

juce::String applyPolyStepSequencerPresetToPath(PluginApi& plugins,
                                                const PolyStepSequencerAgent::Preset& preset,
                                                const ChainNodePath& path) {
    PolySequencerPattern pattern{
        .description = preset.description,
        .numSteps = preset.numSteps,
        .rate = preset.rate,
        .swing = preset.swing,
        .gateLength = preset.gateLength,
    };
    pattern.steps.reserve(preset.steps.size());
    for (const auto& step : preset.steps) {
        PolySequencerStep runtimeStep{
            .index = step.index,
            .gate = step.gate,
            .tie = step.tie,
            .probability = step.probability,
            .velocity = step.velocity,
        };
        runtimeStep.notes.reserve(step.notes.size());
        for (const auto& note : step.notes) {
            runtimeStep.notes.push_back({
                .noteNumber = note.noteNumber,
                .velocityOverride = note.velocityOverride,
            });
        }
        pattern.steps.push_back(std::move(runtimeStep));
    }
    return plugins.applyPolySequencerPattern(path, pattern);
}

}  // namespace magda
