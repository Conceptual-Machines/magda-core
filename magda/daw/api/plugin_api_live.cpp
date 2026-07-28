#include "plugin_api_live.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <set>

#include "../audio/AudioBridge.hpp"
#include "../audio/plugins/DrumGridPlugin.hpp"
#include "../audio/plugins/IFaustEditorModel.hpp"
#include "../audio/plugins/PolyStepSequencerPlugin.hpp"
#include "../audio/plugins/StepSequencerPlugin.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/PresetManager.hpp"
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
    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* sequencer = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get());
    if (sequencer == nullptr)
        return std::nullopt;

    return SequencerRuntimeContext{
        .numSteps = sequencer->numSteps.get(),
        .rate = sequencer->rate.get(),
        .swing = sequencer->swing.get(),
        .gateLength = sequencer->gateLength.get(),
    };
}

std::optional<SequencerRuntimeContext> PluginApiLive::getPolySequencerContext(
    const ChainNodePath& path) const {
    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* sequencer = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get());
    if (sequencer == nullptr)
        return std::nullopt;

    SequencerRuntimeContext context{
        .numSteps = sequencer->numSteps.get(),
        .rate = sequencer->rate.get(),
        .swing = sequencer->swing.get(),
        .gateLength = sequencer->gateLength.get(),
        .viewMode = sequencer->viewMode.get(),
    };

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

    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* sequencer = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get());
    if (sequencer == nullptr)
        return "(could not resolve live Step Sequencer)";

    auto* undoManager = sequencer->getUndoManager();
    if (pattern.numSteps >= 1)
        sequencer->state.setProperty("seqNumSteps", pattern.numSteps, undoManager);
    if (pattern.rate >= 0)
        sequencer->state.setProperty("seqRate", pattern.rate, undoManager);
    if (pattern.swing >= 0.0f)
        sequencer->state.setProperty("seqSwing", pattern.swing, undoManager);
    if (pattern.gateLength >= 0.0f)
        sequencer->state.setProperty("seqGateLength", pattern.gateLength, undoManager);

    const int numStepsToClear =
        juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS,
                     pattern.numSteps >= 1 ? pattern.numSteps : sequencer->numSteps.get());
    for (int i = 0; i < numStepsToClear; ++i)
        sequencer->clearStep(i);

    int stepsWritten = 0;
    for (const auto& step : pattern.steps) {
        if (step.index < 0 || step.index >= daw::audio::StepSequencerPlugin::MAX_STEPS)
            continue;
        sequencer->setStepGate(step.index, step.gate);
        if (step.noteNumber != 60)
            sequencer->setStepNote(step.index, step.noteNumber);
        if (step.octaveShift != 0)
            sequencer->setStepOctaveShift(step.index, step.octaveShift);
        if (step.accent)
            sequencer->setStepAccent(step.index, true);
        if (step.glide)
            sequencer->setStepGlide(step.index, true);
        if (step.tie)
            sequencer->setStepTie(step.index, true);
        ++stepsWritten;
    }

    bridge->getPluginManager().capturePluginState(path);
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

    auto* bridge = getAudioBridge();
    auto plugin = bridge != nullptr ? bridge->getPlugin(path) : nullptr;
    auto* sequencer = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get());
    if (sequencer == nullptr)
        return "(could not resolve live Poly Step Sequencer)";

    auto* undoManager = sequencer->getUndoManager();
    if (pattern.numSteps >= 1)
        sequencer->state.setProperty("seqNumSteps", pattern.numSteps, undoManager);
    if (pattern.rate >= 0)
        sequencer->state.setProperty("seqRate", pattern.rate, undoManager);
    if (pattern.swing >= 0.0f)
        sequencer->state.setProperty("seqSwing", pattern.swing, undoManager);
    if (pattern.gateLength >= 0.0f)
        sequencer->state.setProperty("seqGateLength", pattern.gateLength, undoManager);

    const int numStepsToClear =
        juce::jlimit(1, daw::audio::PolyStepSequencerPlugin::MAX_STEPS,
                     pattern.numSteps >= 1 ? pattern.numSteps : sequencer->numSteps.get());
    for (int i = 0; i < numStepsToClear; ++i)
        sequencer->clearStep(i);

    int stepsWritten = 0;
    int notesWritten = 0;
    for (const auto& step : pattern.steps) {
        if (step.index < 0 || step.index >= daw::audio::PolyStepSequencerPlugin::MAX_STEPS)
            continue;
        sequencer->setStepGate(step.index, step.gate);
        if (step.tie)
            sequencer->setStepTie(step.index, true);
        if (step.probability < 1.0f)
            sequencer->setStepProbability(step.index, step.probability);
        if (step.velocity != 100)
            sequencer->setStepVelocity(step.index, step.velocity);
        for (const auto& note : step.notes) {
            sequencer->addStepNote(step.index, note.noteNumber, note.velocityOverride);
            ++notesWritten;
        }
        ++stepsWritten;
    }

    bridge->getPluginManager().capturePluginState(path);
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
    if (device == nullptr || (!device->pluginId.equalsIgnoreCase("faust") &&
                              !device->pluginId.equalsIgnoreCase("faustinstrument")))
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
    if (!faust->loadDspSource(displayName, source, error, daw::audio::FaustCustomViewKind::None))
        return "compile error: " + error;

    bridge->getPluginManager().refreshDeviceParameters(path);
    bridge->getPluginManager().capturePluginState(path);
    return "applied \"" + displayName + "\"";
}

}  // namespace magda
