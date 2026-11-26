// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "AudioEventWireNames.hpp"
#include "ProjectMigration.hpp"
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
    return {obj};
}

void readMidiCurveHandle(const ser::ObjectReader& in, MidiCurveHandle& handle) {
    in.optional("dx", handle.dx);
    in.optional("dy", handle.dy);
    in.optional("linked", handle.linked);
}

using audio_event_wire::toString;

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
    obj->setProperty("loopExtent", toString(event.loopExtent));
    obj->setProperty("loopLengthIntent", toString(event.loopLengthIntent));
    obj->setProperty("musicalLoopLengthBeats", event.musicalLoopLengthBeats);

    obj->setProperty("interpBpm", event.interpBpm);
    obj->setProperty("bpmFrom", toString(event.bpmFrom));
    obj->setProperty("interpTotalBeats", event.interpTotalBeats);
    obj->setProperty("beatsFrom", toString(event.beatsFrom));
    if (!event.keyRoot.empty())
        obj->setProperty("keyRoot", juce::String(event.keyRoot));
    if (!event.keyScale.empty())
        obj->setProperty("keyScale", juce::String(event.keyScale));

    obj->setProperty("playbackIntent", toString(event.playbackIntent));
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
    if (!event.labelRegions.empty()) {
        juce::Array<juce::var> labelArray;
        for (const auto& label : event.labelRegions) {
            auto* labelObj = new juce::DynamicObject();
            labelObj->setProperty("name", juce::String(label.name));
            labelObj->setProperty("colour", label.colour.toDisplayString(true));
            labelObj->setProperty("sourceStart", label.sourceStart);
            labelObj->setProperty("sourceEnd", label.sourceEnd);
            labelArray.add(juce::var(labelObj));
        }
        obj->setProperty("labelRegions", labelArray);
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

    if (!event.gainEnvelope.empty()) {
        juce::Array<juce::var> points;
        for (const auto& point : event.gainEnvelope.points)
            points.add(ProjectSerializer::serializeAutomationPoint(point));
        obj->setProperty("gainEnvelope", points);
    }

    return {obj};
}

/// Samples travel as decimal strings: juce::var has no 64-bit integer.
void readSamples(const ser::ObjectReader& in, const char* key, int64_t& out) {
    juce::String text;
    if (!in.required(key, text))
        return;
    if (text.isEmpty() || !text.trimCharactersAtStart("-").containsOnly("0123456789"))
        in.fail(key, "expected a sample count, found \"" + text + "\"");
    else
        out = text.getLargeIntValue();
}

/// An event enum saved as its wire name; an unknown name is an issue.
template <typename E>
void readWireName(const ser::ObjectReader& in, const char* key, E& out,
                  std::optional<E> (*parse)(const juce::String&), bool required) {
    juce::String text;
    if (!(required ? in.required(key, text) : in.optional(key, text)))
        return;
    if (const auto value = parse(text))
        out = *value;
    else
        in.fail(key, "unknown value \"" + text + "\"");
}

