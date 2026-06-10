#include "instruction_executor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../daw/api/clip_api.hpp"
#include "../daw/api/magda_api.hpp"
#include "../daw/api/project_api.hpp"
#include "../daw/api/selection_api.hpp"
#include "../daw/api/track_api.hpp"
#include "../daw/api/transport_api.hpp"
#include "../daw/api/undo_api.hpp"
#include "../daw/core/DeviceInfo.hpp"
#include "../daw/core/MidiNoteCommands.hpp"
#include "../daw/core/PluginAlias.hpp"
#include "../daw/core/TrackCommands.hpp"
#include "../daw/core/TrackPropertyCommands.hpp"
#include "../daw/core/TrackTypes.hpp"
#include "../daw/engine/AudioEngine.hpp"
#include "../daw/engine/TracktionEngineWrapper.hpp"
#include "internal_plugins.hpp"
#include "music_helpers.hpp"

namespace magda {

namespace {
/// RAII: groups every command enqueued during one execute() into a single undo
/// step, so an agent turn (incl. a master-selected fan-out) is one undo.
struct CompoundScope {
    UndoApi& undo;
    explicit CompoundScope(UndoApi& u, const juce::String& desc) : undo(u) {
        undo.beginCompound(desc);
    }
    ~CompoundScope() {
        undo.endCompound();
    }
    CompoundScope(const CompoundScope&) = delete;
    CompoundScope& operator=(const CompoundScope&) = delete;
};
}  // namespace

// ============================================================================
// Helpers
// ============================================================================

int InstructionExecutor::findTrackByName(const juce::String& name) const {
    for (const auto& track : api_.tracks().getTracks())
        if (track.name.equalsIgnoreCase(name))
            return track.id;
    return -1;
}

int InstructionExecutor::resolveTrackRef(const TrackRef& ref) {
    if (ref.isImplicit()) {
        if (currentTrackId_ < 0) {
            error_ = "No current track context (use TRACK first or specify a ref)";
            return -1;
        }
        return currentTrackId_;
    }

    auto& tm = api_.tracks();
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

double InstructionExecutor::barsToTime(double bar) const {
    double bpm = 120.0;
    auto* engine = api_.tracks().getAudioEngine();
    if (engine)
        bpm = engine->getTempo();
    return (bar - 1.0) * 4.0 * 60.0 / bpm;
}

double InstructionExecutor::barsToLength(double bars) const {
    double bpm = 120.0;
    auto* engine = api_.tracks().getAudioEngine();
    if (engine)
        bpm = engine->getTempo();
    return bars * 4.0 * 60.0 / bpm;
}

double InstructionExecutor::barsToBeats(double bars) const {
    // Use the project's time-signature numerator. Hard-coding 4 here was
    // the source of the seconds↔beats round-trip drift under non-4/4 sigs.
    int beatsPerBar = api_.project().getCurrentProjectInfo().timeSignatureNumerator;
    if (beatsPerBar <= 0)
        beatsPerBar = 4;
    return bars * static_cast<double>(beatsPerBar);
}

double InstructionExecutor::beatsToBar(double beats) const {
    const double beatsPerBar = barsToBeats(1.0);
    if (beatsPerBar <= 0.0)
        return 1.0;
    return (beats / beatsPerBar) + 1.0;
}

double InstructionExecutor::findNonOverlappingClipStartBeats(TrackId trackId,
                                                             double desiredStartBeats,
                                                             double lengthBeats) const {
    constexpr double epsilon = 1.0e-6;
    double candidate = std::max(0.0, desiredStartBeats);
    if (lengthBeats <= 0.0)
        return candidate;

    struct Range {
        double start;
        double end;
    };
    std::vector<Range> ranges;

    auto& clips = api_.clips();
    for (auto clipId : clips.getClipsOnTrack(trackId)) {
        const auto* clip = clips.getClip(clipId);
        if (!clip || clip->view != ClipView::Arrangement)
            continue;

        const double start = clip->placement.startBeat;
        const double end = clip->placement.endBeat();
        if (end > start)
            ranges.push_back({start, end});
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const auto& a, const auto& b) { return a.start < b.start; });

    for (const auto& range : ranges) {
        if (range.end <= candidate + epsilon)
            continue;
        if (candidate + lengthBeats <= range.start + epsilon)
            break;
        candidate = range.end;
    }

    return candidate;
}

// ============================================================================
// Main execute
// ============================================================================

bool InstructionExecutor::execute(const std::vector<Instruction>& instructions) {
    error_ = {};
    results_.clear();
    currentTrackId_ = -1;
    currentClipId_ = -1;
    autoCreatedClip_ = false;
    pendingContentEndBeats_ = 0.0;
    clearActiveSelection();

    // Pre-scan instructions for the latest beat reached by any note-producing
    // op, so autoCreateClip() can size the new clip to actually fit the
    // music. Without this an LLM that emits an 8-chord 64-beat progression
    // gets stuffed into a hardcoded 4-bar clip and loops the first bar.
    for (const auto& inst : instructions) {
        switch (inst.opcode) {
            case OpCode::Note: {
                const auto& n = std::get<NoteOp>(inst.payload);
                pendingContentEndBeats_ = std::max(pendingContentEndBeats_, n.beat + n.length);
                break;
            }
            case OpCode::Chord: {
                const auto& c = std::get<ChordOp>(inst.payload);
                pendingContentEndBeats_ = std::max(pendingContentEndBeats_, c.beat + c.length);
                break;
            }
            case OpCode::Arp: {
                const auto& a = std::get<ArpOp>(inst.payload);
                double span = a.beats;
                if (span <= 0.0) {
                    std::vector<int> midiNotes;
                    juce::String chordError;
                    if (music::resolveChordNotes(a.root.toStdString(), a.quality.toStdString(),
                                                 a.inversion, midiNotes, chordError)) {
                        auto pattern = a.pattern.trim().toLowerCase();
                        size_t noteCount = midiNotes.size();
                        if ((pattern == "updown" || pattern == "upanddown" ||
                             pattern == "up_down") &&
                            midiNotes.size() > 2) {
                            noteCount += midiNotes.size() - 2;
                        }
                        span = static_cast<double>(noteCount) * a.step;
                    } else {
                        span = a.step;
                    }
                }
                pendingContentEndBeats_ = std::max(pendingContentEndBeats_, a.beat + span);
                break;
            }
            case OpCode::Hit: {
                const auto& h = std::get<HitOp>(inst.payload);
                pendingContentEndBeats_ = std::max(pendingContentEndBeats_, h.beat + h.length);
                break;
            }
            default:
                break;
        }
    }

    // Inherit only the selected track from UI context — intentionally do NOT
    // inherit the selected clip. The music agent should always produce a fresh
    // clip unless explicitly seeded by a prior step in the same turn (e.g. the
    // command agent's clip.new). Using SelectionManager for agent-to-agent
    // handoff would silently fill whichever clip the user happened to have
    // selected in the UI.
    auto& sm = api_.selection();
    auto selectedTrack = sm.getSelectedTrack();

    // Ignore master track as implicit target — it's not a user track
    if (selectedTrack != INVALID_TRACK_ID && selectedTrack != MASTER_TRACK_ID) {
        currentTrackId_ = selectedTrack;
    } else if (selectedTrack == MASTER_TRACK_ID) {
        // Master selected = fan implicit-target ops out across every track as a
        // single action. Reuses the SELECT machinery: MUTE/SOLO/SET/FX with no
        // ref apply to each selected track. (getTracks() excludes the master.)
        for (const auto& track : api_.tracks().getTracks())
            selectedTracks_.insert(track.id);
    }

    // Seeded clip from the command agent (BOTH-intent handoff).
    // Validate before adopting: a stale/deleted ID would otherwise suppress
    // auto-creation and cause note commands to silently no-op on a missing
    // clip while still reporting success.
    if (seedClipId_ >= 0) {
        auto* clipInfo = api_.clips().getClip(seedClipId_);
        if (clipInfo) {
            currentClipId_ = seedClipId_;
            if (clipInfo->trackId != INVALID_TRACK_ID)
                currentTrackId_ = clipInfo->trackId;
        } else {
            DBG("InstructionExecutor: ignoring stale seedClipId=" + juce::String(seedClipId_) +
                " (clip no longer exists)");
            seedClipId_ = -1;
        }
    }

    // Multi-clip selection → populate selectedClips_ so SET/DEL apply to all
    auto& uiClips = sm.getSelectedClips();
    if (!uiClips.empty()) {
        selectedClips_.insert(uiClips.begin(), uiClips.end());
        // Derive track from first clip if no track selected
        if (currentTrackId_ < 0) {
            for (auto cid : uiClips) {
                auto* clipInfo = api_.clips().getClip(cid);
                if (clipInfo && clipInfo->trackId != INVALID_TRACK_ID) {
                    currentTrackId_ = clipInfo->trackId;
                    break;
                }
            }
        }
    }

    DBG("InstructionExecutor: currentTrack=" + juce::String(currentTrackId_) +
        " currentClip=" + juce::String(currentClipId_) +
        " selectedClips=" + juce::String(static_cast<int>(selectedClips_.size())));

    int succeeded = 0;
    int failed = 0;

    // One undo step for the whole turn. Property/track commands enqueued below
    // are collected by the UndoManager and wrapped into a single CompoundCommand.
    CompoundScope compound(api_.undo(), "AI Assistant");

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
            case OpCode::Hit:
                ok = executeHit(std::get<HitOp>(inst.payload));
                break;
        }

