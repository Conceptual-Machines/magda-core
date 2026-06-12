#include "step_sequencer_apply.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "internal_plugins.hpp"

namespace magda {

juce::String applyStepSequencerPresetToPath(const StepSequencerAgent::Preset& preset,
                                            const ChainNodePath& path) {
    auto& tm = TrackManager::getInstance();
    auto* device = tm.getDeviceInChainByPath(path);
    if (device == nullptr ||
        internalPluginFromId(device->pluginId) != InternalPlugin::StepSequencer)
        return "(target device is not a Step Sequencer)";

    // Reach the live plugin via AudioBridge.
    auto* engine = tm.getAudioEngine();
    auto* bridge = engine ? engine->getAudioBridge() : nullptr;
    auto plugin = bridge ? bridge->getPlugin(path) : nullptr;
    auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get());
    if (seq == nullptr)
        return "(could not resolve live StepSequencerPlugin)";

    // --- Write global settings ---
    // CachedValues backed by the plugin's state ValueTree.
    // Pass the plugin's UndoManager so globals are undo-trackable alongside
    // the step writes below.
    auto* um = seq->getUndoManager();

    if (preset.numSteps >= 1)
        seq->state.setProperty(juce::Identifier("seqNumSteps"), preset.numSteps, um);

    if (preset.rate >= 0)
        seq->state.setProperty(juce::Identifier("seqRate"), preset.rate, um);

    if (preset.swing >= 0.0f)
        seq->state.setProperty(juce::Identifier("seqSwing"), preset.swing, um);

    if (preset.gateLength >= 0.0f)
        seq->state.setProperty(juce::Identifier("seqGateLength"), preset.gateLength, um);

    // --- Clear all existing steps ---
    // When numSteps is absent from the preset (sentinel -1), read the live value
    // so we clear exactly the current active window, not a hardcoded default.
    const int numStepsToClear =
        juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS,
                     preset.numSteps >= 1 ? preset.numSteps : seq->numSteps.get());
    for (int i = 0; i < numStepsToClear; ++i)
        seq->clearStep(i);

    // --- Write pattern steps ---
    int stepsWritten = 0;
    for (const auto& step : preset.steps) {
        if (step.index < 0 || step.index >= daw::audio::StepSequencerPlugin::MAX_STEPS)
            continue;

        // clearStep above reset everything to defaults (gate=true, note=60,
        // octave=0, accent=false, glide=false, tie=false). Only set fields
        // that differ from those defaults.
        seq->setStepGate(step.index, step.gate);

        if (step.noteNumber != 60)
            seq->setStepNote(step.index, step.noteNumber);

        if (step.octaveShift != 0)
            seq->setStepOctaveShift(step.index, step.octaveShift);

        if (step.accent)
            seq->setStepAccent(step.index, true);

        if (step.glide)
            seq->setStepGlide(step.index, true);

        if (step.tie)
            seq->setStepTie(step.index, true);

        ++stepsWritten;
    }

    // Capture the mutated live plugin state into MAGDA's DeviceInfo so a
    // later syncTrackPlugins doesn't re-push stale state. Same approach as
    // poly_step_sequencer_apply.
    if (bridge)
        bridge->getPluginManager().capturePluginState(path);

    // Stash the pattern description as the suggested preset name.
    if (!preset.description.empty())
        PresetManager::getInstance().setSuggestedPresetName(device->id,
                                                            juce::String(preset.description));

    return "applied " + juce::String(stepsWritten) + " step(s) to " + device->name;
}

}  // namespace magda