void readAudioEvent(const ser::ObjectReader& in, AudioEvent& event) {
    using namespace audio_event_wire;
    in.optional("id", event.id);
    in.optional("sourceId", event.sourceId);
    in.required("startBeat", event.startBeat);
    in.required("lengthBeats", event.lengthBeats);

    readSamples(in, "anchorSamples", event.sourceAnchorSamples);
    readSamples(in, "loopStartSamples", event.loopStartSamples);
    LoopLengthState loopLength{.samples = 0,
                               .extent = RegionExtent::WholeSource,
                               .intent = LoopLengthIntent::Source,
                               .musicalBeats = 0.0};
    readSamples(in, "loopLengthSamples", loopLength.samples);
    readWireName(in, "loopExtent", loopLength.extent, regionExtentFrom, true);
    // An intent this build does not know reads as Source, which drops the musical length.
    if (juce::String text; in.optional("loopLengthIntent", text))
        loopLength.intent = loopLengthIntentFrom(text).value_or(LoopLengthIntent::Source);
    in.optional("musicalLoopLengthBeats", loopLength.musicalBeats);

    in.optional("interpBpm", event.interpBpm);
    in.optional("interpTotalBeats", event.interpTotalBeats);
    readWireName(in, "bpmFrom", event.bpmFrom, provenanceFrom, true);
    readWireName(in, "beatsFrom", event.beatsFrom, provenanceFrom, true);
    in.optional("keyRoot", event.keyRoot);
    in.optional("keyScale", event.keyScale);
    event.restoreLoopLength(loopLength);

    // autoTempo is restored as stored, not re-resolved: load must not move it.
    in.optional("autoTempo", event.autoTempo);
    readWireName(in, "playbackIntent", event.playbackIntent, playbackIntentFrom, true);
    if (in.optional("speedRatio", event.speedRatio) && event.speedRatio <= 0.0)
        event.speedRatio = 1.0;
    in.optional("timeStretchMode", event.timeStretchMode);
    in.optional("warpEnabled", event.warpEnabled);
    in.forEachObject("warpMarkers", [&event](const ser::ObjectReader& item) {
        WarpMarker marker;
        item.required("sourceTime", marker.sourceTime);
        item.required("warpTime", marker.warpTime);
        event.warpMarkers.push_back(marker);
    });
    in.forEachObject("labelRegions", [&event](const ser::ObjectReader& item) {
        LabelRegion label;
        item.required("name", label.name);
        item.required("colour", label.colour);
        item.required("sourceStart", label.sourceStart);
        item.required("sourceEnd", label.sourceEnd);
        event.labelRegions.push_back(std::move(label));
    });

    in.optional("autoPitch", event.autoPitch);
    in.optional("analogPitch", event.analogPitch);
    in.optional("autoPitchMode", event.autoPitchMode);
    in.optional("pitchChange", event.pitchChange);
    in.optional("transpose", event.transpose);
    in.optional("reversed", event.reversed);
    in.optional("autoDetectBeats", event.autoDetectBeats);
    in.optional("beatSensitivity", event.beatSensitivity);
    in.optional("leftChannelActive", event.leftChannelActive);
    in.optional("rightChannelActive", event.rightChannelActive);

    in.optional("gainDB", event.gainDB);
    in.optional("fadeInSeconds", event.fadeInSeconds);
    in.optional("fadeOutSeconds", event.fadeOutSeconds);
    in.optional("fadeInType", event.fadeInType);
    in.optional("fadeOutType", event.fadeOutType);
    in.optional("fadeInBehaviour", event.fadeInBehaviour);
    in.optional("fadeOutBehaviour", event.fadeOutBehaviour);
}

