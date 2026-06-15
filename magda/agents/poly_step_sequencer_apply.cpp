#include "poly_step_sequencer_apply.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "internal_plugins.hpp"

namespace magda {

juce::String applyPolyStepSequencerPresetToPath(const PolyStepSequencerAgent::Preset& preset,
                                                const ChainNodePath& path) {
    auto& tm = TrackManager::getInstance();
    auto* device = tm.getDeviceInChainByPath(path);
    if (device == nullptr ||
        internalPluginFromId(device->pluginId) != InternalPlugin::PolyStepSequencer)
        return "(target device is not a Poly Step Sequencer)";

    // Reach the live plugin via AudioBridge.
    auto* engine = tm.getAudioEngine();
    auto* bridge = engine ? engine->getAudioBridge() : nullptr;
    auto plugin = bridge ? bridge->getPlugin(path) : nullptr;
    auto* seq = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get());
    if (seq == nullptr)
        return "(could not resolve live PolyStepSequencerPlugin)";

    // --- Write global settings ---
    // These are CachedValues backed by the plugin's state ValueTree.
    // Writing via the state directly is the same pattern used by the UI.
    // Using nullptr as UndoManager so the globals don't pollute undo history;
    // the pattern write below goes through the plugin's undo-aware setters.
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
    // The plugin's clearStep() is the undo-safe public API for blanking a step.
    // When numSteps is absent from the preset (sentinel -1), read the live value
    // so we clear exactly the current active window, not a hardcoded default.
    const int numStepsToClear =
        juce::jlimit(1, daw::audio::PolyStepSequencerPlugin::MAX_STEPS,
                     preset.numSteps >= 1 ? preset.numSteps : seq->numSteps.get());
    for (int i = 0; i < numStepsToClear; ++i)
        seq->clearStep(i);

    // --- Write pattern steps ---
    int stepsWritten = 0;
    int notesWritten = 0;
    for (const auto& step : preset.steps) {
        if (step.index < 0 || step.index >= daw::audio::PolyStepSequencerPlugin::MAX_STEPS)
            continue;

        // clearStep above already set gate=off, no notes. Only re-set the
        // properties that differ from that baseline.
        seq->setStepGate(step.index, step.gate);

        if (step.tie)
            seq->setStepTie(step.index, true);

        if (step.probability < 1.0f)
            seq->setStepProbability(step.index, step.probability);

        if (step.velocity != 100)
            seq->setStepVelocity(step.index, step.velocity);

        for (const auto& note : step.notes) {
            seq->addStepNote(step.index, note.noteNumber, note.velocityOverride);
            ++notesWritten;
        }

        ++stepsWritten;
    }

    // Capture the mutated live plugin state into MAGDA's DeviceInfo so a
    // later syncTrackPlugins doesn't re-push stale state. Same approach as
    // four_osc_apply; intentionally skip notifyTrackDevicesChanged here so
    // the AI panel stays alive to show the apply status before rebuilding.
    if (bridge)
        bridge->getPluginManager().capturePluginState(path);

    // Stash the pattern description as the suggested preset name.
    if (!preset.description.empty())
        PresetManager::getInstance().setSuggestedPresetName(device->id,
                                                            juce::String(preset.description));

    return "applied " + juce::String(stepsWritten) + " step(s), " + juce::String(notesWritten) +
           " note(s) to " + device->name;
}

}  // namespace magda
