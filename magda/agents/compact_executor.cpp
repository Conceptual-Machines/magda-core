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

double CompactExecutor::barsToLength(double bars) const {
    double bpm = 120.0;
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine)
        bpm = engine->getTempo();
    return bars * 4.0 * 60.0 / bpm;
}

// ============================================================================
// Main execute
// ============================================================================

bool CompactExecutor::execute(const std::vector<Instruction>& instructions) {
    error_ = {};
    results_.clear();
    currentTrackId_ = -1;
    currentClipId_ = -1;
    clearActiveSelection();

    // Inherit selected track/clip from UI context
    auto& sm = SelectionManager::getInstance();
    auto selectedTrack = sm.getSelectedTrack();

    // Ignore master track as implicit target — it's not a user track
    if (selectedTrack != INVALID_TRACK_ID && selectedTrack != MASTER_TRACK_ID)
        currentTrackId_ = selectedTrack;

    // Single clip selection
    auto selectedClip = sm.getSelectedClip();
    if (selectedClip != INVALID_CLIP_ID) {
        currentClipId_ = selectedClip;
        auto* clipInfo = ClipManager::getInstance().getClip(selectedClip);
        if (clipInfo && clipInfo->trackId != INVALID_TRACK_ID)
            currentTrackId_ = clipInfo->trackId;
    }

    // Multi-clip selection → populate selectedClips_ so SET/DEL apply to all
    auto& uiClips = sm.getSelectedClips();
    if (!uiClips.empty()) {
        selectedClips_.insert(uiClips.begin(), uiClips.end());
        // Derive track from first clip if no track selected
        if (currentTrackId_ < 0) {
            for (auto cid : uiClips) {
                auto* clipInfo = ClipManager::getInstance().getClip(cid);
                if (clipInfo && clipInfo->trackId != INVALID_TRACK_ID) {
                    currentTrackId_ = clipInfo->trackId;
                    break;
                }
            }
        }
    }

    DBG("CompactExecutor: currentTrack=" + juce::String(currentTrackId_) +
        " currentClip=" + juce::String(currentClipId_) +
        " selectedClips=" + juce::String(static_cast<int>(selectedClips_.size())));

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
            case OpCode::Select:
                ok = executeSelect(std::get<SelectOp>(inst.payload));
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
        FxOp fxOp;
        fxOp.fxName = op.fxAlias;

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

        fxOp.target.implicit = true;
        if (!executeFx(fxOp)) {
            results_.add("[!] Could not add FX '" + op.fxAlias + "': " + error_);
            error_ = {};
        }

        return true;
    }

    auto trackId = tm.createTrack(op.name, TrackType::Audio);
    currentTrackId_ = trackId;
    results_.add("Created track '" + op.name + "'");
    return true;
}

bool CompactExecutor::executeDel(const DelOp& op) {
    // If active selection from SELECT, delete all selected items
    if (op.target.isImplicit() && hasActiveSelection()) {
        auto& tm = TrackManager::getInstance();
        auto& cm = ClipManager::getInstance();
        int count = 0;

        if (!selectedClips_.empty()) {
            for (auto clipId : selectedClips_) {
                cm.deleteClip(clipId);
                count++;
            }
            results_.add("Deleted " + juce::String(count) + " clip(s)");
        }
        if (!selectedTracks_.empty()) {
            for (auto trackId : selectedTracks_) {
                tm.deleteTrack(trackId);
                count++;
            }
            results_.add("Deleted " + juce::String(static_cast<int>(selectedTracks_.size())) +
                         " track(s)");
        }
        clearActiveSelection();
        return true;
    }

    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;
    TrackManager::getInstance().deleteTrack(trackId);
    results_.add("Deleted track");
    return true;
}