        if (ok) {
            succeeded++;
        } else {
            DBG("InstructionExecutor: instruction " + juce::String(succeeded + failed) +
                " (opcode=" + juce::String(static_cast<int>(inst.opcode)) + ") FAILED: " + error_);
            results_.add("[!] " + error_);
            failed++;
        }
    }

    DBG("InstructionExecutor: execute done - succeeded=" + juce::String(succeeded) +
        " failed=" + juce::String(failed) + " currentClip=" + juce::String(currentClipId_));

    if (succeeded == 0 && failed > 0) {
        error_ = "All " + juce::String(failed) + " instruction(s) failed";
        return false;
    }

    return true;
}

bool InstructionExecutor::autoCreateClip() {
    if (currentTrackId_ < 0) {
        error_ = "No track context - use TRACK first or select a track";
        DBG("InstructionExecutor::autoCreateClip FAIL: " + error_);
        return false;
    }

    // Create a clip at the edit/playhead position and size it exactly to the
    // generated content. Playback/scheduling bugs must be fixed in the
    // scheduler; the arranger should not pad the container to compensate.
    double startBeats = std::max(0.0, api_.transport().getPositionBeats());
    double lengthBeats = pendingContentEndBeats_ > 0.0 ? pendingContentEndBeats_ : barsToBeats(1.0);
    startBeats = findNonOverlappingClipStartBeats(currentTrackId_, startBeats, lengthBeats);

    DBG("InstructionExecutor::autoCreateClip creating MIDI clip on track " +
        juce::String(currentTrackId_) + " startBeats=" + juce::String(startBeats, 3) +
        " lenBeats=" + juce::String(lengthBeats, 3));

    auto clipId = api_.clips().createMidiClipBeats(currentTrackId_, startBeats, lengthBeats);
    if (clipId < 0) {
        error_ = "Failed to auto-create clip";
        DBG("InstructionExecutor::autoCreateClip FAIL: createMidiClip returned -1 for track " +
            juce::String(currentTrackId_));
        return false;
    }

    DBG("InstructionExecutor::autoCreateClip OK: clipId=" + juce::String(clipId));

    currentClipId_ = clipId;
    autoCreatedClip_ = true;
    api_.selection().selectClip(clipId);
    results_.add("Created MIDI clip at bar " + juce::String(beatsToBar(startBeats), 2) +
                 ", length " + juce::String(beatsToBar(lengthBeats) - 1.0, 2) + " bars");
    return true;
}

