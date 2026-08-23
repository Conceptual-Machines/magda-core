#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"

namespace magda {

// ============================================================================
// Clip serialization helpers
// ============================================================================

namespace {

juce::var serializeMidiCurveHandle(const MidiCurveHandle& handle) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("dx", handle.dx);
    obj->setProperty("dy", handle.dy);
    obj->setProperty("linked", handle.linked);
    return juce::var(obj);
}

void deserializeMidiCurveHandle(const juce::var& json, MidiCurveHandle& handle) {
    if (auto* obj = json.getDynamicObject()) {
        if (obj->hasProperty("dx"))
            handle.dx = obj->getProperty("dx");
        if (obj->hasProperty("dy"))
            handle.dy = obj->getProperty("dy");
        if (obj->hasProperty("linked"))
            handle.linked = static_cast<bool>(obj->getProperty("linked"));
    }
}

double getValidProjectTempo(double projectTempo) {
    return isValidBpm(projectTempo) ? projectTempo : DEFAULT_BPM;
}

juce::var serializeAudioEvent(const AudioEvent& event) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", event.id);
    obj->setProperty("sourceId", event.sourceId);

    obj->setProperty("startBeat", event.startBeat);
    obj->setProperty("lengthBeats", event.lengthBeats);

    // Source-domain values are samples at the source's own rate. juce::var has
    // no 64-bit integer, so they travel as strings rather than silently losing
    // precision through a double on long files.
    obj->setProperty("anchorSamples", juce::String(event.sourceAnchorSamples));
    obj->setProperty("loopStartSamples", juce::String(event.loopStartSamples));
    obj->setProperty("loopLengthSamples", juce::String(event.loopLengthSamples));

    obj->setProperty("interpBpm", event.interpBpm);
    obj->setProperty("interpTotalBeats", event.interpTotalBeats);
    obj->setProperty("interpTotalBeatsLocked", event.interpTotalBeatsLocked);
    if (!event.keyRoot.empty())
        obj->setProperty("keyRoot", juce::String(event.keyRoot));
    if (!event.keyScale.empty())
        obj->setProperty("keyScale", juce::String(event.keyScale));

    obj->setProperty("autoTempo", event.autoTempo);
    obj->setProperty("speedRatio", event.speedRatio);
    if (event.timeStretchMode != 0)
        obj->setProperty("timeStretchMode", event.timeStretchMode);
    if (event.warpEnabled)
        obj->setProperty("warpEnabled", true);

    // Markers persist whether or not warp is on. The model treats the two as
    // independent everywhere else: turning warp off keeps the map, paste
    // preserves it, and ghosts share it as its own field. Writing it only when
    // the toggle is on meant re-enabling warp restored the map in-session but
    // lost it across a save.
    if (!event.warpMarkers.empty()) {
        juce::Array<juce::var> warpArray;
        for (const auto& wm : event.warpMarkers) {
            auto* wmObj = new juce::DynamicObject();
            wmObj->setProperty("sourceTime", wm.sourceTime);
            wmObj->setProperty("warpTime", wm.warpTime);
            warpArray.add(juce::var(wmObj));
        }
        obj->setProperty("warpMarkers", warpArray);
    }

    obj->setProperty("autoPitch", event.autoPitch);
    if (event.analogPitch)
        obj->setProperty("analogPitch", true);
    obj->setProperty("autoPitchMode", event.autoPitchMode);
    obj->setProperty("pitchChange", event.pitchChange);
    obj->setProperty("transpose", event.transpose);

    obj->setProperty("reversed", event.reversed);
    obj->setProperty("autoDetectBeats", event.autoDetectBeats);
    obj->setProperty("beatSensitivity", event.beatSensitivity);

    obj->setProperty("leftChannelActive", event.leftChannelActive);
    obj->setProperty("rightChannelActive", event.rightChannelActive);

    obj->setProperty("gainDB", event.gainDB);
    obj->setProperty("fadeInSeconds", event.fadeInSeconds);
    obj->setProperty("fadeOutSeconds", event.fadeOutSeconds);
    obj->setProperty("fadeInType", event.fadeInType);
    obj->setProperty("fadeOutType", event.fadeOutType);
    obj->setProperty("fadeInBehaviour", event.fadeInBehaviour);
    obj->setProperty("fadeOutBehaviour", event.fadeOutBehaviour);

    return juce::var(obj);
}

int64_t readSamples(const juce::DynamicObject& obj, const char* key) {
    return obj.hasProperty(key) ? obj.getProperty(key).toString().getLargeIntValue() : 0;
}

