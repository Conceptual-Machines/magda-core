#include "plugin_api_live.hpp"

#include <set>

#include "../audio/faust/FaustModelEdits.hpp"
#include "../audio/plugins/FaustInstrumentPlugin.hpp"
#include "../audio/plugins/FaustPlugin.hpp"
#include "../audio/plugins/IFaustEditorModel.hpp"
#include "../audio/plugins/PolyStepSequencerPlugin.hpp"
#include "../audio/plugins/StepSequencerPlugin.hpp"
#include "../core/ChainWalk.hpp"
#include "../core/DrumGridPads.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/PresetManager.hpp"
#include "../core/StepPatternCommands.hpp"
#include "../core/TrackManager.hpp"
#include "../core/aliases/ParamNameNormalize.hpp"
#include "../engine/AudioEngine.hpp"
#include "../engine/PluginService.hpp"

namespace magda {
namespace {

std::vector<DeviceInfo> toDeviceInfo(const juce::Array<juce::PluginDescription>& descriptions) {
    std::vector<DeviceInfo> plugins;
    plugins.reserve(static_cast<size_t>(descriptions.size()));
    for (const auto& description : descriptions) {
        DeviceInfo plugin;
        plugin.name = description.name;
        plugin.pluginId = description.createIdentifierString();
        plugin.manufacturer = description.manufacturerName;
        plugin.format = pluginFormatFromName(description.pluginFormatName);
        plugin.isInstrument = description.isInstrument;
        plugin.deviceType = description.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
        plugin.uniqueId = description.createIdentifierString();
        plugin.fileOrIdentifier = description.fileOrIdentifier;
        plugins.push_back(std::move(plugin));
    }
    return plugins;
}

/// A device's parameter in its display domain, or @p fallback when the model
/// has no entry for that slot yet.
///
/// By paramIndex, never by array position: DeviceInfo::parameters is built in
/// the saved document's order and a migration can drop an entry, so the two
/// part company and a positional read hands back a neighbouring slot's value
/// (DeviceInfo::findParameterByIndex, #2335).
float deviceParameterValue(const DeviceInfo& device, int index, float fallback) {
    const auto* parameter = device.findParameterByIndex(index);
    return parameter != nullptr ? parameter->currentValue : fallback;
}

}  // namespace

std::vector<DeviceInfo> PluginApiLive::getExternalPlugins() const {
    return toDeviceInfo(PluginService::getInstance().preferredTypes());
}

std::vector<DeviceInfo> PluginApiLive::getAllExternalPlugins() const {
    return toDeviceInfo(PluginService::getInstance().knownTypes());
}

std::optional<SequencerRuntimeContext> PluginApiLive::getStepSequencerContext(
    const ChainNodePath& path) const {
    // The model, not the live device: the pattern and the slot values are what
    // the model holds (#2313/#2317), and an agent can ask about a sequencer on
    // a track the engine has not instantiated.
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr || !device->pluginId.equalsIgnoreCase("stepsequencer"))
        return std::nullopt;

    using Seq = daw::audio::StepSequencerPlugin;
    return SequencerRuntimeContext{
        .numSteps = step_pattern::monoPatternOf(device->pluginState).playingLength(),
        .rate = juce::roundToInt(deviceParameterValue(*device, Seq::kRate, 7.0f)),
        .swing = deviceParameterValue(*device, Seq::kSwing, 0.0f),
        .gateLength = deviceParameterValue(*device, Seq::kGateLength, 0.8f),
    };
}

