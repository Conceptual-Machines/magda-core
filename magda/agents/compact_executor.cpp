#include "compact_executor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../daw/core/ClipManager.hpp"
#include "../daw/core/DeviceInfo.hpp"
#include "../daw/core/MidiNoteCommands.hpp"
#include "../daw/core/PluginAlias.hpp"
#include "../daw/core/SelectionManager.hpp"
#include "../daw/core/TrackManager.hpp"
#include "../daw/core/TrackTypes.hpp"
#include "../daw/core/UndoManager.hpp"
#include "../daw/engine/AudioEngine.hpp"
#include "../daw/engine/TracktionEngineWrapper.hpp"
#include "music_helpers.hpp"

namespace magda {

// ============================================================================
// Helpers
// ============================================================================

int CompactExecutor::findTrackByName(const juce::String& name) const {
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks())
        if (track.name.equalsIgnoreCase(name))
            return track.id;
    return -1;
}

int CompactExecutor::resolveTrackRef(const TrackRef& ref) {
    if (ref.isImplicit()) {
        if (currentTrackId_ < 0) {
            error_ = "No current track context (use TRACK first or specify a ref)";
            return -1;
        }
        return currentTrackId_;
    }

    auto& tm = TrackManager::getInstance();
    if (ref.isById()) {
        int index = ref.id - 1;
        if (index < 0 || index >= tm.getNumTracks()) {
            error_ = "Track " + juce::String(ref.id) + " not found";
            return -1;
        }
        return tm.getTracks()[static_cast<size_t>(index)].id;
    }
    int id = findTrackByName(ref.name);
    if (id < 0)
        error_ = "Track '" + ref.name + "' not found";
    return id;
}

double CompactExecutor::barsToTime(double bar) const {
    double bpm = 120.0;
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine)
        bpm = engine->getTempo();
    return (bar - 1.0) * 4.0 * 60.0 / bpm;
}

// ============================================================================
// Main execute
// ============================================================================

bool CompactExecutor::execute(const std::vector<Instruction>& instructions) {
    error_ = {};
    results_.clear();
    currentTrackId_ = -1;
    currentClipId_ = -1;

    // Inherit selected clip from UI
    auto& sm = SelectionManager::getInstance();
    auto selectedClip = sm.getSelectedClip();
    if (selectedClip != INVALID_CLIP_ID)
        currentClipId_ = selectedClip;

    int succeeded = 0;
    int failed = 0;

    for (const auto& inst : instructions) {
        bool ok = false;

        switch (inst.opcode) {
            case OpCode::Track:
                ok = executeTrack(std::get<TrackOp>(inst.payload));
                break;
            case OpCode::Del:
                ok = executeDel(std::get<DelOp>(inst.payload));
                break;
            case OpCode::Mute:
                ok = executeMute(std::get<MuteOp>(inst.payload));
                break;
            case OpCode::Solo:
                ok = executeSolo(std::get<SoloOp>(inst.payload));
                break;
            case OpCode::Set:
                ok = executeSet(std::get<SetOp>(inst.payload));
                break;
            case OpCode::Clip:
                ok = executeClip(std::get<ClipOp>(inst.payload));
                break;
            case OpCode::Fx:
                ok = executeFx(std::get<FxOp>(inst.payload));
                break;
            case OpCode::Arp:
                ok = executeArp(std::get<ArpOp>(inst.payload));
                break;
            case OpCode::Chord:
                ok = executeChord(std::get<ChordOp>(inst.payload));
                break;
            case OpCode::Note:
                ok = executeNote(std::get<NoteOp>(inst.payload));
                break;
        }

        if (ok) {
            succeeded++;
        } else {
            results_.add("[!] " + error_);
            failed++;
        }
    }

    if (succeeded == 0 && failed > 0) {
        error_ = "All " + juce::String(failed) + " instruction(s) failed";
        return false;
    }

    return true;
}

// ============================================================================
// Instruction executors
// ============================================================================

bool CompactExecutor::executeTrack(const TrackOp& op) {
    auto& tm = TrackManager::getInstance();

    // TRACK FX <alias> — resolve plugin, name track after it, add plugin
    if (op.fxAlias.isNotEmpty()) {
        // Try to resolve plugin from alias
        FxOp fxOp;
        fxOp.fxName = op.fxAlias;

        // We need to find the plugin name before creating the track
        juce::String trackName = op.fxAlias;  // fallback to alias

        auto* engine = tm.getAudioEngine();
        if (engine) {
            auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(engine);
            if (teWrapper) {
                const auto& knownPlugins = teWrapper->getKnownPluginList();
                for (const auto& desc : knownPlugins.getTypes()) {
                    auto alias = pluginNameToAlias(desc.name);
                    if (desc.name.equalsIgnoreCase(op.fxAlias) ||
                        alias.equalsIgnoreCase(op.fxAlias)) {
                        trackName = desc.name;
                        break;
                    }
                }
            }
        }

        auto trackId = tm.createTrack(op.name.isEmpty() ? trackName : op.name, TrackType::Audio);
        currentTrackId_ = trackId;
        results_.add("Created track '" + trackName + "'");

        // Now add the FX via the existing executeFx path
        fxOp.target.implicit = true;
        if (!executeFx(fxOp))
            return false;

        return true;
    }

    auto trackId = tm.createTrack(op.name, TrackType::Audio);
    currentTrackId_ = trackId;
    results_.add("Created track '" + op.name + "'");
    return true;
}