void deserializeAudioEvent(const juce::var& json, AudioEvent& event) {
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return;

    event.id = obj->getProperty("id");
    event.sourceId = obj->hasProperty("sourceId") ? static_cast<int>(obj->getProperty("sourceId"))
                                                  : INVALID_SOURCE_ID;

    event.startBeat = obj->getProperty("startBeat");
    event.lengthBeats = obj->getProperty("lengthBeats");

    event.sourceAnchorSamples = readSamples(*obj, "anchorSamples");
    event.loopStartSamples = readSamples(*obj, "loopStartSamples");
    event.loopLengthSamples = readSamples(*obj, "loopLengthSamples");

    event.interpBpm = obj->getProperty("interpBpm");
    event.interpTotalBeats = obj->getProperty("interpTotalBeats");
    event.interpTotalBeatsLocked = static_cast<bool>(obj->getProperty("interpTotalBeatsLocked"));
    event.keyRoot = obj->getProperty("keyRoot").toString().toStdString();
    event.keyScale = obj->getProperty("keyScale").toString().toStdString();

    event.autoTempo = static_cast<bool>(obj->getProperty("autoTempo"));
    event.speedRatio = obj->getProperty("speedRatio");
    if (event.speedRatio <= 0.0)
        event.speedRatio = 1.0;
    event.timeStretchMode = obj->getProperty("timeStretchMode");
    event.warpEnabled = static_cast<bool>(obj->getProperty("warpEnabled"));
    if (auto warpVar = obj->getProperty("warpMarkers"); warpVar.isArray()) {
        for (const auto& entry : *warpVar.getArray()) {
            if (auto* wmObj = entry.getDynamicObject()) {
                event.warpMarkers.push_back(
                    {wmObj->getProperty("sourceTime"), wmObj->getProperty("warpTime")});
            }
        }
    }

    event.autoPitch = static_cast<bool>(obj->getProperty("autoPitch"));
    event.analogPitch = static_cast<bool>(obj->getProperty("analogPitch"));
    event.autoPitchMode = obj->getProperty("autoPitchMode");
    event.pitchChange = static_cast<float>(static_cast<double>(obj->getProperty("pitchChange")));
    event.transpose = obj->getProperty("transpose");

    event.reversed = static_cast<bool>(obj->getProperty("reversed"));
    event.autoDetectBeats = static_cast<bool>(obj->getProperty("autoDetectBeats"));
    if (obj->hasProperty("beatSensitivity")) {
        event.beatSensitivity =
            static_cast<float>(static_cast<double>(obj->getProperty("beatSensitivity")));
    }

    if (obj->hasProperty("leftChannelActive"))
        event.leftChannelActive = static_cast<bool>(obj->getProperty("leftChannelActive"));
    if (obj->hasProperty("rightChannelActive"))
        event.rightChannelActive = static_cast<bool>(obj->getProperty("rightChannelActive"));

    event.gainDB = static_cast<float>(static_cast<double>(obj->getProperty("gainDB")));
    event.fadeInSeconds = obj->getProperty("fadeInSeconds");
    event.fadeOutSeconds = obj->getProperty("fadeOutSeconds");
    if (obj->hasProperty("fadeInType"))
        event.fadeInType = obj->getProperty("fadeInType");
    if (obj->hasProperty("fadeOutType"))
        event.fadeOutType = obj->getProperty("fadeOutType");
    event.fadeInBehaviour = obj->getProperty("fadeInBehaviour");
    event.fadeOutBehaviour = obj->getProperty("fadeOutBehaviour");
}

/// Audio interpretation that schema v1 stored on the CLIP rather than on an
/// event. Collected while reading the clip, then folded into the one migrated
/// event. v2 files leave every field at its default.
struct LegacyAudioFields {
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    int fadeInType = 1;
    int fadeOutType = 1;
    int fadeInBehaviour = 0;
    int fadeOutBehaviour = 0;
    float pitchChange = 0.0f;
    int transpose = 0;
    bool autoPitch = false;
    int autoPitchMode = 0;
    bool reversed = false;
    bool autoDetectBeats = false;
    float beatSensitivity = 0.5f;
    bool leftChannelActive = true;
    bool rightChannelActive = true;
    bool autoTempo = false;
};

/// The v1 "one source per clip" record, in whichever of the three historical
/// layouts it was written.
struct LegacyAudioSource {
    juce::String filePath;
    double durationSeconds = 0.0;
    double interpBpm = 0.0;
    double interpTotalBeats = 0.0;
    bool interpTotalBeatsLocked = false;

    double offsetSeconds = 0.0;
    double offsetBeats = 0.0;
    double loopStartSeconds = 0.0;
    double loopLengthSeconds = 0.0;
    double loopStartBeats = 0.0;
    double loopLengthBeats = 0.0;
    double speedRatio = 1.0;

    bool warpEnabled = false;
    bool analogPitch = false;
    int timeStretchMode = 0;
    std::vector<WarpMarker> warpMarkers;
};

void readLegacyWarpMarkers(const juce::var& warpMarkersVar, std::vector<WarpMarker>& out) {
    if (!warpMarkersVar.isArray())
        return;
    for (const auto& wmVar : *warpMarkersVar.getArray()) {
        if (auto* wmObj = wmVar.getDynamicObject())
            out.push_back({wmObj->getProperty("sourceTime"), wmObj->getProperty("warpTime")});
    }
}

