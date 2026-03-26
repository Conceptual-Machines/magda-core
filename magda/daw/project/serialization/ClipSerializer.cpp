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
    obj->setProperty("type", static_cast<int>(clip.type));
    obj->setProperty("startTime", clip.startTime);
    obj->setProperty("length", clip.length);
    obj->setProperty("view", static_cast<int>(clip.view));
    obj->setProperty("loopEnabled", clip.loopEnabled);
    obj->setProperty("loopStart", clip.loopStart);
    obj->setProperty("loopLength", clip.loopLength);
    obj->setProperty("sceneIndex", clip.sceneIndex);
    obj->setProperty("launchMode", static_cast<int>(clip.launchMode));
    obj->setProperty("launchQuantize", static_cast<int>(clip.launchQuantize));

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

    // Auto-tempo / Musical mode & beat-based properties
    obj->setProperty("autoTempo", clip.autoTempo);
    obj->setProperty("startBeats", clip.startBeats);
    obj->setProperty("lengthBeats", clip.lengthBeats);
    obj->setProperty("loopStartBeats", clip.loopStartBeats);
    obj->setProperty("loopLengthBeats", clip.loopLengthBeats);
    obj->setProperty("offsetBeats", clip.offsetBeats);

    // Source metadata
    if (clip.sourceNumBeats > 0.0)
        obj->setProperty("sourceNumBeats", clip.sourceNumBeats);
    if (clip.sourceBPM > 0.0)
        obj->setProperty("sourceBPM", clip.sourceBPM);

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

    // Audio properties (TE-aligned model)
    if (clip.audioFilePath.isNotEmpty()) {
        obj->setProperty("audioFilePath", clip.audioFilePath);
        obj->setProperty("offset", clip.offset);
        obj->setProperty("speedRatio", clip.speedRatio);
        if (clip.warpEnabled) {
            obj->setProperty("warpEnabled", clip.warpEnabled);

            // Serialize warp markers
            if (!clip.warpMarkers.empty()) {
                juce::Array<juce::var> warpArray;
                for (const auto& wm : clip.warpMarkers) {
                    auto* wmObj = new juce::DynamicObject();
                    wmObj->setProperty("sourceTime", wm.sourceTime);
                    wmObj->setProperty("warpTime", wm.warpTime);
                    warpArray.add(juce::var(wmObj));
                }
                obj->setProperty("warpMarkers", warpArray);
            }
        }
        if (clip.analogPitch) {
            obj->setProperty("analogPitch", clip.analogPitch);
        }
        if (clip.timeStretchMode != 0) {
            obj->setProperty("timeStretchMode", clip.timeStretchMode);
        }
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
    outClip.type = static_cast<ClipType>(static_cast<int>(obj->getProperty("type")));
    outClip.startTime = obj->getProperty("startTime");
    outClip.length = obj->getProperty("length");
    // View type (backward compatible - defaults to Arrangement if missing)
    auto viewVar = obj->getProperty("view");
    if (!viewVar.isVoid()) {
        outClip.view = static_cast<ClipView>(static_cast<int>(viewVar));
    }
    // Loop settings
    outClip.loopEnabled = static_cast<bool>(obj->getProperty("loopEnabled"));
    outClip.loopStart = obj->getProperty("loopStart");
    outClip.loopLength = obj->getProperty("loopLength");
    outClip.sceneIndex = obj->getProperty("sceneIndex");

    // Launch properties
    outClip.launchMode = static_cast<LaunchMode>(static_cast<int>(obj->getProperty("launchMode")));
    outClip.launchQuantize =
        static_cast<LaunchQuantize>(static_cast<int>(obj->getProperty("launchQuantize")));

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

    // Auto-tempo / Musical mode & beat-based properties
    outClip.autoTempo = static_cast<bool>(obj->getProperty("autoTempo"));
    outClip.startBeats = obj->getProperty("startBeats");
    outClip.lengthBeats = obj->getProperty("lengthBeats");
    outClip.loopStartBeats = obj->getProperty("loopStartBeats");
    outClip.loopLengthBeats = obj->getProperty("loopLengthBeats");
    outClip.offsetBeats = obj->getProperty("offsetBeats");

    // Source metadata
    outClip.sourceNumBeats = obj->getProperty("sourceNumBeats");
    outClip.sourceBPM = obj->getProperty("sourceBPM");

    // MIDI offset
    outClip.midiOffset = obj->getProperty("midiOffset");
    outClip.midiTrimOffset = obj->getProperty("midiTrimOffset");

    // Groove/Shuffle/Swing
    outClip.grooveTemplate = obj->getProperty("grooveTemplate").toString();
    outClip.grooveStrength =
        static_cast<float>(static_cast<double>(obj->getProperty("grooveStrength")));

    // Audio properties
    auto audioFilePathVar = obj->getProperty("audioFilePath");
    if (!audioFilePathVar.isVoid()) {
        outClip.audioFilePath = audioFilePathVar.toString();
        outClip.offset = obj->getProperty("offset");
        outClip.speedRatio = obj->getProperty("speedRatio");
        if (outClip.speedRatio <= 0.0)
            outClip.speedRatio = 1.0;
        outClip.warpEnabled = static_cast<bool>(obj->getProperty("warpEnabled"));
        outClip.analogPitch = static_cast<bool>(obj->getProperty("analogPitch"));
        outClip.timeStretchMode = obj->getProperty("timeStretchMode");

        // Warp markers
        auto warpMarkersVar = obj->getProperty("warpMarkers");
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

    // MIDI clips: ensure beats are populated (backward compat with old project files)
    if (outClip.type == ClipType::MIDI) {
        if (outClip.lengthBeats <= 0.0 && projectTempo > 0.0) {
            outClip.startBeats = (outClip.startTime * projectTempo) / 60.0;
            outClip.lengthBeats = (outClip.length * projectTempo) / 60.0;
        }
        // Derive seconds cache from authoritative beats
        outClip.deriveTimesFromBeats(projectTempo);
    }

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