// ============================================================================
// Instruction executors
// ============================================================================

bool InstructionExecutor::executeTrack(const TrackOp& op) {
    auto& tm = api_.tracks();

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

        auto createCmd = std::make_unique<CreateTrackCommand>(
            TrackType::Audio, op.name.isEmpty() ? trackName : op.name);
        auto* createPtr = createCmd.get();
        api_.undo().executeCommand(std::move(createCmd));
        currentTrackId_ = createPtr->getCreatedTrackId();
        results_.add("Created track '" + trackName + "'");

        fxOp.target.implicit = true;
        if (!executeFx(fxOp)) {
            results_.add("[!] Could not add FX '" + op.fxAlias + "': " + error_);
            error_ = {};
        }

        return true;
    }

    auto createCmd = std::make_unique<CreateTrackCommand>(TrackType::Audio, op.name);
    auto* createPtr = createCmd.get();
    api_.undo().executeCommand(std::move(createCmd));
    currentTrackId_ = createPtr->getCreatedTrackId();
    results_.add("Created track '" + op.name + "'");

    return true;
}

bool InstructionExecutor::executeDel(const DelOp& op) {
    // If active selection from SELECT, delete all selected items
    if (op.target.isImplicit() && hasActiveSelection()) {
        auto& cm = api_.clips();
        int count = 0;

        if (!selectedClips_.empty()) {
            for (auto clipId : selectedClips_) {
                cm.deleteClip(clipId);
                count++;
            }
            results_.add("Deleted " + juce::String(count) + " clip(s)");
        }
        if (!selectedTracks_.empty()) {
            for (auto trackId : selectedTracks_)
                api_.undo().executeCommand(std::make_unique<DeleteTrackCommand>(trackId));
            results_.add("Deleted " + juce::String(static_cast<int>(selectedTracks_.size())) +
                         " track(s)");
        }
        clearActiveSelection();
        return true;
    }

    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;
    api_.undo().executeCommand(std::make_unique<DeleteTrackCommand>(trackId));
    results_.add("Deleted track");
    return true;
}