/// Turn a v1 audio clip into a clip holding exactly one event that spans it.
///
/// The source-domain values were seconds (or beats, for autoTempo clips) and
/// become samples at the source's own rate. When the file cannot be opened the
/// pool reports a nominal rate and rescales the anchors on the first successful
/// open, so an offline project still round-trips.
void migrateLegacyAudioClip(const LegacyAudioSource& v1, const LegacyAudioFields& legacy,
                            ClipInfo& outClip, std::vector<Source>* legacySources,
                            double projectTempo) {
    // Duration recorded in the project is a fallback for a file we cannot read.
    double fallbackDuration = v1.durationSeconds;
    if (fallbackDuration <= 0.0 && v1.interpTotalBeats > 0.0 && v1.interpBpm > 0.0)
        fallbackDuration = v1.interpTotalBeats * 60.0 / v1.interpBpm;
    fallbackDuration = juce::jmax(0.0, fallbackDuration);

    AudioEvent event;
    if (legacySources != nullptr) {
        // Staging: no pool writes. The anchors below are computed at the
        // nominal rate and rescaled when install acquires the real file.
        event.sourceId =
            ProjectSerializer::stageLegacySource(*legacySources, v1.filePath, fallbackDuration);
    } else {
        event.sourceId = SourcePool::getInstance().acquire(v1.filePath);
        if (auto* source = SourcePool::getInstance().getMutable(event.sourceId);
            source != nullptr && source->durationSeconds <= 0.0) {
            source->durationSeconds = fallbackDuration;
        }
    }

    event.interpBpm = v1.interpBpm;
    event.interpTotalBeats = v1.interpTotalBeats;
    event.interpTotalBeatsLocked = v1.interpTotalBeatsLocked;
    event.autoTempo = legacy.autoTempo;
    event.speedRatio = v1.speedRatio > 0.0 ? v1.speedRatio : 1.0;
    event.timeStretchMode = v1.timeStretchMode;
    event.warpEnabled = v1.warpEnabled;
    event.warpMarkers = v1.warpMarkers;
    event.analogPitch = v1.analogPitch;

    // v1 kept beats authoritative under autoTempo and seconds otherwise. Both
    // resolve to the same source seconds, which is what becomes the anchor.
    //
    // A v1 clip could be saved with autoTempo on and interpBpm still 0, with
    // detection pending: its seconds mirrors were deliberately stale and v1
    // playback read the beat fields through the project tempo. Migrating off
    // those mirrors would land the clip somewhere it never played, so fall
    // back to the same tempo v1 did.
    const double interpretationBpm = v1.interpBpm > 0.0 ? v1.interpBpm : projectTempo;
    const bool beatsAuthoritative = legacy.autoTempo && isValidBpm(interpretationBpm);
    const double secondsPerBeat = beatsAuthoritative ? 60.0 / interpretationBpm : 0.0;
    event.setAnchorSeconds(beatsAuthoritative ? v1.offsetBeats * secondsPerBeat : v1.offsetSeconds);

    event.setLoopStartSeconds(beatsAuthoritative ? v1.loopStartBeats * secondsPerBeat
                                                 : v1.loopStartSeconds);
    event.setLoopLengthSeconds(beatsAuthoritative ? v1.loopLengthBeats * secondsPerBeat
                                                  : v1.loopLengthSeconds);

    event.autoPitch = legacy.autoPitch;
    event.autoPitchMode = legacy.autoPitchMode;
    event.pitchChange = legacy.pitchChange;
    event.transpose = legacy.transpose;
    event.reversed = legacy.reversed;
    event.autoDetectBeats = legacy.autoDetectBeats;
    event.beatSensitivity = legacy.beatSensitivity;
    event.leftChannelActive = legacy.leftChannelActive;
    event.rightChannelActive = legacy.rightChannelActive;

    event.fadeInSeconds = legacy.fadeIn;
    event.fadeOutSeconds = legacy.fadeOut;
    event.fadeInType = legacy.fadeInType;
    event.fadeOutType = legacy.fadeOutType;
    event.fadeInBehaviour = legacy.fadeInBehaviour;
    event.fadeOutBehaviour = legacy.fadeOutBehaviour;

    outClip.audio().addEvent(std::move(event));
    outClip.syncSingleEventToClipBounds();
}

/// Takes and comp sections are unchanged by the event split.
void readAudioTakesAndComp(const juce::DynamicObject& audioObj, ClipInfo& outClip) {
    auto takesVar = audioObj.getProperty("takes");
    if (takesVar.isArray()) {
        for (const auto& takeVar : *takesVar.getArray()) {
            if (auto* takeObj = takeVar.getDynamicObject()) {
                AudioTake take;
                take.filePath = takeObj->getProperty("filePath").toString();
                take.durationSeconds = takeObj->getProperty("durationSeconds");
                outClip.audio().takes.push_back(take);
            }
        }
        if (!outClip.audio().takes.empty()) {
            outClip.audio().currentTakeIndex =
                juce::jlimit(0, static_cast<int>(outClip.audio().takes.size()) - 1,
                             static_cast<int>(audioObj.getProperty("currentTakeIndex")));
        }
    }

    auto compVar = audioObj.getProperty("comp");
    if (compVar.isArray()) {
        for (const auto& secVar : *compVar.getArray()) {
            if (auto* secObj = secVar.getDynamicObject()) {
                CompSection sec;
                sec.startSeconds = secObj->getProperty("startSeconds");
                sec.endSeconds = secObj->getProperty("endSeconds");
                sec.takeIndex = static_cast<int>(secObj->getProperty("takeIndex"));
                outClip.audio().comp.push_back(sec);
            }
        }
        outClip.audio().compActive =
            !outClip.audio().comp.empty() && static_cast<bool>(audioObj.getProperty("compActive"));
    }
}

}  // namespace