/** @brief Takes and comp sections, which every audio layout kept the same way. */
void readAudioTakesAndComp(const ser::ObjectReader& audio, ClipInfo& outClip) {
    auto& content = outClip.audio();
    if (const auto window = audio.optionalObject("envelopeWindow")) {
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        window->required("startBeat", startBeat);
        window->required("lengthBeats", lengthBeats);
        if (std::isfinite(startBeat) && std::isfinite(lengthBeats) && lengthBeats > 0.0)
            content.envelopeWindow = ClipPlacement{startBeat, lengthBeats};
    }

    audio.forEachObject("takes", [&content](const ser::ObjectReader& item) {
        AudioTake take;
        item.required("filePath", take.filePath);
        item.required("durationSeconds", take.durationSeconds);
        content.takes.push_back(take);
    });
    if (!content.takes.empty() && audio.optional("currentTakeIndex", content.currentTakeIndex))
        content.currentTakeIndex =
            juce::jlimit(0, static_cast<int>(content.takes.size()) - 1, content.currentTakeIndex);

    audio.forEachObject("comp", [&content](const ser::ObjectReader& item) {
        CompSection section;
        item.required("startSeconds", section.startSeconds);
        item.required("endSeconds", section.endSeconds);
        item.required("takeIndex", section.takeIndex);
        content.comp.push_back(section);
    });
    bool compActive = false;
    audio.optional("compActive", compActive);
    content.compActive = !content.comp.empty() && compActive;
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
    if (clip.chordLaneVisible)
        obj->setProperty("chordLaneVisible", *clip.chordLaneVisible);

    // Per-clip mix
    obj->setProperty("volumeDB", clip.volumeDB);
    obj->setProperty("gainDB", clip.gainDB);
    obj->setProperty("pan", clip.pan);

    // Crossfade with the neighbouring clip; per-event fades live on the event.
    obj->setProperty("autoCrossfade", clip.autoCrossfade);
    if (clip.gainEnvelopeShown)
        obj->setProperty("gainEnvelopeShown", true);
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
        if (midi.sourceFilePath.isNotEmpty() || !midi.takes.empty() || midi.nextEventId != 1) {
            auto* midiObj = new juce::DynamicObject();
            if (midi.sourceFilePath.isNotEmpty())
                midiObj->setProperty("sourceFilePath", midi.sourceFilePath);
            midiObj->setProperty("nextEventId", midi.nextEventId);
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
                    juce::Array<juce::var> pressure;
                    for (const auto& event : take.channelPressure)
                        pressure.add(serializeMidiChannelPressureData(event));
                    juce::Array<juce::var> polyAftertouch;
                    for (const auto& event : take.polyAftertouch)
                        polyAftertouch.add(serializeMidiPolyAftertouchData(event));
                    takeObj->setProperty("notes", juce::var(notes));
                    takeObj->setProperty("cc", juce::var(ccs));
                    takeObj->setProperty("pitchBend", juce::var(pbs));
                    takeObj->setProperty("channelPressure", juce::var(pressure));
                    takeObj->setProperty("polyAftertouch", juce::var(polyAftertouch));
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

    if (clip.isMidi() && !(clip.timeBend == sdk::sequencer::TimeBend{}))
        obj->setProperty("timeBend", juce::String(sdk::sequencer::timeBendToText(clip.timeBend)));

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

        if (const auto& window = clip.audio().envelopeWindow) {
            auto* windowObj = new juce::DynamicObject();
            windowObj->setProperty("startBeat", window->startBeat);
            windowObj->setProperty("lengthBeats", window->lengthBeats);
            audioObj->setProperty("envelopeWindow", juce::var(windowObj));
        }

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

    if (!clip.midiChannelPressureData.empty()) {
        juce::Array<juce::var> pressureArray;
        for (const auto& pressure : clip.midiChannelPressureData)
            pressureArray.add(serializeMidiChannelPressureData(pressure));
        obj->setProperty("midiChannelPressureData", juce::var(pressureArray));
    }

    if (!clip.midiPolyAftertouchData.empty()) {
        juce::Array<juce::var> aftertouchArray;
        for (const auto& aftertouch : clip.midiPolyAftertouchData)
            aftertouchArray.add(serializeMidiPolyAftertouchData(aftertouch));
        obj->setProperty("midiPolyAftertouchData", juce::var(aftertouchArray));
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

    return {obj};
}

bool ProjectSerializer::deserializeClipInfo(const juce::var& json, ClipInfo& outClip,
                                            double projectTempo,
                                            std::vector<Source>* legacySources) {
    outClip = {};
    return ser::decodeDocument(json, "clip", lastError_, [&](const ser::ObjectReader& in) {
        read(in, outClip, projectTempo, legacySources);
    });
}

void ProjectSerializer::read(const ser::ObjectReader& raw, ClipInfo& outClip, double projectTempo,
                             std::vector<Source>* legacySources) {
    const auto in = project_migration::upgradeClip(raw, projectTempo);

    in.required("id", outClip.id);
    in.required("trackId", outClip.trackId);
    in.optional("linkGroupId", outClip.linkGroupId);
    in.required("name", outClip.name);
    in.required("colour", outClip.colour);
    int rawClipType = 0;
    if (!in.required("type", rawClipType))
        return;
    if (rawClipType == static_cast<int>(ClipType::Audio))
        outClip.setAudioContent();
    else if (rawClipType == static_cast<int>(ClipType::MIDI))
        outClip.setMidiContent();
    else
        return in.fail("type", "unknown clip type " + juce::String(rawClipType));
    in.required("view", outClip.view);

    if (const auto placement = in.object("placement"); placement.isValid()) {
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        placement.required("startBeat", startBeat);
        placement.required("lengthBeats", lengthBeats);
        outClip.setPlacementBeats(startBeat, lengthBeats);
    }

    // Each absent in projects saved before it existed, where the default is how they played:
    // enabled (#1736), creation order on ties (#2003), and the top clip winning an overlap.
    in.optional("enabled", outClip.enabled);
    in.optional("stackOrder", outClip.stackOrder);
    in.optional("overlapPlaysBoth", outClip.overlapPlaysBoth);

    in.required("loopEnabled", outClip.loopEnabled);
    in.required("sceneIndex", outClip.sceneIndex);
    in.required("launchMode", outClip.launchMode);
    in.required("launchQuantize", outClip.launchQuantize);
    in.optional("followAction", outClip.followAction);
    if (in.optional("followActionDelayBeats", outClip.followActionDelayBeats))
        outClip.followActionDelayBeats = juce::jmax(0.0, outClip.followActionDelayBeats);
    if (in.optional("followActionLoopCount", outClip.followActionLoopCount))
        outClip.followActionLoopCount = juce::jmax(1, outClip.followActionLoopCount);

    in.optional("gridAutoGrid", outClip.gridAutoGrid);
    in.optional("gridNumerator", outClip.gridNumerator);
    in.optional("gridDenominator", outClip.gridDenominator);
    in.optional("gridSnapEnabled", outClip.gridSnapEnabled);
    if (bool chordLaneVisible = false; in.optional("chordLaneVisible", chordLaneVisible))
        outClip.chordLaneVisible = chordLaneVisible;
    if (in.optional("midiEditorRowHeight", outClip.midiEditorRowHeight))
        outClip.midiEditorRowHeight =
            juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT, ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT,
                         outClip.midiEditorRowHeight);

    in.required("volumeDB", outClip.volumeDB);
    in.required("gainDB", outClip.gainDB);
    in.required("pan", outClip.pan);
    in.required("autoCrossfade", outClip.autoCrossfade);
    in.optional("gainEnvelopeShown", outClip.gainEnvelopeShown);
    in.optional("launchFadeSamples", outClip.launchFadeSamples);

    in.optional("midiOffset", outClip.midiOffset);
    in.optional("midiTrimOffset", outClip.midiTrimOffset);
    if (outClip.isMidi()) {
        in.optional("loopStartBeats", outClip.loopStartBeats);
        in.optional("loopLengthBeats", outClip.loopLengthBeats);
        if (const auto midi = in.optionalObject("midi"))
            readMidiContent(*midi, outClip.midi());
    }

    if (juce::String text; in.optional("timeBend", text)) {
        if (auto bend = sdk::sequencer::timeBendFromText(text.toStdString()))
            outClip.timeBend = *bend;
        else
            in.fail("timeBend", "not a time bend: \"" + text + "\"");
    }

    if (!outClip.isAudio()) {
        if (in.has("audio") || in.has("audioSource"))
            in.fail("audio", "a MIDI clip cannot hold audio");
    } else if (project_migration::isLegacyAudioClip(in)) {
        project_migration::readLegacyAudioClip(in, outClip, projectTempo, legacySources);
    } else if (const auto audio = in.optionalObject("audio")) {
        auto& content = outClip.audio();
        audio->forEachObject("events", [&content](const ser::ObjectReader& item) {
            AudioEvent event;
            readAudioEvent(item, event);
            item.forEachObject("gainEnvelope", [&event](const ser::ObjectReader& point) {
                AutomationPoint envelopePoint;
                read(point, envelopePoint);
                event.gainEnvelope.points.push_back(envelopePoint);
            });
            std::sort(event.gainEnvelope.points.begin(), event.gainEnvelope.points.end());
            if (event.id == INVALID_EVENT_ID)
                event.id = content.nextEventId++;
            content.events.push_back(std::move(event));
        });
        if (int nextEventId = 0; audio->optional("nextEventId", nextEventId))
            content.nextEventId = juce::jmax(content.nextEventId, nextEventId);
        for (const auto& event : content.events)
            content.nextEventId = juce::jmax(content.nextEventId, event.id + 1);
    }
    if (outClip.isAudio())
        if (const auto audio = in.optionalObject("audio"))
            readAudioTakesAndComp(*audio, outClip);

    readMidiEvents(in, outClip.midiNotes, outClip.midiCCData, outClip.midiPitchBendData,
                   outClip.midiChannelPressureData, outClip.midiPolyAftertouchData,
                   {"midiNotes", "midiCCData", "midiPitchBendData", "midiChannelPressureData",
                    "midiPolyAftertouchData"});

    in.forEachObject("chordAnnotations", [&outClip](const ser::ObjectReader& item) {
        ClipInfo::ChordAnnotation annotation;
        item.required("beatPosition", annotation.beatPosition);
        item.required("lengthBeats", annotation.lengthBeats);
        item.required("chordName", annotation.chordName);
        item.optional("chordGroup", annotation.chordGroup);
        outClip.chordAnnotations.push_back(annotation);
    });
    in.optional("nextChordGroupId", outClip.nextChordGroupId);

    outClip.ensureMidiEventIds();
}

void ProjectSerializer::readMidiContent(const ser::ObjectReader& in, MidiClipModel& midi) {
    in.optional("sourceFilePath", midi.sourceFilePath);
    in.optional("nextEventId", midi.nextEventId);

    in.forEachObject("takes", [&midi](const ser::ObjectReader& item) {
        MidiTake take;
        readMidiEvents(item, take.notes, take.cc, take.pitchBend, take.channelPressure,
                       take.polyAftertouch,
                       {"notes", "cc", "pitchBend", "channelPressure", "polyAftertouch"});
        midi.takes.push_back(std::move(take));
    });
    if (!midi.takes.empty() && in.optional("currentTakeIndex", midi.currentTakeIndex))
        midi.currentTakeIndex =
            juce::jlimit(0, static_cast<int>(midi.takes.size()) - 1, midi.currentTakeIndex);

    in.forEachObject("comp", [&midi](const ser::ObjectReader& item) {
        MidiCompSection section;
        item.required("startBeat", section.startBeat);
        item.required("endBeat", section.endBeat);
        item.required("takeIndex", section.takeIndex);
        midi.comp.push_back(section);
    });
    bool compActive = false;
    in.optional("compActive", compActive);
    midi.compActive = !midi.comp.empty() && compActive;
}

void ProjectSerializer::readMidiEvents(const ser::ObjectReader& in, std::vector<MidiNote>& notes,
                                       std::vector<MidiCCData>& cc,
                                       std::vector<MidiPitchBendData>& pitchBend,
                                       std::vector<MidiChannelPressureData>& pressure,
                                       std::vector<MidiPolyAftertouchData>& aftertouch,
                                       const MidiEventKeys& keys) {
    const auto readInto = [&in](const char* key, auto& out) {
        in.forEachObject(key, [&out](const ser::ObjectReader& item) {
            typename std::remove_reference_t<decltype(out)>::value_type value;
            read(item, value);
            out.push_back(value);
        });
    };
    readInto(keys.notes, notes);
    readInto(keys.cc, cc);
    readInto(keys.pitchBend, pitchBend);
    readInto(keys.channelPressure, pressure);
    readInto(keys.polyAftertouch, aftertouch);
}

namespace {

/// A timbre or pressure lane as written, or nothing when the lane is empty.
void writeNoteExpression(juce::DynamicObject& note, const juce::Identifier& key,
                         const std::vector<MidiNoteExpressionPoint>& lane) {
    if (lane.empty())
        return;

    juce::Array<juce::var> points;
    for (const auto& p : lane) {
        auto* pObj = new juce::DynamicObject();
        pObj->setProperty("beat", p.beat);
        pObj->setProperty("value", p.value);
        if (p.tension != 0.0)
            pObj->setProperty("tension", p.tension);
        points.add(juce::var(pObj));
    }
    note.setProperty(key, juce::var(points));
}

std::vector<MidiNoteExpressionPoint> readNoteExpression(const ser::ObjectReader& note,
                                                        const juce::Identifier& key) {
    std::vector<MidiNoteExpressionPoint> lane;
    note.forEachObject(key, [&lane](const ser::ObjectReader& item) {
        MidiNoteExpressionPoint point;
        item.required("beat", point.beat);
        item.required("value", point.value);
        item.optional("tension", point.tension);
        lane.push_back(point);
    });
    return lane;
}

}  // namespace

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
            // Only when bent (#2198), so a project full of straight glides is
            // written exactly as it was before the field existed.
            if (p.tension != 0.0)
                pObj->setProperty("tension", p.tension);
            points.add(juce::var(pObj));
        }
        obj->setProperty("pitchExpression", juce::var(points));
    }
    writeNoteExpression(*obj, "timbreExpression", data.timbreExpression);
    writeNoteExpression(*obj, "pressureExpression", data.pressureExpression);
    if (data.keyswitch)
        obj->setProperty("keyswitch", true);
    if (data.id != INVALID_EVENT_ID)
        obj->setProperty("id", data.id);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MidiNote& data) {
    DESER(noteNumber);
    DESER(velocity);
    DESER(startBeat);
    DESER(lengthBeats);
    DESER_OPT(chordGroup);
    DESER_OPT(keyswitch);
    DESER_OPT(id);
    in.forEachObject("pitchExpression", [&data](const ser::ObjectReader& item) {
        MidiPitchExpressionPoint point;
        item.required("beat", point.beat);
        item.required("semitones", point.semitones);
        // Absent before #2198, and in any straight segment since.
        item.optional("tension", point.tension);
        data.pitchExpression.push_back(point);
    });
    data.timbreExpression = readNoteExpression(in, "timbreExpression");
    data.pressureExpression = readNoteExpression(in, "pressureExpression");
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
    if (data.id != INVALID_EVENT_ID)
        obj->setProperty("id", data.id);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MidiCCData& data) {
    DESER(controller);
    DESER(value);
    DESER(beatPosition);
    DESER_OPT(curveType);
    DESER_OPT(tension);
    if (const auto handle = in.optionalObject("inHandle"))
        readMidiCurveHandle(*handle, data.inHandle);
    if (const auto handle = in.optionalObject("outHandle"))
        readMidiCurveHandle(*handle, data.outHandle);
    DESER_OPT(id);
}