bool CompactExecutor::executeDel(const DelOp& op) {
    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;
    TrackManager::getInstance().deleteTrack(trackId);
    results_.add("Deleted track");
    return true;
}

bool CompactExecutor::executeMute(const MuteOp& op) {
    auto& tm = TrackManager::getInstance();
    int count = 0;
    for (const auto& track : tm.getTracks()) {
        if (track.name.equalsIgnoreCase(op.name)) {
            tm.setTrackMuted(track.id, true);
            count++;
        }
    }
    results_.add("Muted " + juce::String(count) + " track(s)");
    return true;
}

bool CompactExecutor::executeSolo(const SoloOp& op) {
    auto& tm = TrackManager::getInstance();
    int count = 0;
    for (const auto& track : tm.getTracks()) {
        if (track.name.equalsIgnoreCase(op.name)) {
            tm.setTrackSoloed(track.id, true);
            count++;
        }
    }
    results_.add("Soloed " + juce::String(count) + " track(s)");
    return true;
}

bool CompactExecutor::executeSet(const SetOp& op) {
    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;

    currentTrackId_ = trackId;

    auto& tm = TrackManager::getInstance();

    for (const auto& key : op.props.getAllKeys()) {
        auto val = op.props.getValue(key, "");

        if (key == "vol" || key == "volume_db") {
            double db = val.getDoubleValue();
            float vol = static_cast<float>(std::pow(10.0, db / 20.0));
            tm.setTrackVolume(trackId, vol);
        } else if (key == "pan") {
            tm.setTrackPan(trackId, val.getFloatValue());
        } else if (key == "mute") {
            tm.setTrackMuted(trackId, val == "true" || val == "1");
        } else if (key == "solo") {
            tm.setTrackSoloed(trackId, val == "true" || val == "1");
        } else if (key == "name") {
            tm.setTrackName(trackId, val);
        }
    }

    results_.add("Set track properties");
    return true;
}

bool CompactExecutor::executeClip(const ClipOp& op) {
    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;

    currentTrackId_ = trackId;

    double startTime = barsToTime(op.bar);
    double length = barsToTime(op.bar + op.lengthBars) - startTime;

    auto& cm = ClipManager::getInstance();
    auto clipId = cm.createMidiClip(trackId, startTime, length);

    if (clipId < 0) {
        error_ = "Failed to create clip";
        return false;
    }

    currentClipId_ = clipId;
    results_.add("Created clip at bar " + juce::String(op.bar, 0) + ", length " +
                 juce::String(op.lengthBars, 0) + " bars");
    return true;
}

bool CompactExecutor::executeFx(const FxOp& op) {
    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;

    // Internal plugin alias lookup (mirrors DSL interpreter)
    static const std::map<juce::String, juce::String> internalAliases = {
        {"eq", "eq"},
        {"equaliser", "eq"},
        {"equalizer", "eq"},
        {"compressor", "compressor"},
        {"reverb", "reverb"},
        {"delay", "delay"},
        {"chorus", "chorus"},
        {"phaser", "phaser"},
        {"filter", "lowpass"},
        {"lowpass", "lowpass"},
        {"utility", "utility"},
        {"pitch shift", "pitchshift"},
        {"pitchshift", "pitchshift"},
        {"ir reverb", "impulseresponse"},
        {"impulse response", "impulseresponse"},
    };

    auto lowerName = op.fxName.toLowerCase();
    auto aliasIt = internalAliases.find(lowerName);

    if (aliasIt != internalAliases.end()) {
        DeviceInfo device;
        device.name = op.fxName;
        device.pluginId = aliasIt->second;
        device.format = PluginFormat::Internal;
        device.isInstrument = false;

        auto deviceId = TrackManager::getInstance().addDeviceToTrack(trackId, device);
        if (deviceId == INVALID_DEVICE_ID) {
            error_ = "Failed to add FX '" + op.fxName + "'";
            return false;
        }
        results_.add("Added FX '" + op.fxName + "'");
        return true;
    }

    // External plugin lookup via alias matching
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (!engine) {
        error_ = "Audio engine not available";
        return false;
    }

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(engine);
    if (!teWrapper) {
        error_ = "Plugin scanning not available";
        return false;
    }

    const auto& knownPlugins = teWrapper->getKnownPluginList();
    const juce::PluginDescription* bestMatch = nullptr;

    for (const auto& desc : knownPlugins.getTypes()) {
        auto alias = pluginNameToAlias(desc.name);
        if (desc.name.equalsIgnoreCase(op.fxName) || alias.equalsIgnoreCase(op.fxName)) {
            bestMatch = &desc;
            break;
        }
    }

    if (!bestMatch) {
        error_ = "Plugin '" + op.fxName + "' not found";
        return false;
    }

    DeviceInfo device;
    device.name = bestMatch->name;
    device.pluginId = bestMatch->createIdentifierString();
    device.manufacturer = bestMatch->manufacturerName;
    device.uniqueId = bestMatch->createIdentifierString();
    device.fileOrIdentifier = bestMatch->fileOrIdentifier;
    device.isInstrument = bestMatch->isInstrument;

    juce::String matchedFormat = bestMatch->pluginFormatName;
    juce::String matchedName = bestMatch->name;
    bestMatch = nullptr;

    if (matchedFormat == "VST3")
        device.format = PluginFormat::VST3;
    else if (matchedFormat == "AudioUnit" || matchedFormat == "AU")
        device.format = PluginFormat::AU;
    else if (matchedFormat == "VST")
        device.format = PluginFormat::VST;
    else
        device.format = PluginFormat::VST3;

    auto deviceId = TrackManager::getInstance().addDeviceToTrack(trackId, device);
    if (deviceId == INVALID_DEVICE_ID) {
        error_ = "Failed to add plugin '" + op.fxName + "'";
        return false;
    }

    results_.add("Added plugin '" + matchedName + "' by " + device.manufacturer);
    return true;
}