juce::var ProjectSerializer::serializeClipInfo(const ClipInfo& clip) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", clip.id);
    obj->setProperty("trackId", clip.trackId);
    if (clip.linkGroupId != 0)
        obj->setProperty("linkGroupId", clip.linkGroupId);
    obj->setProperty("name", clip.name);
    obj->setProperty("colour", colourToString(clip.colour));
    obj->setProperty("type", static_cast<int>(clip.getType()));
    obj->setProperty("view", static_cast<int>(clip.view));
    obj->setProperty("enabled", clip.enabled);
    obj->setProperty("stackOrder", clip.stackOrder);
    obj->setProperty("overlapPlaysBoth", clip.overlapPlaysBoth);
    obj->setProperty("sceneIndex", clip.sceneIndex);
    obj->setProperty("launchMode", static_cast<int>(clip.launchMode));
    obj->setProperty("launchQuantize", static_cast<int>(clip.launchQuantize));
    obj->setProperty("followAction", static_cast<int>(clip.followAction));
    obj->setProperty("followActionDelayBeats", clip.followActionDelayBeats);
    obj->setProperty("followActionLoopCount", clip.followActionLoopCount);

    auto* placementObj = new juce::DynamicObject();
    placementObj->setProperty("startBeat", clip.placement.startBeat);
    placementObj->setProperty("lengthBeats", clip.placement.lengthBeats);
    obj->setProperty("placement", juce::var(placementObj));

    // Per-clip grid settings
    obj->setProperty("gridAutoGrid", clip.gridAutoGrid);
    obj->setProperty("gridNumerator", clip.gridNumerator);
    obj->setProperty("gridDenominator", clip.gridDenominator);
    obj->setProperty("gridSnapEnabled", clip.gridSnapEnabled);
    if (clip.midiEditorRowHeight > 0)
        obj->setProperty("midiEditorRowHeight", clip.midiEditorRowHeight);

    // Per-clip mix
    obj->setProperty("volumeDB", clip.volumeDB);
    obj->setProperty("gainDB", clip.gainDB);
    obj->setProperty("pan", clip.pan);

    // Crossfade with the neighbouring clip; per-event fades live on the event.
    obj->setProperty("autoCrossfade", clip.autoCrossfade);
    obj->setProperty("launchFadeSamples", clip.launchFadeSamples);

    // MIDI offset
    if (clip.midiOffset != 0.0)
        obj->setProperty("midiOffset", clip.midiOffset);
    if (clip.midiTrimOffset != 0.0)
        obj->setProperty("midiTrimOffset", clip.midiTrimOffset);
    obj->setProperty("loopEnabled", clip.loopEnabled);
    if (clip.isMidi()) {
        const auto& midi = clip.midi();
        if (clip.loopStartBeats != 0.0)
            obj->setProperty("loopStartBeats", clip.loopStartBeats);
        if (clip.loopLengthBeats > 0.0)
            obj->setProperty("loopLengthBeats", clip.loopLengthBeats);
        if (midi.sourceFilePath.isNotEmpty() || !midi.takes.empty()) {
            auto* midiObj = new juce::DynamicObject();
            if (midi.sourceFilePath.isNotEmpty())
                midiObj->setProperty("sourceFilePath", midi.sourceFilePath);
            // Loop-record takes (one note set per pass).
            if (!midi.takes.empty()) {
                juce::Array<juce::var> takesArray;
                for (const auto& take : midi.takes) {
                    auto* takeObj = new juce::DynamicObject();
                    juce::Array<juce::var> notes;
                    for (const auto& n : take.notes)
                        notes.add(serializeMidiNote(n));
                    juce::Array<juce::var> ccs;
                    for (const auto& cc : take.cc)
                        ccs.add(serializeMidiCCData(cc));
                    juce::Array<juce::var> pbs;
                    for (const auto& pb : take.pitchBend)
                        pbs.add(serializeMidiPitchBendData(pb));
                    takeObj->setProperty("notes", juce::var(notes));
                    takeObj->setProperty("cc", juce::var(ccs));
                    takeObj->setProperty("pitchBend", juce::var(pbs));
                    takesArray.add(juce::var(takeObj));
                }
                midiObj->setProperty("takes", juce::var(takesArray));
                midiObj->setProperty("currentTakeIndex", midi.currentTakeIndex);
            }
            // Comp sections (beats). Active note list is persisted via midiNotes.
            if (!midi.comp.empty()) {
                juce::Array<juce::var> compArray;
                for (const auto& sec : midi.comp) {
                    auto* secObj = new juce::DynamicObject();
                    secObj->setProperty("startBeat", sec.startBeat);
                    secObj->setProperty("endBeat", sec.endBeat);
                    secObj->setProperty("takeIndex", sec.takeIndex);
                    compArray.add(juce::var(secObj));
                }
                midiObj->setProperty("comp", juce::var(compArray));
                midiObj->setProperty("compActive", midi.compActive);
            }
            obj->setProperty("midi", juce::var(midiObj));
        }
    }

    // Groove/Shuffle/Swing
    if (clip.grooveTemplate.isNotEmpty())
        obj->setProperty("grooveTemplate", clip.grooveTemplate);
    if (clip.grooveStrength > 0.0f)
        obj->setProperty("grooveStrength", clip.grooveStrength);

    if (clip.isAudio() && !clip.audio().events.empty()) {
        auto* audioObj = new juce::DynamicObject();

        // Schema v2 (#1901): the clip hosts a list of events, each referencing a
        // pooled Source by id. The Source records themselves are written once
        // per project, not per clip.
        juce::Array<juce::var> eventsArray;
        for (const auto& event : clip.audio().events)
            eventsArray.add(serializeAudioEvent(event));
        audioObj->setProperty("events", eventsArray);
        audioObj->setProperty("nextEventId", clip.audio().nextEventId);

        // Loop-record takes (one source file per pass). Persist so the take
        // alternates survive save/reload; the engine rebuilds them on sync.
        if (!clip.audio().takes.empty()) {
            juce::Array<juce::var> takesArray;
            for (const auto& take : clip.audio().takes) {
                auto* takeObj = new juce::DynamicObject();
                takeObj->setProperty("filePath", take.filePath);
                takeObj->setProperty("durationSeconds", take.durationSeconds);
                takesArray.add(juce::var(takeObj));
            }
            audioObj->setProperty("takes", takesArray);
            audioObj->setProperty("currentTakeIndex", clip.audio().currentTakeIndex);
        }

        // Comp sections (the render itself is regenerated, not persisted).
        if (!clip.audio().comp.empty()) {
            juce::Array<juce::var> compArray;
            for (const auto& sec : clip.audio().comp) {
                auto* secObj = new juce::DynamicObject();
                secObj->setProperty("startSeconds", sec.startSeconds);
                secObj->setProperty("endSeconds", sec.endSeconds);
                secObj->setProperty("takeIndex", sec.takeIndex);
                compArray.add(juce::var(secObj));
            }
            audioObj->setProperty("comp", compArray);
            audioObj->setProperty("compActive", clip.audio().compActive);
        }

        obj->setProperty("audio", juce::var(audioObj));
    }

    // MIDI notes
    juce::Array<juce::var> midiNotesArray;
    for (const auto& note : clip.midiNotes) {
        midiNotesArray.add(serializeMidiNote(note));
    }
    obj->setProperty("midiNotes", juce::var(midiNotesArray));

    // MIDI CC data
    if (!clip.midiCCData.empty()) {
        juce::Array<juce::var> ccArray;
        for (const auto& cc : clip.midiCCData) {
            ccArray.add(serializeMidiCCData(cc));
        }
        obj->setProperty("midiCCData", juce::var(ccArray));
    }

    // MIDI pitch bend data
    if (!clip.midiPitchBendData.empty()) {
        juce::Array<juce::var> pbArray;
        for (const auto& pb : clip.midiPitchBendData) {
            pbArray.add(serializeMidiPitchBendData(pb));
        }
        obj->setProperty("midiPitchBendData", juce::var(pbArray));
    }

    // Chord annotations
    if (!clip.chordAnnotations.empty()) {
        juce::Array<juce::var> chordArray;
        for (const auto& ca : clip.chordAnnotations) {
            auto* caObj = new juce::DynamicObject();
            caObj->setProperty("beatPosition", ca.beatPosition);
            caObj->setProperty("lengthBeats", ca.lengthBeats);
            caObj->setProperty("chordName", ca.chordName);
            if (ca.chordGroup != 0)
                caObj->setProperty("chordGroup", ca.chordGroup);
            chordArray.add(juce::var(caObj));
        }
        obj->setProperty("chordAnnotations", chordArray);
    }
    if (clip.nextChordGroupId > 1)
        obj->setProperty("nextChordGroupId", clip.nextChordGroupId);

    return juce::var(obj);
}