bool InstructionExecutor::executeMute(const MuteOp& op) {
    auto& tm = api_.tracks();

    // If an active track selection from SELECT is present, mute all selected
    // (unless the caller gave an explicit target, in which case honour that).
    if (!selectedTracks_.empty() && op.target.isImplicit()) {
        for (auto trackId : selectedTracks_)
            api_.undo().executeCommand(std::make_unique<SetTrackMuteCommand>(trackId, true));
        results_.add("Muted " + juce::String(static_cast<int>(selectedTracks_.size())) +
                     " track(s)");
        return true;
    }

    // MUTE <name> historically muted ALL tracks whose name matched (the
    // grammar allows casual "mute drums" to mute every drum-named track).
    // Preserve that behaviour when target is by-name only.
    if (!op.target.isImplicit() && !op.target.isById() && op.target.name.isNotEmpty()) {
        int count = 0;
        for (const auto& track : tm.getTracks()) {
            if (track.name.equalsIgnoreCase(op.target.name)) {
                api_.undo().executeCommand(std::make_unique<SetTrackMuteCommand>(track.id, true));
                ++count;
            }
        }
        results_.add("Muted " + juce::String(count) + " track(s)");
        return true;
    }

    // Implicit or by-id: resolve to a single track.
    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;
    api_.undo().executeCommand(std::make_unique<SetTrackMuteCommand>(trackId, true));
    results_.add("Muted 1 track");
    return true;
}

bool InstructionExecutor::executeSolo(const SoloOp& op) {
    auto& tm = api_.tracks();

    if (!selectedTracks_.empty() && op.target.isImplicit()) {
        for (auto trackId : selectedTracks_)
            api_.undo().executeCommand(std::make_unique<SetTrackSoloCommand>(trackId, true));
        results_.add("Soloed " + juce::String(static_cast<int>(selectedTracks_.size())) +
                     " track(s)");
        return true;
    }

    if (!op.target.isImplicit() && !op.target.isById() && op.target.name.isNotEmpty()) {
        int count = 0;
        for (const auto& track : tm.getTracks()) {
            if (track.name.equalsIgnoreCase(op.target.name)) {
                api_.undo().executeCommand(std::make_unique<SetTrackSoloCommand>(track.id, true));
                ++count;
            }
        }
        results_.add("Soloed " + juce::String(count) + " track(s)");
        return true;
    }

    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;
    api_.undo().executeCommand(std::make_unique<SetTrackSoloCommand>(trackId, true));
    results_.add("Soloed 1 track");
    return true;
}

void InstructionExecutor::applySetProps(int trackId, const juce::StringPairArray& props) {
    auto& undo = api_.undo();
    for (const auto& key : props.getAllKeys()) {
        auto val = props.getValue(key, "");
        if (key == "vol" || key == "volume_db") {
            double db = val.getDoubleValue();
            float vol = static_cast<float>(std::pow(10.0, db / 20.0));
            undo.executeCommand(std::make_unique<SetTrackVolumeCommand>(trackId, vol));
        } else if (key == "pan") {
            undo.executeCommand(std::make_unique<SetTrackPanCommand>(trackId, val.getFloatValue()));
        } else if (key == "mute") {
            undo.executeCommand(
                std::make_unique<SetTrackMuteCommand>(trackId, val == "true" || val == "1"));
        } else if (key == "solo") {
            undo.executeCommand(
                std::make_unique<SetTrackSoloCommand>(trackId, val == "true" || val == "1"));
        } else if (key == "name") {
            undo.executeCommand(std::make_unique<SetTrackNameCommand>(trackId, val));
        }
    }
}

