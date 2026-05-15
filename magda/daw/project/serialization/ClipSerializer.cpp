#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"

namespace magda {

// ============================================================================
// Clip serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeClipInfo(const ClipInfo& clip) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", clip.id);
    obj->setProperty("trackId", clip.trackId);
    obj->setProperty("name", clip.name);
    obj->setProperty("colour", colourToString(clip.colour));
    obj->setProperty("type", static_cast<int>(clip.getType()));
    obj->setProperty("view", static_cast<int>(clip.view));
    obj->setProperty("loopEnabled", clip.loopEnabled);
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

    // Per-clip mix
    obj->setProperty("volumeDB", clip.volumeDB);
    obj->setProperty("gainDB", clip.gainDB);
    obj->setProperty("pan", clip.pan);

    // Fades
    obj->setProperty("fadeIn", clip.fadeIn);
    obj->setProperty("fadeOut", clip.fadeOut);
    obj->setProperty("fadeInType", clip.fadeInType);
    obj->setProperty("fadeOutType", clip.fadeOutType);
    obj->setProperty("fadeInBehaviour", clip.fadeInBehaviour);
    obj->setProperty("fadeOutBehaviour", clip.fadeOutBehaviour);
    obj->setProperty("autoCrossfade", clip.autoCrossfade);
    obj->setProperty("launchFadeSamples", clip.launchFadeSamples);

    // Pitch
    obj->setProperty("pitchChange", clip.pitchChange);
    obj->setProperty("transpose", clip.transpose);
    obj->setProperty("autoPitch", clip.autoPitch);
    obj->setProperty("autoPitchMode", clip.autoPitchMode);

    // Playback
    obj->setProperty("isReversed", clip.isReversed);

    // Beat detection
    obj->setProperty("autoDetectBeats", clip.autoDetectBeats);
    obj->setProperty("beatSensitivity", clip.beatSensitivity);

    // Channels
    obj->setProperty("leftChannelActive", clip.leftChannelActive);
    obj->setProperty("rightChannelActive", clip.rightChannelActive);

    // Auto-tempo / Musical mode
    obj->setProperty("autoTempo", clip.autoTempo);

    // MIDI offset
    if (clip.midiOffset != 0.0)
        obj->setProperty("midiOffset", clip.midiOffset);
    if (clip.midiTrimOffset != 0.0)
        obj->setProperty("midiTrimOffset", clip.midiTrimOffset);

    // Groove/Shuffle/Swing
    if (clip.grooveTemplate.isNotEmpty())
        obj->setProperty("grooveTemplate", clip.grooveTemplate);
    if (clip.grooveStrength > 0.0f)
        obj->setProperty("grooveStrength", clip.grooveStrength);