bool ProjectSerializer::deserializeClipInfo(const juce::var& json, ClipInfo& outClip,
                                            double projectTempo,
                                            std::vector<Source>* legacySources) {
    if (!json.isObject()) {
        lastError_ = "Clip data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outClip.id = obj->getProperty("id");
    outClip.trackId = obj->getProperty("trackId");
    outClip.linkGroupId = obj->getProperty("linkGroupId");  // void -> 0 (unlinked)
    outClip.name = obj->getProperty("name").toString();
    outClip.colour = stringToColour(obj->getProperty("colour").toString());
    const auto typeVar = obj->getProperty("type");
    if (typeVar.isVoid()) {
        lastError_ = "Clip is missing type";
        return false;
    }

    const int rawClipType = static_cast<int>(typeVar);
    if (rawClipType == static_cast<int>(ClipType::Audio)) {
        outClip.setAudioContent();
    } else if (rawClipType == static_cast<int>(ClipType::MIDI)) {
        outClip.setMidiContent();
    } else {
        lastError_ = "Unknown clip type: " + juce::String(rawClipType);
        return false;
    }
    outClip.view = static_cast<ClipView>(static_cast<int>(obj->getProperty("view")));

    auto* placementObj = obj->getProperty("placement").getDynamicObject();
    if (placementObj != nullptr) {
        outClip.setPlacementBeats(placementObj->getProperty("startBeat"),
                                  placementObj->getProperty("lengthBeats"));
    } else if (obj->hasProperty("startBeats") || obj->hasProperty("startTime")) {
        const double legacyTempo = getValidProjectTempo(projectTempo);
        const double legacyStartBeats =
            obj->hasProperty("startBeats")
                ? static_cast<double>(obj->getProperty("startBeats"))
                : static_cast<double>(obj->getProperty("startTime")) * legacyTempo / 60.0;
        double legacyLengthBeats = obj->hasProperty("lengthBeats")
                                       ? static_cast<double>(obj->getProperty("lengthBeats"))
                                       : 0.0;
        if (legacyLengthBeats <= 0.0 && obj->hasProperty("length"))
            legacyLengthBeats =
                static_cast<double>(obj->getProperty("length")) * legacyTempo / 60.0;
        outClip.setPlacementBeats(legacyStartBeats, legacyLengthBeats);
    } else {
        lastError_ = "Clip is missing placement";
        return false;
    }
    outClip.deriveTimesFromBeats(projectTempo);

    // Enabled state (missing in projects saved before #1736 → default true)
    if (!obj->getProperty("enabled").isVoid())
        outClip.enabled = static_cast<bool>(obj->getProperty("enabled"));

    // Stacking order (#2003). Missing in projects saved before it existed, and
    // 0 across the board is exactly right there: ties fall back to clip id, so
    // those projects keep the order they were created in.
    if (!obj->getProperty("stackOrder").isVoid())
        outClip.stackOrder = static_cast<int>(obj->getProperty("stackOrder"));

    // Missing in projects saved before it existed, where the top clip always
    // won: false is that behaviour.
    if (!obj->getProperty("overlapPlaysBoth").isVoid())
        outClip.overlapPlaysBoth = static_cast<bool>(obj->getProperty("overlapPlaysBoth"));

    outClip.loopEnabled = static_cast<bool>(obj->getProperty("loopEnabled"));
    outClip.sceneIndex = obj->getProperty("sceneIndex");

    // Launch properties
    outClip.launchMode = static_cast<LaunchMode>(static_cast<int>(obj->getProperty("launchMode")));
    outClip.launchQuantize =
        static_cast<LaunchQuantize>(static_cast<int>(obj->getProperty("launchQuantize")));
    if (!obj->getProperty("followAction").isVoid())
        outClip.followAction =
            static_cast<FollowAction>(static_cast<int>(obj->getProperty("followAction")));
    if (!obj->getProperty("followActionDelayBeats").isVoid())
        outClip.followActionDelayBeats =
            juce::jmax(0.0, static_cast<double>(obj->getProperty("followActionDelayBeats")));
    if (!obj->getProperty("followActionLoopCount").isVoid())
        outClip.followActionLoopCount =
            juce::jmax(1, static_cast<int>(obj->getProperty("followActionLoopCount")));

    // Per-clip grid settings
    outClip.gridAutoGrid = static_cast<bool>(obj->getProperty("gridAutoGrid"));
    outClip.gridNumerator = obj->getProperty("gridNumerator");
    outClip.gridDenominator = obj->getProperty("gridDenominator");
    outClip.gridSnapEnabled = static_cast<bool>(obj->getProperty("gridSnapEnabled"));
    if (obj->hasProperty("midiEditorRowHeight")) {
        outClip.midiEditorRowHeight =
            juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT, ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT,
                         static_cast<int>(obj->getProperty("midiEditorRowHeight")));
    } else if (obj->hasProperty("pianoRollNoteHeight")) {
        outClip.midiEditorRowHeight =
            juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT, ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT,
                         static_cast<int>(obj->getProperty("pianoRollNoteHeight")));
    }

    // Per-clip mix
    outClip.volumeDB = static_cast<float>(static_cast<double>(obj->getProperty("volumeDB")));
    outClip.gainDB = static_cast<float>(static_cast<double>(obj->getProperty("gainDB")));
    outClip.pan = static_cast<float>(static_cast<double>(obj->getProperty("pan")));

    outClip.autoCrossfade = static_cast<bool>(obj->getProperty("autoCrossfade"));
    outClip.launchFadeSamples = obj->getProperty("launchFadeSamples");

    // Schema v1 wrote the audio interpretation as clip-level fields. They are
    // read here and folded into the migrated event further down; v2 files carry
    // them per event and simply leave these at their defaults.
    LegacyAudioFields legacy;
    legacy.fadeIn = obj->getProperty("fadeIn");
    legacy.fadeOut = obj->getProperty("fadeOut");
    if (obj->hasProperty("fadeInType"))
        legacy.fadeInType = obj->getProperty("fadeInType");
    if (obj->hasProperty("fadeOutType"))
        legacy.fadeOutType = obj->getProperty("fadeOutType");
    legacy.fadeInBehaviour = obj->getProperty("fadeInBehaviour");
    legacy.fadeOutBehaviour = obj->getProperty("fadeOutBehaviour");
    legacy.pitchChange = static_cast<float>(static_cast<double>(obj->getProperty("pitchChange")));
    legacy.transpose = obj->getProperty("transpose");
    legacy.autoPitch = static_cast<bool>(obj->getProperty("autoPitch"));
    legacy.autoPitchMode = obj->getProperty("autoPitchMode");
    legacy.reversed = static_cast<bool>(obj->getProperty("isReversed"));
    legacy.autoDetectBeats = static_cast<bool>(obj->getProperty("autoDetectBeats"));
    if (obj->hasProperty("beatSensitivity")) {
        legacy.beatSensitivity =
            static_cast<float>(static_cast<double>(obj->getProperty("beatSensitivity")));
    }
    if (obj->hasProperty("leftChannelActive"))
        legacy.leftChannelActive = static_cast<bool>(obj->getProperty("leftChannelActive"));
    if (obj->hasProperty("rightChannelActive"))
        legacy.rightChannelActive = static_cast<bool>(obj->getProperty("rightChannelActive"));
    legacy.autoTempo = static_cast<bool>(obj->getProperty("autoTempo"));

    // MIDI offset
    outClip.midiOffset = obj->getProperty("midiOffset");
    outClip.midiTrimOffset = obj->getProperty("midiTrimOffset");
    if (outClip.isMidi()) {
        if (obj->hasProperty("loopStartBeats"))
            outClip.loopStartBeats = obj->getProperty("loopStartBeats");
        if (obj->hasProperty("loopLengthBeats"))
            outClip.loopLengthBeats = obj->getProperty("loopLengthBeats");

        if (auto* midiObj = obj->getProperty("midi").getDynamicObject()) {
            outClip.midi().sourceFilePath = midiObj->getProperty("sourceFilePath").toString();

            // Loop-record takes
            auto takesVar = midiObj->getProperty("takes");
            if (takesVar.isArray()) {
                auto& takes = outClip.midi().takes;
                for (const auto& takeVar : *takesVar.getArray()) {
                    auto* takeObj = takeVar.getDynamicObject();
                    if (takeObj == nullptr)
                        continue;
                    MidiTake take;
                    if (auto* notes = takeObj->getProperty("notes").getArray()) {
                        for (const auto& nv : *notes) {
                            MidiNote n;
                            if (deserializeMidiNote(nv, n))
                                take.notes.push_back(n);
                        }
                    }
                    if (auto* ccs = takeObj->getProperty("cc").getArray()) {
                        for (const auto& cv : *ccs) {
                            MidiCCData cc;
                            if (deserializeMidiCCData(cv, cc))
                                take.cc.push_back(cc);
                        }
                    }
                    if (auto* pbs = takeObj->getProperty("pitchBend").getArray()) {
                        for (const auto& pv : *pbs) {
                            MidiPitchBendData pb;
                            if (deserializeMidiPitchBendData(pv, pb))
                                take.pitchBend.push_back(pb);
                        }
                    }
                    takes.push_back(std::move(take));
                }
                if (!takes.empty())
                    outClip.midi().currentTakeIndex =
                        juce::jlimit(0, static_cast<int>(takes.size()) - 1,
                                     static_cast<int>(midiObj->getProperty("currentTakeIndex")));
            }

            // Comp sections
            auto compVar = midiObj->getProperty("comp");
            if (compVar.isArray()) {
                for (const auto& secVar : *compVar.getArray()) {
                    if (auto* secObj = secVar.getDynamicObject()) {
                        MidiCompSection sec;
                        sec.startBeat = secObj->getProperty("startBeat");
                        sec.endBeat = secObj->getProperty("endBeat");
                        sec.takeIndex = static_cast<int>(secObj->getProperty("takeIndex"));
                        outClip.midi().comp.push_back(sec);
                    }
                }
                outClip.midi().compActive = !outClip.midi().comp.empty() &&
                                            static_cast<bool>(midiObj->getProperty("compActive"));
            }
        }
    }

    // Groove/Shuffle/Swing
    outClip.grooveTemplate = obj->getProperty("grooveTemplate").toString();
    outClip.grooveStrength =
        static_cast<float>(static_cast<double>(obj->getProperty("grooveStrength")));

    auto* audioObj = obj->getProperty("audio").getDynamicObject();
    auto* legacyAudioSourceObj = obj->getProperty("audioSource").getDynamicObject();

    if ((audioObj != nullptr || legacyAudioSourceObj != nullptr) && !outClip.isAudio()) {
        lastError_ = "MIDI clip contains audio source data";
        return false;
    }

    if (outClip.isAudio() && audioObj != nullptr && audioObj->getProperty("events").isArray()) {
        // Schema v2 (#1901): events + pooled source ids.
        for (const auto& eventVar : *audioObj->getProperty("events").getArray()) {
            AudioEvent event;
            deserializeAudioEvent(eventVar, event);
            if (event.id == INVALID_EVENT_ID)
                event.id = outClip.audio().nextEventId++;
            outClip.audio().events.push_back(std::move(event));
        }
        if (audioObj->hasProperty("nextEventId")) {
            outClip.audio().nextEventId =
                juce::jmax(outClip.audio().nextEventId,
                           static_cast<int>(audioObj->getProperty("nextEventId")));
        }
        for (const auto& event : outClip.audio().events)
            outClip.audio().nextEventId = juce::jmax(outClip.audio().nextEventId, event.id + 1);

        readAudioTakesAndComp(*audioObj, outClip);
    } else if (outClip.isAudio() && audioObj != nullptr) {
        // Schema v1: one source + one playback block per clip. It becomes a
        // clip holding exactly one event spanning it.
        auto* sourceObj = audioObj->getProperty("source").getDynamicObject();
        auto* interpretationObj = audioObj->getProperty("interpretation").getDynamicObject();
        auto* playbackObj = audioObj->getProperty("playback").getDynamicObject();

        if (sourceObj == nullptr || interpretationObj == nullptr || playbackObj == nullptr) {
            lastError_ = "Audio clip is missing source, interpretation, or playback";
            return false;
        }

        LegacyAudioSource v1;
        v1.filePath = sourceObj->getProperty("filePath").toString();
        v1.durationSeconds = sourceObj->getProperty("durationSeconds");
        v1.interpBpm = interpretationObj->getProperty("bpm");
        v1.interpTotalBeats = interpretationObj->getProperty("totalBeats");
        v1.interpTotalBeatsLocked =
            static_cast<bool>(interpretationObj->getProperty("totalBeatsLocked"));

        v1.offsetSeconds = playbackObj->getProperty("offsetSeconds");
        v1.offsetBeats = playbackObj->getProperty("offsetBeats");
        v1.loopStartSeconds = playbackObj->getProperty("loopStartSeconds");
        v1.loopLengthSeconds = playbackObj->getProperty("loopLengthSeconds");
        v1.loopStartBeats = playbackObj->getProperty("loopStartBeats");
        v1.loopLengthBeats = playbackObj->getProperty("loopLengthBeats");
        v1.speedRatio = playbackObj->getProperty("speedRatio");

        v1.warpEnabled = static_cast<bool>(audioObj->getProperty("warpEnabled"));
        v1.analogPitch = static_cast<bool>(audioObj->getProperty("analogPitch"));
        v1.timeStretchMode = audioObj->getProperty("timeStretchMode");
        readLegacyWarpMarkers(audioObj->getProperty("warpMarkers"), v1.warpMarkers);

        migrateLegacyAudioClip(v1, legacy, outClip, legacySources, projectTempo);
        readAudioTakesAndComp(*audioObj, outClip);
    } else if (outClip.isAudio() && legacyAudioSourceObj != nullptr) {
        LegacyAudioSource v1;
        v1.filePath = legacyAudioSourceObj->getProperty("filePath").toString();
        v1.offsetSeconds = legacyAudioSourceObj->getProperty("offsetSeconds");
        v1.offsetBeats = legacyAudioSourceObj->getProperty("offsetBeats");
        v1.loopStartSeconds = legacyAudioSourceObj->getProperty("loopStartSeconds");
        v1.loopLengthSeconds = legacyAudioSourceObj->getProperty("loopLengthSeconds");
        v1.loopStartBeats = legacyAudioSourceObj->getProperty("loopStartBeats");
        v1.loopLengthBeats = legacyAudioSourceObj->getProperty("loopLengthBeats");
        v1.speedRatio = legacyAudioSourceObj->getProperty("speedRatio");
        v1.interpTotalBeats = legacyAudioSourceObj->getProperty("sourceNumBeats");
        v1.interpBpm = legacyAudioSourceObj->getProperty("sourceBPM");
        v1.warpEnabled = static_cast<bool>(legacyAudioSourceObj->getProperty("warpEnabled"));
        v1.analogPitch = static_cast<bool>(legacyAudioSourceObj->getProperty("analogPitch"));
        v1.timeStretchMode = legacyAudioSourceObj->getProperty("timeStretchMode");
        readLegacyWarpMarkers(legacyAudioSourceObj->getProperty("warpMarkers"), v1.warpMarkers);

        migrateLegacyAudioClip(v1, legacy, outClip, legacySources, projectTempo);
    } else if (outClip.isAudio() && obj->hasProperty("audioFilePath")) {
        LegacyAudioSource v1;
        v1.filePath = obj->getProperty("audioFilePath").toString();
        v1.offsetSeconds = obj->getProperty("offset");
        v1.offsetBeats = obj->getProperty("offsetBeats");
        v1.loopStartSeconds = obj->getProperty("loopStart");
        v1.loopLengthSeconds = obj->getProperty("loopLength");
        v1.loopStartBeats = obj->getProperty("loopStartBeats");
        v1.loopLengthBeats = obj->getProperty("loopLengthBeats");
        v1.speedRatio = obj->getProperty("speedRatio");
        v1.interpTotalBeats = obj->getProperty("sourceNumBeats");
        v1.interpBpm = obj->getProperty("sourceBPM");

        migrateLegacyAudioClip(v1, legacy, outClip, legacySources, projectTempo);
    }

    // MIDI notes
    auto midiNotesVar = obj->getProperty("midiNotes");
    if (midiNotesVar.isArray()) {
        auto* arr = midiNotesVar.getArray();
        for (const auto& noteVar : *arr) {
            MidiNote note;
            if (!deserializeMidiNote(noteVar, note)) {
                return false;
            }
            outClip.midiNotes.push_back(note);
        }
    }

    // MIDI CC data
    auto midiCCVar = obj->getProperty("midiCCData");
    if (midiCCVar.isArray()) {
        auto* arr = midiCCVar.getArray();
        for (const auto& ccVar : *arr) {
            MidiCCData cc;
            if (!deserializeMidiCCData(ccVar, cc))
                return false;
            outClip.midiCCData.push_back(cc);
        }
    }

    // MIDI pitch bend data
    auto midiPBVar = obj->getProperty("midiPitchBendData");
    if (midiPBVar.isArray()) {
        auto* arr = midiPBVar.getArray();
        for (const auto& pbVar : *arr) {
            MidiPitchBendData pb;
            if (!deserializeMidiPitchBendData(pbVar, pb))
                return false;
            outClip.midiPitchBendData.push_back(pb);
        }
    }

    // Chord annotations
    auto chordAnnotVar = obj->getProperty("chordAnnotations");
    if (chordAnnotVar.isArray()) {
        auto* arr = chordAnnotVar.getArray();
        for (const auto& caVar : *arr) {
            if (auto* caObj = caVar.getDynamicObject()) {
                ClipInfo::ChordAnnotation ca;
                ca.beatPosition = caObj->getProperty("beatPosition");
                ca.lengthBeats = caObj->getProperty("lengthBeats");
                ca.chordName = caObj->getProperty("chordName").toString();
                if (caObj->hasProperty("chordGroup"))
                    ca.chordGroup = static_cast<int>(caObj->getProperty("chordGroup"));
                outClip.chordAnnotations.push_back(ca);
            }
        }
    }
    if (obj->hasProperty("nextChordGroupId"))
        outClip.nextChordGroupId = static_cast<int>(obj->getProperty("nextChordGroupId"));

    outClip.deriveTimesFromBeats(projectTempo);

    return true;
}