std::optional<SequencerRuntimeContext> PluginApiLive::getPolySequencerContext(
    const ChainNodePath& path) const {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr || !device->pluginId.equalsIgnoreCase("polystepsequencer"))
        return std::nullopt;

    using Seq = daw::audio::PolyStepSequencerPlugin;
    SequencerRuntimeContext context{
        .numSteps = step_pattern::polyPatternOf(device->pluginState).playingLength(),
        .rate = juce::roundToInt(deviceParameterValue(*device, Seq::kRate, 7.0f)),
        .swing = deviceParameterValue(*device, Seq::kSwing, 0.0f),
        .gateLength = deviceParameterValue(*device, Seq::kGateLength, 0.8f),
    };

    if (auto doc = device_state::decode(device->pluginState)) {
        if (const auto* mode = doc->root.props.getVarPointer(Seq::SettingIDs::viewMode))
            context.viewMode = mode->toString();
    }

    // The drum grid this sequencer plays into names its lanes, so an agent can
    // write "kick" rather than note 36: the first one after it in the chain.
    const auto* track = TrackManager::getInstance().getTrack(path.trackId);
    if (track == nullptr)
        return context;

    bool passedSequencer = false;
    const DeviceInfo* drumGrid = nullptr;
    const DeviceInfo* fallback = nullptr;
    chain_walk::forEachDevice(track->chain.fxChainElements, ChainNodePath::trackLevel(path.trackId),
                              chain_walk::Pads::Skip,
                              [&](const DeviceInfo& candidate, const ChainNodePath& candidatePath) {
                                  if (candidatePath == path) {
                                      passedSequencer = true;
                                      return true;
                                  }
                                  if (!isPadRackDevice(candidate.pluginId) || !candidate.pads)
                                      return true;
                                  if (passedSequencer) {
                                      drumGrid = &candidate;
                                      return false;
                                  }
                                  if (fallback == nullptr)
                                      fallback = &candidate;
                                  return true;
                              });

    if (drumGrid == nullptr && !passedSequencer)
        drumGrid = fallback;
    if (drumGrid != nullptr) {
        for (const auto& pad : drumGrid->pads->chains)
            context.laneNames.emplace_back(pad.lowNote, pad.name);
    }
    return context;
}

juce::String PluginApiLive::applyStepSequencerPattern(const ChainNodePath& path,
                                                      const StepSequencerPattern& pattern) {
    auto& trackManager = TrackManager::getInstance();
    auto* device = trackManager.getDeviceInChainByPath(path);
    if (device == nullptr || !device->pluginId.equalsIgnoreCase("stepsequencer"))
        return "(target device is not a Step Sequencer)";

    using Seq = daw::audio::StepSequencerPlugin;

    // Rate, swing and gate are slots, so they go through the model's parameter
    // write path; the pattern is authored state, so it goes through the model's
    // undoable pattern edit (#2313). Neither touches the live device directly.
    if (pattern.rate >= 0)
        trackManager.setDeviceParameterValue(path, Seq::kRate, static_cast<float>(pattern.rate));
    if (pattern.swing >= 0.0f)
        trackManager.setDeviceParameterValue(path, Seq::kSwing, pattern.swing);
    if (pattern.gateLength >= 0.0f)
        trackManager.setDeviceParameterValue(path, Seq::kGateLength, pattern.gateLength);

    int stepsWritten = 0;
    const bool committed =
        editMonoStepPattern(path, "Apply Step Pattern", [&](step_pattern::MonoPattern& target) {
            if (pattern.numSteps >= 1)
                target.length = juce::jlimit(1, Seq::MAX_STEPS, pattern.numSteps);

            // The incoming pattern is the whole pattern: steps it does not mention
            // are rests, not leftovers from what was there before.
            const int playing = target.playingLength();
            for (int i = 0; i < playing; ++i)
                target.steps[static_cast<size_t>(i)] = Seq::Step{};

            for (const auto& step : pattern.steps) {
                if (step.index < 0 || step.index >= Seq::MAX_STEPS)
                    continue;
                auto& written = target.steps[static_cast<size_t>(step.index)];
                written.noteNumber = juce::jlimit(0, 127, step.noteNumber);
                written.octaveShift = juce::jlimit(-2, 2, step.octaveShift);
                written.gate = step.gate;
                written.accent = step.accent;
                written.glide = step.glide;
                written.tie = step.tie;
                ++stepsWritten;
            }
        });

    // The lambda only runs when the model will accept the edit, so nothing
    // written and no commit means the write was refused - a state document
    // this build cannot rewrite, say. Reporting that as a success left the
    // agent believing a pattern had landed that never did (#2335).
    if (!committed && stepsWritten == 0)
        return "(could not write the pattern to " + device->name + ")";

    if (pattern.description.isNotEmpty())
        PresetManager::getInstance().setSuggestedPresetName(device->id, pattern.description);
    return "applied " + juce::String(stepsWritten) + " step(s) to " + device->name;
}