juce::var ProjectSerializer::serializeMidiPitchBendData(const MidiPitchBendData& data) {
    auto* obj = new juce::DynamicObject();
    SER(value);
    SER(beatPosition);
    SER(curveType);
    SER(tension);
    obj->setProperty("inHandle", serializeMidiCurveHandle(data.inHandle));
    obj->setProperty("outHandle", serializeMidiCurveHandle(data.outHandle));
    if (data.id != INVALID_EVENT_ID)
        obj->setProperty("id", data.id);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MidiPitchBendData& data) {
    DESER(value);
    DESER(beatPosition);
    DESER_OPT(curveType);
    DESER_OPT(tension);
    if (const auto handle = in.optionalObject("inHandle"))
        readMidiCurveHandle(*handle, data.inHandle);
    if (const auto handle = in.optionalObject("outHandle"))
        readMidiCurveHandle(*handle, data.outHandle);
    DESER_OPT(id);
}

juce::var ProjectSerializer::serializeMidiChannelPressureData(const MidiChannelPressureData& data) {
    auto* obj = new juce::DynamicObject();
    SER(value);
    SER(beatPosition);
    if (data.id != INVALID_EVENT_ID)
        obj->setProperty("id", data.id);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MidiChannelPressureData& data) {
    DESER(value);
    DESER(beatPosition);
    DESER_OPT(id);
}

juce::var ProjectSerializer::serializeMidiPolyAftertouchData(const MidiPolyAftertouchData& data) {
    auto* obj = new juce::DynamicObject();
    SER(noteNumber);
    SER(value);
    SER(beatPosition);
    if (data.id != INVALID_EVENT_ID)
        obj->setProperty("id", data.id);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, MidiPolyAftertouchData& data) {
    DESER(noteNumber);
    DESER(value);
    DESER(beatPosition);
    DESER_OPT(id);
}

}  // namespace magda