    if (clip.isAudio() && clip.audio().source.filePath.isNotEmpty()) {
        auto* audioObj = new juce::DynamicObject();

        auto* sourceObj = new juce::DynamicObject();
        sourceObj->setProperty("filePath", clip.audio().source.filePath);
        sourceObj->setProperty("durationSeconds", clip.audio().source.durationSeconds);
        audioObj->setProperty("source", juce::var(sourceObj));

        auto* interpretationObj = new juce::DynamicObject();
        interpretationObj->setProperty("bpm", clip.audio().interpretation.bpm);
        interpretationObj->setProperty("totalBeats", clip.audio().interpretation.totalBeats);
        interpretationObj->setProperty("totalBeatsLocked",
                                       clip.audio().interpretation.totalBeatsLocked);
        audioObj->setProperty("interpretation", juce::var(interpretationObj));

        auto* playbackObj = new juce::DynamicObject();
        playbackObj->setProperty("offsetSeconds", clip.offset);
        playbackObj->setProperty("offsetBeats", clip.offsetBeats);
        playbackObj->setProperty("loopStartSeconds", clip.loopStart);
        playbackObj->setProperty("loopLengthSeconds", clip.loopLength);
        playbackObj->setProperty("loopStartBeats", clip.loopStartBeats);
        playbackObj->setProperty("loopLengthBeats", clip.loopLengthBeats);
        playbackObj->setProperty("speedRatio", clip.speedRatio);
        audioObj->setProperty("playback", juce::var(playbackObj));

        if (clip.warpEnabled) {
            audioObj->setProperty("warpEnabled", clip.warpEnabled);

            // Serialize warp markers
            if (!clip.warpMarkers.empty()) {
                juce::Array<juce::var> warpArray;
                for (const auto& wm : clip.warpMarkers) {
                    auto* wmObj = new juce::DynamicObject();
                    wmObj->setProperty("sourceTime", wm.sourceTime);
                    wmObj->setProperty("warpTime", wm.warpTime);
                    warpArray.add(juce::var(wmObj));
                }
                audioObj->setProperty("warpMarkers", warpArray);
            }
        }
        if (clip.analogPitch) {
            audioObj->setProperty("analogPitch", clip.analogPitch);
        }
        if (clip.timeStretchMode != 0) {
            audioObj->setProperty("timeStretchMode", clip.timeStretchMode);
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
                                            double projectTempo) {
    if (!json.isObject()) {
        lastError_ = "Clip data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outClip.id = obj->getProperty("id");
    outClip.trackId = obj->getProperty("trackId");
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
    if (placementObj == nullptr) {
        lastError_ = "Clip is missing placement";
        return false;
    }
    outClip.setPlacementBeats(placementObj->getProperty("startBeat"),
                              placementObj->getProperty("lengthBeats"));
    outClip.deriveTimesFromBeats(projectTempo);

    // Loop settings
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

    // Per-clip mix
    outClip.volumeDB = static_cast<float>(static_cast<double>(obj->getProperty("volumeDB")));
    outClip.gainDB = static_cast<float>(static_cast<double>(obj->getProperty("gainDB")));
    outClip.pan = static_cast<float>(static_cast<double>(obj->getProperty("pan")));

    // Fades
    outClip.fadeIn = obj->getProperty("fadeIn");
    outClip.fadeOut = obj->getProperty("fadeOut");
    outClip.fadeInType = obj->getProperty("fadeInType");
    outClip.fadeOutType = obj->getProperty("fadeOutType");
    outClip.fadeInBehaviour = obj->getProperty("fadeInBehaviour");
    outClip.fadeOutBehaviour = obj->getProperty("fadeOutBehaviour");
    outClip.autoCrossfade = static_cast<bool>(obj->getProperty("autoCrossfade"));
    outClip.launchFadeSamples = obj->getProperty("launchFadeSamples");

    // Pitch
    outClip.pitchChange = static_cast<float>(static_cast<double>(obj->getProperty("pitchChange")));
    outClip.transpose = obj->getProperty("transpose");
    outClip.autoPitch = static_cast<bool>(obj->getProperty("autoPitch"));
    outClip.autoPitchMode = obj->getProperty("autoPitchMode");

    // Playback
    outClip.isReversed = static_cast<bool>(obj->getProperty("isReversed"));

    // Beat detection
    outClip.autoDetectBeats = static_cast<bool>(obj->getProperty("autoDetectBeats"));
    outClip.beatSensitivity =
        static_cast<float>(static_cast<double>(obj->getProperty("beatSensitivity")));

    // Channels
    outClip.leftChannelActive = static_cast<bool>(obj->getProperty("leftChannelActive"));
    outClip.rightChannelActive = static_cast<bool>(obj->getProperty("rightChannelActive"));

    // Auto-tempo / Musical mode
    outClip.autoTempo = static_cast<bool>(obj->getProperty("autoTempo"));

    // MIDI offset
    outClip.midiOffset = obj->getProperty("midiOffset");
    outClip.midiTrimOffset = obj->getProperty("midiTrimOffset");

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

    if (outClip.isAudio() && audioObj != nullptr) {
        auto* sourceObj = audioObj->getProperty("source").getDynamicObject();
        auto* interpretationObj = audioObj->getProperty("interpretation").getDynamicObject();
        auto* playbackObj = audioObj->getProperty("playback").getDynamicObject();

        if (sourceObj == nullptr || interpretationObj == nullptr || playbackObj == nullptr) {
            lastError_ = "Audio clip is missing source, interpretation, or playback";
            return false;
        }

        outClip.audio().source.filePath = sourceObj->getProperty("filePath").toString();
        outClip.audio().source.durationSeconds = sourceObj->getProperty("durationSeconds");
        outClip.audio().interpretation.bpm = interpretationObj->getProperty("bpm");
        outClip.audio().interpretation.totalBeats = interpretationObj->getProperty("totalBeats");
        outClip.audio().interpretation.totalBeatsLocked =
            static_cast<bool>(interpretationObj->getProperty("totalBeatsLocked"));

        outClip.offset = playbackObj->getProperty("offsetSeconds");
        outClip.offsetBeats = playbackObj->getProperty("offsetBeats");
        outClip.loopStart = playbackObj->getProperty("loopStartSeconds");
        outClip.loopLength = playbackObj->getProperty("loopLengthSeconds");
        outClip.loopStartBeats = playbackObj->getProperty("loopStartBeats");
        outClip.loopLengthBeats = playbackObj->getProperty("loopLengthBeats");
        outClip.speedRatio = playbackObj->getProperty("speedRatio");
        if (outClip.speedRatio <= 0.0)
            outClip.speedRatio = 1.0;
        outClip.warpEnabled = static_cast<bool>(audioObj->getProperty("warpEnabled"));
        outClip.analogPitch = static_cast<bool>(audioObj->getProperty("analogPitch"));
        outClip.timeStretchMode = audioObj->getProperty("timeStretchMode");

        // Warp markers
        auto warpMarkersVar = audioObj->getProperty("warpMarkers");
        if (warpMarkersVar.isArray()) {
            auto* arr = warpMarkersVar.getArray();
            for (const auto& wmVar : *arr) {
                if (auto* wmObj = wmVar.getDynamicObject()) {
                    ClipInfo::WarpMarker wm;
                    wm.sourceTime = wmObj->getProperty("sourceTime");
                    wm.warpTime = wmObj->getProperty("warpTime");
                    outClip.warpMarkers.push_back(wm);
                }
            }
        }
    } else if (outClip.isAudio() && legacyAudioSourceObj != nullptr) {
        outClip.audio().source.filePath = legacyAudioSourceObj->getProperty("filePath").toString();
        outClip.offset = legacyAudioSourceObj->getProperty("offsetSeconds");
        outClip.offsetBeats = legacyAudioSourceObj->getProperty("offsetBeats");
        outClip.loopStart = legacyAudioSourceObj->getProperty("loopStartSeconds");
        outClip.loopLength = legacyAudioSourceObj->getProperty("loopLengthSeconds");
        outClip.loopStartBeats = legacyAudioSourceObj->getProperty("loopStartBeats");
        outClip.loopLengthBeats = legacyAudioSourceObj->getProperty("loopLengthBeats");
        outClip.speedRatio = legacyAudioSourceObj->getProperty("speedRatio");
        if (outClip.speedRatio <= 0.0)
            outClip.speedRatio = 1.0;

        outClip.audio().interpretation.totalBeats =
            legacyAudioSourceObj->getProperty("sourceNumBeats");
        outClip.audio().interpretation.bpm = legacyAudioSourceObj->getProperty("sourceBPM");
        if (outClip.audio().source.durationSeconds <= 0.0 &&
            outClip.audio().interpretation.totalBeats > 0.0 &&
            outClip.audio().interpretation.bpm > 0.0) {
            outClip.audio().source.durationSeconds = outClip.audio().interpretation.totalBeats *
                                                     60.0 / outClip.audio().interpretation.bpm;
        }

        outClip.warpEnabled = static_cast<bool>(legacyAudioSourceObj->getProperty("warpEnabled"));
        outClip.analogPitch = static_cast<bool>(legacyAudioSourceObj->getProperty("analogPitch"));
        outClip.timeStretchMode = legacyAudioSourceObj->getProperty("timeStretchMode");

        auto warpMarkersVar = legacyAudioSourceObj->getProperty("warpMarkers");
        if (warpMarkersVar.isArray()) {
            auto* arr = warpMarkersVar.getArray();
            for (const auto& wmVar : *arr) {
                if (auto* wmObj = wmVar.getDynamicObject()) {
                    ClipInfo::WarpMarker wm;
                    wm.sourceTime = wmObj->getProperty("sourceTime");
                    wm.warpTime = wmObj->getProperty("warpTime");
                    outClip.warpMarkers.push_back(wm);
                }
            }
        }
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
    return true;
}

juce::var ProjectSerializer::serializeMidiCCData(const MidiCCData& data) {
    auto* obj = new juce::DynamicObject();
    SER(controller);
    SER(value);
    SER(beatPosition);
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
    return true;
}

juce::var ProjectSerializer::serializeMidiPitchBendData(const MidiPitchBendData& data) {
    auto* obj = new juce::DynamicObject();
    SER(value);
    SER(beatPosition);
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
    return true;
}

}  // namespace magda