bool InstructionExecutor::executeSet(const SetOp& op) {
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
        auto& cm = api_.clips();
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

bool InstructionExecutor::executeClip(const ClipOp& op) {
    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;

    currentTrackId_ = trackId;

    // Beats-authoritative: convert bars to beats once via the project time
    // signature; do NOT round-trip through seconds.
    double startBeats = barsToBeats(op.bar - 1.0);
    double lengthBeats = barsToBeats(op.lengthBars);
    startBeats = findNonOverlappingClipStartBeats(trackId, startBeats, lengthBeats);

    auto& cm = api_.clips();
    auto clipId = cm.createMidiClipBeats(trackId, startBeats, lengthBeats);

    if (clipId < 0) {
        error_ = "Failed to create clip";
        return false;
    }

    currentClipId_ = clipId;
    api_.selection().selectClip(clipId);

    if (op.name.isNotEmpty()) {
        cm.setClipName(clipId, op.name);
        results_.add("Created clip '" + op.name + "' at bar " +
                     juce::String(beatsToBar(startBeats), 0) + ", length " +
                     juce::String(op.lengthBars, 0) + " bars");
    } else {
        results_.add("Created clip at bar " + juce::String(beatsToBar(startBeats), 0) +
                     ", length " + juce::String(op.lengthBars, 0) + " bars");
    }
    return true;
}

bool InstructionExecutor::executeFx(const FxOp& op) {
    // Fan out across an active track selection (e.g. master-selected = all
    // tracks) when the op has no explicit ref. Mirrors MUTE/SOLO/SET.
    if (op.target.isImplicit() && !selectedTracks_.empty()) {
        int count = 0;
        for (auto trackId : selectedTracks_) {
            if (!addFxToTrack(trackId, op.fxName))
                return false;
            count++;
        }
        results_.add("Added '" + op.fxName + "' to " + juce::String(count) + " track(s)");
        return true;
    }

    int trackId = resolveTrackRef(op.target);
    if (trackId < 0)
        return false;
    return addFxToTrack(trackId, op.fxName);
}

bool InstructionExecutor::addFxToTrack(int trackId, const juce::String& fxName) {
    // Internal plugin lookup — shares internal_plugins.hpp with the DSL
    // interpreter so a single canonical alias per plugin is accepted by both.
    if (const auto* match = lookupInternalPluginByAlias(fxName)) {
        DeviceInfo device;
        device.name = match->displayName;
        device.pluginId = match->pluginId;
        device.format = PluginFormat::Internal;
        device.deviceType = match->deviceType;
        device.isInstrument = (match->deviceType == DeviceType::Instrument);

        auto cmd = std::make_unique<AddDeviceToTrackCommand>(trackId, device);
        auto* cmdPtr = cmd.get();
        api_.undo().executeCommand(std::move(cmd));
        if (cmdPtr->getCreatedDeviceId() == INVALID_DEVICE_ID) {
            error_ = "Failed to add FX '" + fxName + "'";
            return false;
        }
        results_.add("Added FX '" + match->displayName + "'");
        return true;
    }

    // External plugin lookup via alias matching
    auto* engine = api_.tracks().getAudioEngine();
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
        if (desc.name.equalsIgnoreCase(fxName) || alias.equalsIgnoreCase(fxName)) {
            bestMatch = &desc;
            break;
        }
    }

    if (!bestMatch) {
        error_ = "Plugin '" + fxName + "' not found";
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

    auto cmd = std::make_unique<AddDeviceToTrackCommand>(trackId, device);
    auto* cmdPtr = cmd.get();
    api_.undo().executeCommand(std::move(cmd));
    if (cmdPtr->getCreatedDeviceId() == INVALID_DEVICE_ID) {
        error_ = "Failed to add plugin '" + fxName + "'";
        return false;
    }

    results_.add("Added plugin '" + matchedName + "' by " + device.manufacturer);
    return true;
}

bool InstructionExecutor::executeSelect(const SelectOp& op) {
    auto& sm = api_.selection();

    if (op.target == SelectOp::Target::Tracks) {
        auto& tm = api_.tracks();
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
        // Advance the implicit-context track so follow-up commands
        // (MUTE/SOLO/SET/CLIP) without an explicit ref target the first
        // match instead of a stale context from before SELECT ran.
        if (!matches.empty())
            currentTrackId_ = *matches.begin();
        results_.add("Selected " + juce::String(static_cast<int>(matches.size())) + " track(s)");
        return true;
    }

    // SELECT CLIPS
    auto& cm = api_.clips();
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
            for (const auto& track : api_.tracks().getTracks()) {
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
    // Advance implicit-context track via the first clip's owner so
    // follow-up commands without an explicit track ref target something
    // sensible.
    if (!matches.empty()) {
        if (const auto* firstClip = cm.getClip(*matches.begin()))
            currentTrackId_ = firstClip->trackId;
    }
    results_.add("Selected " + juce::String(static_cast<int>(matches.size())) + " clip(s)");
    return true;
}

bool InstructionExecutor::executeArp(const ArpOp& op) {
    if (currentClipId_ < 0) {
        if (!autoCreateClip())
            return false;
    }

    std::vector<int> midiNotes;
    juce::String chordError;
    if (!music::resolveChordNotes(op.root.toStdString(), op.quality.toStdString(), op.inversion,
                                  midiNotes, chordError)) {
        error_ = chordError;
        return false;
    }

    // Sort ascending as the canonical starting order
    std::sort(midiNotes.begin(), midiNotes.end());

    // Apply pattern ordering: up (default), down, updown
    auto pattern = op.pattern.trim().toLowerCase();
    std::vector<int> ordered;
    if (pattern == "down") {
        ordered.assign(midiNotes.rbegin(), midiNotes.rend());
    } else if (pattern == "updown" || pattern == "upanddown" || pattern == "up_down") {
        ordered = midiNotes;
        if (midiNotes.size() > 2) {
            // Add descent without repeating the top and bottom notes
            for (auto it = midiNotes.rbegin() + 1; it + 1 != midiNotes.rend(); ++it)
                ordered.push_back(*it);
        }
    } else {
        // "up" or empty/unknown — ascending
        ordered = midiNotes;
    }

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

    api_.undo().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
        currentClipId_, std::move(notes),
        "Add " + op.quality + " arpeggio at beat " + juce::String(op.beat, 2)));

    results_.add("Added arpeggio " + op.root + " " + op.quality);
    return true;
}

