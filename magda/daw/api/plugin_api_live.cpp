#include "plugin_api_live.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <set>

#include "../audio/AudioBridge.hpp"
#include "../audio/plugins/DrumGridPlugin.hpp"
#include "../audio/plugins/FaustInstrumentPlugin.hpp"
#include "../audio/plugins/FaustPlugin.hpp"
#include "../audio/plugins/IFaustEditorModel.hpp"
#include "../audio/plugins/PolyStepSequencerPlugin.hpp"
#include "../audio/plugins/StepSequencerPlugin.hpp"
#include "../audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/PresetManager.hpp"
#include "../core/StepPatternCommands.hpp"
#include "../core/TrackManager.hpp"
#include "../core/aliases/ParamNameNormalize.hpp"
#include "../engine/AudioEngine.hpp"

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
float deviceParameterValue(const DeviceInfo& device, int index, float fallback) {
    if (index < 0 || index >= static_cast<int>(device.parameters.size()))
        return fallback;
    return device.parameters[static_cast<size_t>(index)].currentValue;
}

AudioBridge* getAudioBridge() {
    if (auto* engine = TrackManager::getInstance().getAudioEngine())
        return engine->getAudioBridge();
    return nullptr;
}

int waveNameToShapeInt(const juce::String& name) {
    const auto key = name.trim().toLowerCase();
    if (key == "none" || key == "off")
        return 0;
    if (key == "sine")
        return 1;
    if (key == "square")
        return 2;
    if (key == "saw" || key == "saw_up" || key == "saw_down")
        return 3;
    if (key == "triangle")
        return 4;
    if (key == "noise" || key == "random")
        return 5;
    return -1;
}

int filterTypeNameToInt(const juce::String& name) {
    const auto key = name.trim().toLowerCase();
    if (key == "off" || key == "bypass" || key == "none")
        return 0;
    if (key == "lp" || key == "lowpass" || key == "low_pass")
        return 1;
    if (key == "hp" || key == "highpass" || key == "high_pass")
        return 2;
    if (key == "bp" || key == "bandpass" || key == "band_pass")
        return 3;
    if (key == "notch" || key == "bandreject" || key == "band_reject")
        return 4;
    return -1;
}

int voiceModeNameToInt(const juce::String& name) {
    const auto key = name.trim().toLowerCase();
    if (key == "mono")
        return 0;
    if (key == "leg" || key == "legato")
        return 1;
    if (key == "poly" || key == "polyphonic")
        return 2;
    return -1;
}

}  // namespace

std::vector<DeviceInfo> PluginApiLive::getExternalPlugins() const {
    if (auto* engine = TrackManager::getInstance().getAudioEngine())
        return toDeviceInfo(engine->getPreferredPluginTypes());
    return {};
}