juce::String PluginApiLive::applyPolySequencerPattern(const ChainNodePath& path,
                                                      const PolySequencerPattern& pattern) {
    auto& trackManager = TrackManager::getInstance();
    auto* device = trackManager.getDeviceInChainByPath(path);
    if (device == nullptr || !device->pluginId.equalsIgnoreCase("polystepsequencer"))
        return "(target device is not a Poly Step Sequencer)";

    using Seq = daw::audio::PolyStepSequencerPlugin;

    if (pattern.rate >= 0)
        trackManager.setDeviceParameterValue(path, Seq::kRate, static_cast<float>(pattern.rate));
    if (pattern.swing >= 0.0f)
        trackManager.setDeviceParameterValue(path, Seq::kSwing, pattern.swing);
    if (pattern.gateLength >= 0.0f)
        trackManager.setDeviceParameterValue(path, Seq::kGateLength, pattern.gateLength);

    int stepsWritten = 0;
    int notesWritten = 0;
    const bool committed = editPolyStepPattern(
        path, "Apply Poly Step Pattern", [&](step_pattern::PolyPattern& target) {
            if (pattern.numSteps >= 1)
                target.length = juce::jlimit(1, Seq::MAX_STEPS, pattern.numSteps);

            // The incoming pattern is the whole pattern: steps it does not mention
            // are rests, not leftovers from what was there before.
            const int playing = target.playingLength();
            for (int i = 0; i < playing; ++i)
                target.steps[static_cast<size_t>(i)] = Seq::Step{};

            for (const auto& step : pattern.steps) {
                if (step.index < 0 || step.index >= Seq::MAX_STEPS)
                    continue;
                auto& written = target.steps[static_cast<size_t>(step.index)];
                written.gate = step.gate;
                written.tie = step.tie;
                written.probability = juce::jlimit(0.0f, 1.0f, step.probability);
                written.velocity = juce::jlimit(1, 127, step.velocity);
                written.noteCount = 0;
                for (const auto& note : step.notes) {
                    if (written.noteCount >= Seq::MAX_NOTES_PER_STEP)
                        break;
                    auto& target_note = written.notes[static_cast<size_t>(written.noteCount)];
                    target_note.noteNumber = juce::jlimit(0, 127, note.noteNumber);
                    target_note.velocity = juce::jlimit(0, 127, note.velocityOverride);
                    ++written.noteCount;
                    ++notesWritten;
                }
                ++stepsWritten;
            }
        });

    // See applyStepSequencerPattern.
    if (!committed && stepsWritten == 0)
        return "(could not write the pattern to " + device->name + ")";

    if (pattern.description.isNotEmpty())
        PresetManager::getInstance().setSuggestedPresetName(device->id, pattern.description);
    return "applied " + juce::String(stepsWritten) + " step(s), " + juce::String(notesWritten) +
           " note(s) to " + device->name;
}

juce::String PluginApiLive::applyFourOscUpdate(const ChainNodePath&, const FourOscUpdate&) {
    return "4OSC is retired in v1. Convert the project with the existing 4OSC translator.";
}

juce::String PluginApiLive::applyFaustSource(const ChainNodePath& path,
                                             const juce::String& displayName,
                                             const juce::String& source, bool verified) {
    auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr ||
        (!device->pluginId.equalsIgnoreCase(daw::audio::FaustPlugin::xmlTypeName) &&
         !device->pluginId.equalsIgnoreCase(daw::audio::FaustInstrumentPlugin::xmlTypeName)))
        return "(target device is not a Faust plugin)";

    if (!verified) {
        auto* engine = TrackManager::getInstance().getAudioEngine();
        const auto device = engine != nullptr ? engine->renderedDevice(path) : nullptr;
        auto* faust = dynamic_cast<daw::audio::IFaustEditorModel*>(device.get());
        if (faust == nullptr)
            return "(could not resolve live Faust plugin)";
        faust->stageSourceForEditing(displayName, source);
        return "generated \"" + displayName +
               "\" - open the editor to compile (Faust MCP disabled)";
    }

    juce::String error;
    if (!faust_edits::loadSource(path, displayName, source, error))
        return "compile error: " + error;
    return "applied \"" + displayName + "\"";
}

}  // namespace magda