bool InstructionExecutor::executeChord(const ChordOp& op) {
    if (currentClipId_ < 0) {
        if (!autoCreateClip())
            return false;
    }

    std::vector<int> midiNotes;
    juce::String chordError;
    if (!music::resolveChordNotes(op.root.toStdString(), op.quality.toStdString(), op.inversion,
                                  midiNotes, chordError)) {
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

    api_.undo().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
        currentClipId_, std::move(notes),
        "Add " + op.quality + " chord at beat " + juce::String(op.beat, 2)));

    results_.add("Added chord " + op.root + " " + op.quality);
    return true;
}

bool InstructionExecutor::executeHit(const HitOp& op) {
    if (currentTrackId_ < 0) {
        error_ = "No current track context for HIT (drummer needs a selected track)";
        return false;
    }

    // Validate the instrument BEFORE auto-creating a clip — otherwise a track
    // with no instrument gets a stranded empty clip every time the drummer
    // runs against it.
    const auto* device = api_.tracks().getPrimaryInstrument(currentTrackId_);
    if (device == nullptr) {
        error_ = "Track has no instrument plugin to resolve drum roles";
        return false;
    }

    if (currentClipId_ < 0) {
        if (!autoCreateClip())
            return false;
    }

    // Resolve role -> noteNumber via the per-instance kit. Missing rows are
    // not a fatal error — the agent emits the full role vocabulary and the
    // kit may legitimately omit some roles (e.g. a drumkit without ride).
    int noteNumber = -1;
    for (const auto& row : device->kitRows) {
        if (row.role == op.role) {
            noteNumber = row.noteNumber;
            break;
        }
    }
    if (noteNumber < 0) {
        results_.add("Skipped " + op.role + " — no row in track's kit");
        return true;
    }

    int velocity = op.velocity >= 0 ? op.velocity : 100;
    api_.undo().executeCommand(std::make_unique<AddMidiNoteCommand>(
        currentClipId_, op.beat, noteNumber, op.length, velocity));
    results_.add("Hit " + op.role);
    return true;
}

bool InstructionExecutor::executeNote(const NoteOp& op) {
    if (currentClipId_ < 0) {
        if (!autoCreateClip())
            return false;
    }

    int noteNumber = music::parseNoteName(op.pitch.toStdString());
    if (noteNumber < 0 || noteNumber > 127) {
        error_ = "Invalid pitch: " + op.pitch;
        return false;
    }

    int velocity = op.velocity >= 0 ? op.velocity : 100;

    api_.undo().executeCommand(std::make_unique<AddMidiNoteCommand>(
        currentClipId_, op.beat, noteNumber, op.length, velocity));

    results_.add("Added note " + op.pitch);
    return true;
}

}  // namespace magda