std::vector<DeviceInfo> PluginApiLive::getAllExternalPlugins() const {
    if (auto* engine = TrackManager::getInstance().getAudioEngine())
        return toDeviceInfo(engine->getKnownPluginTypes());
    return {};
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
    // write "kick" rather than note 36. The chain is still the engine's to
    // walk: the model has no ordering of a rack's inner plugins.
    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* sequencer = daw::audio::tracktion_adapter::deviceFromPlugin<Seq>(plugin.get()) != nullptr
                          ? plugin.get()
                          : nullptr;
    if (sequencer == nullptr)
        return context;

    auto* track = sequencer->getOwnerTrack();
    if (track == nullptr)
        return context;

    namespace te = tracktion::engine;
    bool passedSequencer = false;
    daw::audio::DrumGridPlugin* drumGrid = nullptr;
    daw::audio::DrumGridPlugin* fallback = nullptr;
    for (auto* candidate : track->pluginList) {
        if (candidate == sequencer) {
            passedSequencer = true;
            continue;
        }

        auto* found = dynamic_cast<daw::audio::DrumGridPlugin*>(candidate);
        if (found == nullptr) {
            if (auto* rack = dynamic_cast<te::RackInstance*>(candidate);
                rack != nullptr && rack->type != nullptr) {
                for (auto* inner : rack->type->getPlugins()) {
                    found = dynamic_cast<daw::audio::DrumGridPlugin*>(inner);
                    if (found != nullptr)
                        break;
                }
            }
        }

        if (found != nullptr) {
            if (passedSequencer) {
                drumGrid = found;
                break;
            }
            if (fallback == nullptr)
                fallback = found;
        }
    }

    if (drumGrid == nullptr && !passedSequencer)
        drumGrid = fallback;
    if (drumGrid != nullptr) {
        for (const auto& chain : drumGrid->getChains()) {
            if (chain != nullptr)
                context.laneNames.emplace_back(chain->lowNote, chain->name);
        }
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
    editPolyStepPattern(path, "Apply Poly Step Pattern", [&](step_pattern::PolyPattern& target) {
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

    if (pattern.description.isNotEmpty())
        PresetManager::getInstance().setSuggestedPresetName(device->id, pattern.description);
    return "applied " + juce::String(stepsWritten) + " step(s), " + juce::String(notesWritten) +
           " note(s) to " + device->name;
}

juce::String PluginApiLive::applyFourOscUpdate(const ChainNodePath& path,
                                               const FourOscUpdate& update) {
    auto& trackManager = TrackManager::getInstance();
    auto* device = trackManager.getDeviceInChainByPath(path);
    if (device == nullptr || !device->pluginId.equalsIgnoreCase("4osc"))
        return "(target device is not a 4OSC)";

    std::map<juce::String, int> indexByName;
    for (int i = 0; i < static_cast<int>(device->parameters.size()); ++i) {
        const auto key = normalizeParamName(device->parameters[static_cast<size_t>(i)].name);
        if (key.isNotEmpty())
            indexByName[key] = i;
    }

    static const std::set<juce::String> realValueParameters = {
        "amp_attack",     "amp_decay",   "amp_release", "filter_attack", "filter_decay",
        "filter_release", "tune_1",      "tune_2",      "tune_3",        "tune_4",
        "fine_tune_1",    "fine_tune_2", "fine_tune_3", "fine_tune_4",
    };

    int parametersApplied = 0;
    int parametersSkipped = 0;
    for (const auto& [name, value] : update.parameters) {
        const auto found = indexByName.find(name);
        if (found == indexByName.end()) {
            ++parametersSkipped;
            continue;
        }
        const auto& info = device->parameters[static_cast<size_t>(found->second)];
        const float real =
            realValueParameters.count(name) ? value : ParameterUtils::normalizedToReal(value, info);
        trackManager.setDeviceParameterValue(path, found->second, real);
        ++parametersApplied;
    }

    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* fourOsc = dynamic_cast<tracktion::engine::FourOscPlugin*>(plugin.get());

    int wavesApplied = 0;
    int wavesSkipped = 0;
    if (fourOsc != nullptr) {
        for (const auto& [oscillator, wave] : update.waves) {
            const int shape = waveNameToShapeInt(wave);
            if (shape < 0) {
                ++wavesSkipped;
                continue;
            }
            fourOsc->state.setProperty("waveShape" + juce::String(oscillator), shape, nullptr);
            ++wavesApplied;
        }
    } else {
        wavesSkipped = static_cast<int>(update.waves.size());
    }

    bool filterTypeApplied = false;
    bool voiceModeApplied = false;
    int effectsToggled = 0;
    if (fourOsc != nullptr) {
        if (update.filterType.isNotEmpty()) {
            const int filterType = filterTypeNameToInt(update.filterType);
            if (filterType >= 0) {
                fourOsc->state.setProperty("filterType", filterType, nullptr);
                filterTypeApplied = true;
            }
        }
        if (update.voiceMode.isNotEmpty()) {
            const int voiceMode = voiceModeNameToInt(update.voiceMode);
            if (voiceMode >= 0) {
                fourOsc->state.setProperty("voiceMode", voiceMode, nullptr);
                voiceModeApplied = true;
            }
        }

        static const std::map<juce::String, juce::Identifier> effectProperties = {
            {"distortion", juce::Identifier("distortionOn")},
            {"reverb", juce::Identifier("reverbOn")},
            {"delay", juce::Identifier("delayOn")},
            {"chorus", juce::Identifier("chorusOn")},
        };
        for (const auto& [name, enabled] : update.effects) {
            const auto found = effectProperties.find(name);
            if (found == effectProperties.end())
                continue;
            fourOsc->state.setProperty(found->second, enabled, nullptr);
            ++effectsToggled;
        }
    }

    if (bridge != nullptr)
        bridge->getPluginManager().capturePluginState(path);

    if (update.name.isNotEmpty()) {
        auto suggestedName = update.name;
        if (update.category.isNotEmpty())
            suggestedName = update.category + "/" + suggestedName;
        PresetManager::getInstance().setSuggestedPresetName(device->id, suggestedName);
    }

    juce::String status = "applied " + juce::String(parametersApplied) + " params";
    if (wavesApplied > 0)
        status += ", " + juce::String(wavesApplied) + " waves";
    if (filterTypeApplied)
        status += ", filter " + update.filterType;
    if (voiceModeApplied)
        status += ", voice " + update.voiceMode;
    if (effectsToggled > 0)
        status += ", " + juce::String(effectsToggled) + " fx gates";
    status += " to " + device->name;
    if (parametersSkipped > 0 || wavesSkipped > 0) {
        status += ", skipped";
        if (parametersSkipped > 0)
            status += " " + juce::String(parametersSkipped) + " params";
        if (wavesSkipped > 0)
            status += " " + juce::String(wavesSkipped) + " waves";
    }
    return status;
}

juce::String PluginApiLive::applyFaustSource(const ChainNodePath& path,
                                             const juce::String& displayName,
                                             const juce::String& source, bool verified) {
    auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    if (device == nullptr ||
        (!device->pluginId.equalsIgnoreCase(daw::audio::FaustPlugin::xmlTypeName) &&
         !device->pluginId.equalsIgnoreCase(daw::audio::FaustInstrumentPlugin::xmlTypeName)))
        return "(target device is not a Faust plugin)";

    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* faust = dynamic_cast<daw::audio::IFaustEditorModel*>(plugin.get());
    if (faust == nullptr)
        return "(could not resolve live Faust plugin)";

    if (!verified) {
        faust->stageSourceForEditing(displayName, source);
        return "generated \"" + displayName +
               "\" - open the editor to compile (Faust MCP disabled)";
    }

    juce::String error;
    if (!faust->loadDspSource(displayName, source, error))
        return "compile error: " + error;

    bridge->getPluginManager().refreshDeviceParameters(path);
    bridge->getPluginManager().capturePluginState(path);
    return "applied \"" + displayName + "\"";
}

}  // namespace magda