juce::var ProjectSerializer::serializeMidiNote(const MidiNote& data) {
    auto* obj = new juce::DynamicObject();
    SER(noteNumber);
    SER(velocity);
    SER(startBeat);
    SER(lengthBeats);
    if (data.chordGroup != 0)
        SER(chordGroup);
    if (!data.pitchExpression.empty()) {
        juce::Array<juce::var> points;
        for (const auto& p : data.pitchExpression) {
            auto* pObj = new juce::DynamicObject();
            pObj->setProperty("beat", p.beat);
            pObj->setProperty("semitones", p.semitones);
            points.add(juce::var(pObj));
        }
        obj->setProperty("pitchExpression", juce::var(points));
    }
    return juce::var(obj);
}

bool ProjectSerializer::deserializeMidiNote(const juce::var& json, MidiNote& data) {
    if (!json.isObject()) {
        lastError_ = "MIDI note is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    DESER(noteNumber);
    DESER(velocity);
    DESER(startBeat);
    DESER(lengthBeats);
    if (obj->hasProperty("chordGroup"))
        data.chordGroup = static_cast<int>(obj->getProperty("chordGroup"));
    auto pitchExpVar = obj->getProperty("pitchExpression");
    if (pitchExpVar.isArray()) {
        for (const auto& pVar : *pitchExpVar.getArray()) {
            if (auto* pObj = pVar.getDynamicObject()) {
                MidiPitchExpressionPoint p;
                p.beat = pObj->getProperty("beat");
                p.semitones = pObj->getProperty("semitones");
                data.pitchExpression.push_back(p);
            }
        }
    }
    return true;
}

juce::var ProjectSerializer::serializeMidiCCData(const MidiCCData& data) {
    auto* obj = new juce::DynamicObject();
    SER(controller);
    SER(value);
    SER(beatPosition);
    SER(curveType);
    SER(tension);
    obj->setProperty("inHandle", serializeMidiCurveHandle(data.inHandle));
    obj->setProperty("outHandle", serializeMidiCurveHandle(data.outHandle));
    return juce::var(obj);
}

bool ProjectSerializer::deserializeMidiCCData(const juce::var& json, MidiCCData& data) {
    if (!json.isObject()) {
        lastError_ = "MIDI CC data is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    DESER(controller);
    DESER(value);
    DESER(beatPosition);
    if (obj->hasProperty("curveType"))
        DESER(curveType);
    if (obj->hasProperty("tension"))
        DESER(tension);
    deserializeMidiCurveHandle(obj->getProperty("inHandle"), data.inHandle);
    deserializeMidiCurveHandle(obj->getProperty("outHandle"), data.outHandle);
    return true;
}

juce::var ProjectSerializer::serializeMidiPitchBendData(const MidiPitchBendData& data) {
    auto* obj = new juce::DynamicObject();
    SER(value);
    SER(beatPosition);
    SER(curveType);
    SER(tension);
    obj->setProperty("inHandle", serializeMidiCurveHandle(data.inHandle));
    obj->setProperty("outHandle", serializeMidiCurveHandle(data.outHandle));
    return juce::var(obj);
}

bool ProjectSerializer::deserializeMidiPitchBendData(const juce::var& json,
                                                     MidiPitchBendData& data) {
    if (!json.isObject()) {
        lastError_ = "MIDI pitch bend data is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    DESER(value);
    DESER(beatPosition);
    if (obj->hasProperty("curveType"))
        DESER(curveType);
    if (obj->hasProperty("tension"))
        DESER(tension);
    deserializeMidiCurveHandle(obj->getProperty("inHandle"), data.inHandle);
    deserializeMidiCurveHandle(obj->getProperty("outHandle"), data.outHandle);
    return true;
}

}  // namespace magda