bool CompactExecutor::executeArp(const ArpOp& op) {
    if (currentClipId_ < 0) {
        error_ = "No clip context for ARP";
        return false;
    }

    std::vector<int> midiNotes;
    juce::String chordError;
    if (!music::resolveChordNotes(op.root.toStdString(), op.quality.toStdString(), 0, midiNotes,
                                  chordError)) {
        error_ = chordError;
        return false;
    }

    // Sort ascending for pattern
    std::sort(midiNotes.begin(), midiNotes.end());

    // Default pattern: up
    std::vector<int> ordered = midiNotes;

    int velocity = 100;
    double noteLength = op.step;

    // Determine fill boundary
    bool fill = op.beats > 0;
    double fillBeats = 0.0;
    if (fill) {
        fillBeats = op.beat + op.beats;
    }

    // Build notes
    std::vector<MidiNote> notes;
    double currentBeat = op.beat;
    size_t idx = 0;
    size_t count = fill ? std::numeric_limits<size_t>::max() : ordered.size();

    while (idx < count) {
        if (fill && currentBeat >= fillBeats)
            break;
        int n = ordered[idx % ordered.size()];
        MidiNote mn;
        mn.noteNumber = n;
        mn.startBeat = currentBeat;
        mn.lengthBeats = noteLength;
        mn.velocity = velocity;
        notes.push_back(mn);
        currentBeat += op.step;
        idx++;
    }

    UndoManager::getInstance().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
        currentClipId_, std::move(notes),
        "Add " + op.quality + " arpeggio at beat " + juce::String(op.beat, 2)));

    results_.add("Added arpeggio " + op.root + " " + op.quality);
    return true;
}

bool CompactExecutor::executeChord(const ChordOp& op) {
    if (currentClipId_ < 0) {
        error_ = "No clip context for CHORD";
        return false;
    }

    std::vector<int> midiNotes;
    juce::String chordError;
    if (!music::resolveChordNotes(op.root.toStdString(), op.quality.toStdString(), 0, midiNotes,
                                  chordError)) {
        error_ = chordError;
        return false;
    }

    int velocity = op.velocity >= 0 ? op.velocity : 100;

    std::vector<MidiNote> notes;
    for (int n : midiNotes) {
        MidiNote mn;
        mn.noteNumber = n;
        mn.startBeat = op.beat;
        mn.lengthBeats = op.length;
        mn.velocity = velocity;
        notes.push_back(mn);
    }

    UndoManager::getInstance().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
        currentClipId_, std::move(notes),
        "Add " + op.quality + " chord at beat " + juce::String(op.beat, 2)));

    results_.add("Added chord " + op.root + " " + op.quality);
    return true;
}

bool CompactExecutor::executeNote(const NoteOp& op) {
    if (currentClipId_ < 0) {
        error_ = "No clip context for NOTE";
        return false;
    }

    int noteNumber = music::parseNoteName(op.pitch.toStdString());
    if (noteNumber < 0 || noteNumber > 127) {
        error_ = "Invalid pitch: " + op.pitch;
        return false;
    }

    int velocity = op.velocity >= 0 ? op.velocity : 100;

    UndoManager::getInstance().executeCommand(std::make_unique<AddMidiNoteCommand>(
        currentClipId_, op.beat, noteNumber, op.length, velocity));

    results_.add("Added note " + op.pitch);
    return true;
}

}  // namespace magda