bool CompactExecutor::executeMute(const MuteOp& op) {
    auto& tm = TrackManager::getInstance();

    // If active track selection from SELECT, mute all selected
    if (!selectedTracks_.empty()) {
        for (auto trackId : selectedTracks_)
            tm.setTrackMuted(trackId, true);
        results_.add("Muted " + juce::String(static_cast<int>(selectedTracks_.size())) +
                     " track(s)");
        return true;
    }

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

    // If active track selection from SELECT, solo all selected
    if (!selectedTracks_.empty()) {
        for (auto trackId : selectedTracks_)
            tm.setTrackSoloed(trackId, true);
        results_.add("Soloed " + juce::String(static_cast<int>(selectedTracks_.size())) +
                     " track(s)");
        return true;
    }

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

void CompactExecutor::applySetProps(int trackId, const juce::StringPairArray& props) {
    auto& tm = TrackManager::getInstance();
    for (const auto& key : props.getAllKeys()) {
        auto val = props.getValue(key, "");
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
}

bool CompactExecutor::executeSet(const SetOp& op) {
    // If active track selection from SELECT, apply to all
    if (op.target.isImplicit() && !selectedTracks_.empty()) {
        for (auto trackId : selectedTracks_)
            applySetProps(trackId, op.props);
        results_.add("Set properties on " + juce::String(static_cast<int>(selectedTracks_.size())) +
                     " track(s)");
        return true;
    }

    // If active clip selection, apply track props to each clip's parent track
    // and apply clip-specific props (name) to the clips
    if (op.target.isImplicit() && !selectedClips_.empty()) {
        auto& cm = ClipManager::getInstance();
        int count = 0;
        for (auto clipId : selectedClips_) {
            auto* clip = cm.getClip(clipId);
            if (!clip)
                continue;
            // Clip-level property: name
            for (const auto& key : op.props.getAllKeys()) {
                if (key == "name")
                    cm.setClipName(clipId, op.props.getValue(key, ""));
            }
            count++;
        }
        results_.add("Set properties on " + juce::String(count) + " clip(s)");
        return true;
    }

    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;

    currentTrackId_ = trackId;
    applySetProps(trackId, op.props);
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

    if (op.name.isNotEmpty()) {
        cm.setClipName(clipId, op.name);
        results_.add("Created clip '" + op.name + "' at bar " + juce::String(op.bar, 0) +
                     ", length " + juce::String(op.lengthBars, 0) + " bars");
    } else {
        results_.add("Created clip at bar " + juce::String(op.bar, 0) + ", length " +
                     juce::String(op.lengthBars, 0) + " bars");
    }
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

bool CompactExecutor::executeSelect(const SelectOp& op) {
    auto& sm = SelectionManager::getInstance();

    if (op.target == SelectOp::Target::Tracks) {
        auto& tm = TrackManager::getInstance();
        std::unordered_set<TrackId> matches;

        for (const auto& track : tm.getTracks()) {
            if (op.field.isEmpty()) {
                // No predicate → select all
                matches.insert(track.id);
                continue;
            }

            bool match = false;
            if (op.field == "name") {
                auto trackName = track.name.toLowerCase();
                auto val = op.value.toLowerCase();
                if (op.op == "=")
                    match = trackName == val;
                else if (op.op == "!=")
                    match = trackName != val;
                else if (op.op == "~")
                    match = trackName.contains(val);
            } else if (op.field == "mute" || op.field == "muted") {
                bool muted = track.muted;
                bool val = op.value == "true" || op.value == "1";
                match = (op.op == "=") ? (muted == val) : (muted != val);
            } else if (op.field == "solo" || op.field == "soloed") {
                bool soloed = track.soloed;
                bool val = op.value == "true" || op.value == "1";
                match = (op.op == "=") ? (soloed == val) : (soloed != val);
            }

            if (match)
                matches.insert(track.id);
        }

        selectedTracks_ = matches;
        selectedClips_.clear();
        sm.selectTracks(matches);
        results_.add("Selected " + juce::String(static_cast<int>(matches.size())) + " track(s)");
        return true;
    }

    // SELECT CLIPS
    auto& cm = ClipManager::getInstance();
    std::unordered_set<ClipId> matches;

    for (const auto& clip : cm.getArrangementClips()) {
        if (op.field.isEmpty()) {
            matches.insert(clip.id);
            continue;
        }

        bool match = false;
        double numVal = op.value.getDoubleValue();

        if (op.field == "length" || op.field == "len") {
            // Compare clip length in bars
            double clipLenBars = clip.length / barsToLength(1.0);
            if (op.op == "<")
                match = clipLenBars < numVal;
            else if (op.op == ">")
                match = clipLenBars > numVal;
            else if (op.op == "<=")
                match = clipLenBars <= numVal;
            else if (op.op == ">=")
                match = clipLenBars >= numVal;
            else if (op.op == "=")
                match = std::abs(clipLenBars - numVal) < 0.01;
            else if (op.op == "!=")
                match = std::abs(clipLenBars - numVal) >= 0.01;
        } else if (op.field == "bar" || op.field == "start") {
            // Compare clip start position in bars (1-based)
            double clipBar = clip.startTime / barsToLength(1.0) + 1.0;
            if (op.op == "<")
                match = clipBar < numVal;
            else if (op.op == ">")
                match = clipBar > numVal;
            else if (op.op == "<=")
                match = clipBar <= numVal;
            else if (op.op == ">=")
                match = clipBar >= numVal;
            else if (op.op == "=")
                match = std::abs(clipBar - numVal) < 0.01;
        } else if (op.field == "track") {
            // Filter by parent track name
            auto& tm = TrackManager::getInstance();
            for (const auto& track : tm.getTracks()) {
                if (track.id == clip.trackId) {
                    auto trackName = track.name.toLowerCase();
                    auto val = op.value.toLowerCase();
                    if (op.op == "=")
                        match = trackName == val;
                    else if (op.op == "!=")
                        match = trackName != val;
                    else if (op.op == "~")
                        match = trackName.contains(val);
                    break;
                }
            }
        } else if (op.field == "name") {
            auto clipName = clip.name.toLowerCase();
            auto val = op.value.toLowerCase();
            if (op.op == "=")
                match = clipName == val;
            else if (op.op == "!=")
                match = clipName != val;
            else if (op.op == "~")
                match = clipName.contains(val);
        }

        if (match)
            matches.insert(clip.id);
    }

    selectedClips_ = matches;
    selectedTracks_.clear();
    sm.selectClips(matches);
    results_.add("Selected " + juce::String(static_cast<int>(matches.size())) + " clip(s)");
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
