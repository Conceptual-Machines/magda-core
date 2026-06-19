#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectInfo.hpp"
#include "magda/daw/project/serialization/DawProjectArchive.hpp"
#include "magda/daw/project/serialization/DawProjectValidator.hpp"
#include "magda/daw/project/serialization/DawProjectXmlAdapter.hpp"
#include "magda/daw/project/serialization/NativeProjectDocumentAdapter.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

using namespace magda;

namespace {

void clearProjectManagers() {
    AutomationManager::getInstance().clearAll();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
}

juce::File createTempDawProjectFile() {
    return juce::File::getCurrentWorkingDirectory().getNonexistentChildFile(
        "magda-dawproject-archive", ".dawproject");
}

juce::String readZipTextEntry(juce::ZipFile& zip, const juce::String& entryName) {
    const auto index = zip.getIndexOfFileName(entryName, false);
    REQUIRE(index >= 0);
    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
    REQUIRE(stream != nullptr);
    return stream->readEntireStreamAsString();
}

}  // namespace

TEST_CASE("NativeProjectDocumentAdapter captures current manager state",
          "[project][serialization][document]") {
    clearProjectManagers();

    ProjectInfo info;
    info.name = "Adapter Capture";
    info.tempo = 132.0;

    auto trackId = TrackManager::getInstance().createTrack("Keys", TrackType::Audio);
    auto clipId = ClipManager::getInstance().createMidiClipBeats(trackId, 4.0, 2.0);
    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->name = "Phrase";
    clip->midiNotes.push_back(MidiNote{64, 96, 0.0, 0.5});

    auto document = NativeProjectDocumentAdapter::captureCurrentProject(info);

    REQUIRE(document.info.name == "Adapter Capture");
    REQUIRE(document.info.tempo == 132.0);
    REQUIRE(document.tracks.size() == 2);
    REQUIRE(document.tracks[0].name == "Keys");
    REQUIRE(document.tracks[1].id == MASTER_TRACK_ID);
    REQUIRE(document.tracks[1].type == TrackType::Master);
    REQUIRE(document.clips.size() == 1);
    REQUIRE(document.clips[0].name == "Phrase");
    REQUIRE(document.clips[0].midiNotes.size() == 1);

    clearProjectManagers();
}

TEST_CASE("DawProjectXmlAdapter roundtrips transport tracks and arrangement clips",
          "[project][serialization][dawproject]") {
    ProjectDocument document;
    document.info.name = "DAWproject Test";
    document.info.version = "0.test";
    document.info.tempo = 149.0;
    document.info.timeSignatureNumerator = 7;
    document.info.timeSignatureDenominator = 8;

    TrackInfo track;
    track.id = 10;
    track.name = "Bass";
    track.colour = juce::Colour(0xffa2eabf);
    track.volume = 0.75f;
    track.pan = -0.25f;
    document.tracks.push_back(track);

    ClipInfo midiClip;
    midiClip.id = 20;
    midiClip.trackId = track.id;
    midiClip.name = "Hook";
    midiClip.setMidiContent();
    midiClip.setPlacementBeats(1.0, 4.0);
    midiClip.midiNotes.push_back(MidiNote{65, 100, 0.0, 0.25});
    midiClip.midiNotes.push_back(MidiNote{53, 80, 1.5, 2.5});
    // A 2-beat pattern looping across the 4-beat clip (beats-domain loop).
    midiClip.loopEnabled = true;
    midiClip.loopStartBeats = 0.0;
    midiClip.loopLengthBeats = 2.0;
    document.clips.push_back(midiClip);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<Project"));
    REQUIRE(xml.contains("version=\"1.0\""));
    // A track carrying a MIDI clip is a "notes" track even with no instrument,
    // so importing DAWs (Bitwig) don't mistake it for an audio track.
    REQUIRE(xml.contains("contentType=\"notes\""));
    REQUIRE(xml.contains("<Notes"));
    REQUIRE(xml.contains("key=\"65\""));

    juce::String validationError;
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));
    REQUIRE(validationError.isEmpty());

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    REQUIRE(error.isEmpty());

    REQUIRE(imported.info.tempo == 149.0);
    REQUIRE(imported.info.timeSignatureNumerator == 7);
    REQUIRE(imported.info.timeSignatureDenominator == 8);
    REQUIRE(imported.tracks.size() == 1);
    REQUIRE(imported.tracks[0].name == "Bass");
    REQUIRE(imported.clips.size() == 1);
    REQUIRE(imported.clips[0].name == "Hook");
    REQUIRE(imported.clips[0].placement.startBeat == 1.0);
    REQUIRE(imported.clips[0].placement.lengthBeats == 4.0);
    REQUIRE(imported.clips[0].midiNotes.size() == 2);
    REQUIRE(imported.clips[0].midiNotes[0].noteNumber == 65);

    // MIDI loop region round-trips in beats.
    REQUIRE(xml.contains("loopEnd=\"2.0\""));
    REQUIRE(imported.clips[0].loopEnabled);
    REQUIRE(imported.clips[0].loopStartBeats == 0.0);
    REQUIRE(imported.clips[0].loopLengthBeats == 2.0);
}

TEST_CASE("DawProjectXmlAdapter roundtrips track volume and pan automation",
          "[project][serialization][dawproject][automation]") {
    ProjectDocument document;
    document.info.name = "Automation Test";
    document.info.version = "0.automation";
    document.info.tempo = 120.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Automated";
    track.volume = 1.0f;
    track.pan = 0.0f;
    document.tracks.push_back(track);

    AutomationLaneInfo volumeLane;
    volumeLane.id = 1;
    volumeLane.target = ControlTarget::trackVolume(track.id);
    volumeLane.type = AutomationLaneType::Absolute;
    volumeLane.paramName = "Volume";
    volumeLane.absolutePoints.push_back(AutomationPoint{1, 0.0, 0.75, AutomationCurveType::Linear});
    volumeLane.absolutePoints.push_back(AutomationPoint{2, 4.0, 0.5, AutomationCurveType::Step});
    document.automationLanes.push_back(volumeLane);

    AutomationLaneInfo panLane;
    panLane.id = 2;
    panLane.target = ControlTarget::trackPan(track.id);
    panLane.type = AutomationLaneType::Absolute;
    panLane.paramName = "Pan";
    panLane.absolutePoints.push_back(AutomationPoint{3, 0.0, 0.5, AutomationCurveType::Linear});
    panLane.absolutePoints.push_back(AutomationPoint{4, 2.0, 1.0, AutomationCurveType::Linear});
    document.automationLanes.push_back(panLane);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<Points"));
    REQUIRE(xml.contains("parameter=\"volume1\""));
    REQUIRE(xml.contains("parameter=\"pan1\""));
    REQUIRE(xml.contains("interpolation=\"hold\""));

    juce::String validationError;
    INFO(validationError);
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    REQUIRE(error.isEmpty());
    REQUIRE(imported.automationLanes.size() == 2);

    const AutomationLaneInfo* importedVolume = nullptr;
    const AutomationLaneInfo* importedPan = nullptr;
    for (const auto& lane : imported.automationLanes) {
        if (lane.target.kind == ControlTarget::Kind::TrackVolume)
            importedVolume = &lane;
        else if (lane.target.kind == ControlTarget::Kind::TrackPan)
            importedPan = &lane;
    }

    REQUIRE(importedVolume != nullptr);
    REQUIRE(importedVolume->absolutePoints.size() == 2);
    REQUIRE(importedVolume->absolutePoints[0].beatPosition == Catch::Approx(0.0));
    REQUIRE(importedVolume->absolutePoints[0].value == Catch::Approx(0.75));
    REQUIRE(importedVolume->absolutePoints[1].beatPosition == Catch::Approx(4.0));
    REQUIRE(importedVolume->absolutePoints[1].value == Catch::Approx(0.5));
    REQUIRE(importedVolume->absolutePoints[1].curveType == AutomationCurveType::Step);

    REQUIRE(importedPan != nullptr);
    REQUIRE(importedPan->absolutePoints.size() == 2);
    REQUIRE(importedPan->absolutePoints[0].beatPosition == Catch::Approx(0.0));
    REQUIRE(importedPan->absolutePoints[0].value == Catch::Approx(0.5));
    REQUIRE(importedPan->absolutePoints[1].beatPosition == Catch::Approx(2.0));
    REQUIRE(importedPan->absolutePoints[1].value == Catch::Approx(1.0));
}

TEST_CASE("DawProjectXmlAdapter roundtrips group track hierarchy",
          "[project][serialization][dawproject][groups]") {
    ProjectDocument document;
    document.info.name = "Group Test";
    document.info.version = "0.groups";
    document.info.tempo = 120.0;

    TrackInfo group;
    group.id = 1;
    group.name = "Drum Bus";
    group.type = TrackType::Group;
    group.childIds = {2, 3};

    TrackInfo kick;
    kick.id = 2;
    kick.name = "Kick";
    kick.parentId = group.id;

    TrackInfo snare;
    snare.id = 3;
    snare.name = "Snare";
    snare.parentId = group.id;

    document.tracks.push_back(group);
    document.tracks.push_back(kick);
    document.tracks.push_back(snare);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("contentType=\"tracks\""));

    auto root = juce::parseXML(xml);
    REQUIRE(root != nullptr);
    auto* structure = root->getChildByName("Structure");
    REQUIRE(structure != nullptr);

    int rootTrackCount = 0;
    const juce::XmlElement* groupElement = nullptr;
    for (auto* trackElement : structure->getChildWithTagNameIterator("Track")) {
        ++rootTrackCount;
        if (trackElement->getStringAttribute("name") == "Drum Bus")
            groupElement = trackElement;
    }
    REQUIRE(rootTrackCount == 1);
    REQUIRE(groupElement != nullptr);

    int childTrackCount = 0;
    for (auto* childElement : groupElement->getChildWithTagNameIterator("Track")) {
        ++childTrackCount;
        REQUIRE(childElement->getStringAttribute("name").isNotEmpty());
    }
    REQUIRE(childTrackCount == 2);

    juce::String validationError;
    INFO(validationError);
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    REQUIRE(error.isEmpty());
    REQUIRE(imported.tracks.size() == 3);

    const TrackInfo* importedGroup = nullptr;
    const TrackInfo* importedKick = nullptr;
    const TrackInfo* importedSnare = nullptr;
    for (const auto& track : imported.tracks) {
        if (track.name == "Drum Bus")
            importedGroup = &track;
        else if (track.name == "Kick")
            importedKick = &track;
        else if (track.name == "Snare")
            importedSnare = &track;
    }

    REQUIRE(importedGroup != nullptr);
    REQUIRE(importedGroup->type == TrackType::Group);
    REQUIRE(importedGroup->childIds.size() == 2);
    REQUIRE(importedKick != nullptr);
    REQUIRE(importedSnare != nullptr);
    REQUIRE(importedKick->parentId == importedGroup->id);
    REQUIRE(importedSnare->parentId == importedGroup->id);
    REQUIRE(importedGroup->childIds[0] == importedKick->id);
    REQUIRE(importedGroup->childIds[1] == importedSnare->id);
}

TEST_CASE("DawProjectXmlAdapter roundtrips session clip scenes",
          "[project][serialization][dawproject][scenes]") {
    ProjectDocument document;
    document.info.name = "Session Test";
    document.info.version = "0.session";
    document.info.tempo = 120.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Launcher";
    document.tracks.push_back(track);

    ClipInfo sessionClip;
    sessionClip.id = 10;
    sessionClip.trackId = track.id;
    sessionClip.name = "Slot Pattern";
    sessionClip.view = ClipView::Session;
    sessionClip.sceneIndex = 2;
    sessionClip.setMidiContent();
    sessionClip.setPlacementBeats(12.0, 4.0);
    sessionClip.loopEnabled = true;
    sessionClip.loopStartBeats = 0.0;
    sessionClip.loopLengthBeats = 4.0;
    sessionClip.midiNotes.push_back(MidiNote{60, 100, 0.0, 1.0});
    document.clips.push_back(sessionClip);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<Scenes>"));
    REQUIRE(xml.contains("<Scene"));
    REQUIRE(xml.contains("<ClipSlot"));
    REQUIRE(xml.contains("track=\"track1\""));

    auto root = juce::parseXML(xml);
    REQUIRE(root != nullptr);
    auto* scenes = root->getChildByName("Scenes");
    REQUIRE(scenes != nullptr);
    auto* scene = scenes->getChildByName("Scene");
    REQUIRE(scene != nullptr);
    REQUIRE(scene->getStringAttribute("id") == "scene2");
    auto* sceneLanes = scene->getChildByName("Lanes");
    REQUIRE(sceneLanes != nullptr);
    auto* slot = sceneLanes->getChildByName("ClipSlot");
    REQUIRE(slot != nullptr);
    REQUIRE(slot->getStringAttribute("track") == "track1");
    auto* clip = slot->getChildByName("Clip");
    REQUIRE(clip != nullptr);
    REQUIRE(clip->getStringAttribute("name") == "Slot Pattern");
    REQUIRE(clip->getDoubleAttribute("time") == Catch::Approx(0.0));

    juce::String validationError;
    INFO(validationError);
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    REQUIRE(error.isEmpty());
    REQUIRE(imported.clips.size() == 1);
    REQUIRE(imported.clips[0].name == "Slot Pattern");
    REQUIRE(imported.clips[0].view == ClipView::Session);
    REQUIRE(imported.clips[0].sceneIndex == 2);
    REQUIRE(imported.clips[0].trackId == imported.tracks[0].id);
    REQUIRE(imported.clips[0].placement.startBeat == Catch::Approx(0.0));
    REQUIRE(imported.clips[0].placement.lengthBeats == Catch::Approx(4.0));
    REQUIRE(imported.clips[0].loopEnabled);
    REQUIRE(imported.clips[0].loopLengthBeats == Catch::Approx(4.0));
    REQUIRE(imported.clips[0].midiNotes.size() == 1);
    REQUIRE(imported.clips[0].midiNotes[0].noteNumber == 60);
}

TEST_CASE("DawProjectValidator validates vendored project and metadata schemas",
          "[project][serialization][dawproject][validation]") {
    juce::String error;
    REQUIRE(DawProjectValidator::validateMetadataXml("<MetaData><Title>Song</Title></MetaData>",
                                                     error));
    REQUIRE(error.isEmpty());

    REQUIRE_FALSE(DawProjectValidator::validateProjectXml("<Project/>", error));
    REQUIRE(error.isNotEmpty());
}

TEST_CASE("DawProjectArchive writes validates and reads dawproject archives",
          "[project][serialization][dawproject][archive]") {
    ProjectDocument document;
    document.info.name = "Archive Test";
    document.info.version = "0.archive";
    document.info.tempo = 111.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Lead";
    document.tracks.push_back(track);

    ClipInfo clip;
    clip.id = 1;
    clip.trackId = track.id;
    clip.name = "Line";
    clip.setMidiContent();
    clip.setPlacementBeats(2.0, 3.0);
    clip.midiNotes.push_back(MidiNote{72, 90, 0.0, 1.0});
    document.clips.push_back(clip);

    auto file = createTempDawProjectFile();
    juce::String error;
    const auto wroteArchive = DawProjectArchive::writeToFile(file, document, error);
    INFO(error);
    REQUIRE(wroteArchive);
    REQUIRE(error.isEmpty());
    REQUIRE(file.existsAsFile());

    juce::ZipFile zip(file);
    auto projectXml = readZipTextEntry(zip, "project.xml");
    auto metadataXml = readZipTextEntry(zip, "metadata.xml");
    REQUIRE(DawProjectValidator::validateProjectXml(projectXml, error));
    REQUIRE(DawProjectValidator::validateMetadataXml(metadataXml, error));
    REQUIRE(metadataXml.contains("<Title>Archive Test</Title>"));

    ProjectDocument imported;
    REQUIRE(DawProjectArchive::readFromFile(file, imported, error));
    REQUIRE(error.isEmpty());
    REQUIRE(imported.info.tempo == 111.0);
    REQUIRE(imported.tracks.size() == 1);
    REQUIRE(imported.tracks[0].name == "Lead");
    REQUIRE(imported.clips.size() == 1);
    REQUIRE(imported.clips[0].name == "Line");
    REQUIRE(imported.clips[0].midiNotes.size() == 1);
    REQUIRE(imported.clips[0].midiNotes[0].noteNumber == 72);

    file.deleteFile();
}

TEST_CASE("ProjectSerializer exports and stages dawproject archives",
          "[project][serialization][dawproject][serializer]") {
    clearProjectManagers();

    ProjectInfo info;
    info.name = "Serializer DAWproject";
    info.version = "0.serializer";
    info.tempo = 126.0;

    auto trackId = TrackManager::getInstance().createTrack("Arp", TrackType::Audio);
    auto clipId = ClipManager::getInstance().createMidiClipBeats(trackId, 0.0, 2.0);
    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->name = "Pattern";
    clip->midiNotes.push_back(MidiNote{60, 100, 0.0, 0.5});

    auto file = createTempDawProjectFile();
    REQUIRE(ProjectSerializer::exportToDawProject(file, info));
    REQUIRE(file.existsAsFile());

    StagedProjectData staged;
    REQUIRE(ProjectSerializer::loadDawProjectAndStage(file, staged));
    REQUIRE(staged.info.name == "Serializer DAWproject");
    REQUIRE(staged.info.tempo == 126.0);
    REQUIRE(staged.tracks.size() == 1);
    REQUIRE(staged.tracks[0].name == "Arp");
    REQUIRE(staged.clips.size() == 1);
    REQUIRE(staged.clips[0].name == "Pattern");
    REQUIRE(staged.clips[0].midiNotes.size() == 1);
    REQUIRE(staged.clips[0].midiNotes[0].noteNumber == 60);

    file.deleteFile();
    clearProjectManagers();
}

TEST_CASE("DawProjectArchive embeds and extracts referenced audio files",
          "[project][serialization][dawproject][archive][audio]") {
    // A real on-disk WAV the audio clip points at, so the exporter reads genuine
    // channel/sample-rate facts from the header.
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 1;
    constexpr int kFrames = kSampleRate / 2;  // 0.5 s
    auto source = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getNonexistentChildFile("magda-dawproject-sample", ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out(source.createOutputStream());
        REQUIRE(out != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
            out.get(), kSampleRate, static_cast<unsigned int>(kChannels), 16, {}, 0));
        REQUIRE(writer != nullptr);
        out.release();  // writer owns the stream now
        juce::AudioBuffer<float> buffer(kChannels, kFrames);
        buffer.clear();
        REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, kFrames));
    }

    ProjectDocument document;
    document.info.name = "Audio Embed";
    document.info.version = "0.audio";
    document.info.tempo = 120.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Drums";
    document.tracks.push_back(track);

    ClipInfo clip;
    clip.id = 1;
    clip.trackId = track.id;
    clip.name = "Loop";
    clip.setAudioContent();
    clip.setPlacementBeats(0.0, 4.0);
    clip.audio().source.filePath = source.getFullPathName();
    // Looped region + read offset (exact binary fractions so they round-trip
    // bit-for-bit through the double->string->double path).
    clip.offset = 0.25;
    clip.loopEnabled = true;
    clip.loopStart = 0.5;
    clip.loopLength = 0.25;
    document.clips.push_back(clip);

    auto archive = createTempDawProjectFile();
    juce::String error;
    REQUIRE(DawProjectArchive::writeToFile(archive, document, error));
    INFO(error);
    REQUIRE(error.isEmpty());

    // The XML references the embedded copy relatively, and the sample is stored
    // inside the archive under that same path.
    juce::ZipFile zip(archive);
    auto projectXml = readZipTextEntry(zip, "project.xml");
    REQUIRE(projectXml.contains("external=\"false\""));
    REQUIRE(projectXml.contains("path=\"audio/" + source.getFileName() + "\""));
    REQUIRE(zip.getIndexOfFileName("audio/" + source.getFileName(), false) >= 0);

    // The <Audio> element carries the real header facts, not placeholders.
    REQUIRE(projectXml.contains("sampleRate=\"48000\""));
    REQUIRE(projectXml.contains("channels=\"1\""));

    // The clip carries its read offset and loop region (content time = seconds).
    REQUIRE(projectXml.contains("playStart=\"0.25\""));
    REQUIRE(projectXml.contains("loopStart=\"0.5\""));
    REQUIRE(projectXml.contains("loopEnd=\"0.75\""));

    // Import re-points the clip at an extracted, byte-identical copy of the WAV.
    ProjectDocument imported;
    REQUIRE(DawProjectArchive::readFromFile(archive, imported, error));
    REQUIRE(imported.clips.size() == 1);
    REQUIRE(imported.clips[0].isAudio());
    const juce::File extracted(imported.clips[0].audio().source.filePath);
    REQUIRE(extracted.existsAsFile());
    REQUIRE(extracted.getSize() == source.getSize());

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(extracted));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == kSampleRate);
    REQUIRE(static_cast<int>(reader->numChannels) == kChannels);

    // Loop region and read offset survive the round-trip.
    REQUIRE(imported.clips[0].offset == 0.25);
    REQUIRE(imported.clips[0].loopEnabled);
    REQUIRE(imported.clips[0].loopStart == 0.5);
    REQUIRE(imported.clips[0].loopLength == 0.25);

    source.deleteFile();
    archive.deleteFile();
    extracted.getParentDirectory().getParentDirectory().deleteRecursively();
}

TEST_CASE("DawProjectXmlAdapter warps beat-locked (autoTempo) audio clips",
          "[project][serialization][dawproject][audio][warp]") {
    ProjectDocument document;
    document.info.name = "Warp Test";
    document.info.version = "0.warp";
    document.info.tempo = 120.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Beats";
    document.tracks.push_back(track);

    ClipInfo clip;
    clip.id = 1;
    clip.trackId = track.id;
    clip.name = "Warped";
    clip.setAudioContent();
    clip.setPlacementBeats(0.0, 16.0);                       // a 4-beat loop across 16 beats
    clip.audio().source.filePath = "/nonexistent/beat.wav";  // not embedded; XML-level test
    clip.audio().source.durationSeconds = 2.0;
    clip.audio().interpretation.totalBeats = 4.0;  // source is 4 beats long -> 120 bpm
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.loopStartBeats = 0.0;
    clip.loopLengthBeats = 4.0;
    document.clips.push_back(clip);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    // Beat-locked audio: clip is beats-domain and the Audio is wrapped in <Warps>
    // mapping 4 beats -> 2 seconds.
    REQUIRE(xml.contains("contentTimeUnit=\"beats\""));
    REQUIRE(xml.contains("<Warps"));
    REQUIRE(xml.contains("contentTime=\"2.0\""));
    REQUIRE(xml.contains("loopEnd=\"4.0\""));

    juce::String validationError;
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));
    INFO(validationError);
    REQUIRE(validationError.isEmpty());

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    REQUIRE(imported.clips.size() == 1);
    const auto& ic = imported.clips[0];
    REQUIRE(ic.isAudio());
    REQUIRE(ic.autoTempo);
    REQUIRE(ic.loopEnabled);
    REQUIRE(ic.loopStartBeats == 0.0);
    REQUIRE(ic.loopLengthBeats == 4.0);
    REQUIRE(ic.audio().interpretation.totalBeats == 4.0);
    REQUIRE(ic.audio().source.durationSeconds == 2.0);
}

TEST_CASE("DawProjectArchive embeds and restores VST3 device state",
          "[project][serialization][dawproject][device]") {
    ProjectDocument document;
    document.info.name = "Device Test";
    document.info.version = "0.device";
    document.info.tempo = 120.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Synth";

    DeviceInfo device;
    device.id = 1;
    device.name = "Pro-Q 3";
    device.manufacturer = "FabFilter";
    device.format = PluginFormat::VST3;
    device.isInstrument = false;
    device.uniqueId = "VST3-ProQ3-abc123";
    // The captured VST3 class id is the portable deviceID other hosts match on.
    device.vst3ClassId = "ABCDEF019182FAEB786C6E4178414432";
    // A VST3 device exports its .vstpreset as <State>. Build a minimal-but-real
    // preset blob and stash it base64 the way capture does.
    juce::MemoryBlock presetBytes;
    {
        juce::MemoryOutputStream out(presetBytes, false);
        out.write("VST3", 4);
        out.writeInt(1);
        out.write("ABCDEF019182FAEB786C6E4178414432", 32);
        out.writeInt64(0);
        out.write("CHUNKDATA", 9);
    }
    device.vst3Preset = juce::Base64::toBase64(presetBytes.getData(), presetBytes.getSize());
    track.chain.fxChainElements.push_back(device);
    document.tracks.push_back(track);

    auto archive = createTempDawProjectFile();
    juce::String error;
    REQUIRE(DawProjectArchive::writeToFile(archive, document, error));
    INFO(error);
    REQUIRE(error.isEmpty());

    juce::ZipFile zip(archive);
    auto projectXml = readZipTextEntry(zip, "project.xml");
    REQUIRE(projectXml.contains("<Vst3Plugin"));
    REQUIRE(projectXml.contains("deviceName=\"Pro-Q 3\""));
    REQUIRE(projectXml.contains("deviceRole=\"audioFX\""));
    // deviceID is the VST3 class id, not MAGDA's uniqueId.
    REQUIRE(projectXml.contains("deviceID=\"ABCDEF019182FAEB786C6E4178414432\""));
    // State is a .vstpreset (loadable by other hosts), not the opaque .bin blob.
    REQUIRE(projectXml.contains("path=\"plugins/device-1.vstpreset\""));
    REQUIRE(zip.getIndexOfFileName("plugins/device-1.vstpreset", false) >= 0);

    ProjectDocument imported;
    REQUIRE(DawProjectArchive::readFromFile(archive, imported, error));
    REQUIRE(imported.tracks.size() == 1);
    const auto& chain = imported.tracks[0].chain.fxChainElements;
    REQUIRE(chain.size() == 1);
    REQUIRE(isDevice(chain[0]));
    const auto& d = getDevice(chain[0]);
    REQUIRE(d.format == PluginFormat::VST3);
    REQUIRE(d.name == "Pro-Q 3");
    REQUIRE(d.manufacturer == "FabFilter");
    REQUIRE_FALSE(d.isInstrument);
    REQUIRE_FALSE(d.bypassed);
    // The class id (deviceID) round-trips as the portable identity.
    REQUIRE(d.vst3ClassId == "ABCDEF019182FAEB786C6E4178414432");

    // The .vstpreset round-trips (base64) and the path placeholder is cleared.
    REQUIRE(d.vst3Preset == device.vst3Preset);
    REQUIRE(d.pluginState.isEmpty());

    archive.deleteFile();
}

TEST_CASE("DawProjectXmlAdapter maps the native compressor to a <Compressor> builtin",
          "[project][serialization][dawproject][builtin]") {
    ProjectDocument document;
    document.info.name = "Compressor Test";
    document.info.version = "0.comp";
    document.info.tempo = 120.0;

    TrackInfo track;
    track.id = 1;
    track.name = "Bus";

    DeviceInfo device;
    device.id = 1;
    device.name = "Compressor";
    device.pluginId = "magda_compressor";  // MAGDA's Faust compressor
    device.deviceType = DeviceType::Effect;

    auto addParam = [&](const char* name, float value, float lo, float hi, const char* unit) {
        ParameterInfo p;
        p.name = name;
        p.currentValue = value;
        p.minValue = lo;
        p.maxValue = hi;
        p.unit = unit;
        device.parameters.push_back(p);
    };
    addParam("Threshold", -18.0f, -60.0f, 0.0f, "dB");
    addParam("Ratio", 4.0f, 1.0f, 20.0f, "");
    addParam("Attack", 50.0f, 0.1f, 1000.0f, "ms");
    addParam("Release", 100.0f, 1.0f, 2000.0f, "ms");
    addParam("Output", 3.0f, -24.0f, 24.0f, "dB");
    addParam("Autogain", 1.0f, 0.0f, 1.0f, "");
    track.chain.fxChainElements.push_back(device);
    document.tracks.push_back(track);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<Compressor"));
    REQUIRE(xml.contains("<Threshold"));
    REQUIRE(xml.contains("<Ratio"));
    REQUIRE(xml.contains("<Attack"));
    REQUIRE(xml.contains("<Release"));
    REQUIRE(xml.contains("<OutputGain"));
    REQUIRE(xml.contains("<AutoMakeup"));
    REQUIRE(xml.contains("unit=\"decibel\""));
    REQUIRE(xml.contains("unit=\"seconds\""));  // attack/release ms -> seconds
    // No opaque plugin state file for a param-mapped builtin.
    REQUIRE_FALSE(xml.contains(".vstpreset"));
    REQUIRE_FALSE(xml.contains(".bin"));

    juce::String validationError;
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));
    INFO(validationError);
    REQUIRE(validationError.isEmpty());

    // Import it back: the <Compressor> builtin reconstructs MAGDA's native
    // compressor (pluginId, params at their host-slot indices, seconds -> ms).
    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    REQUIRE(imported.tracks.size() == 1);
    REQUIRE(imported.tracks[0].chain.fxChainElements.size() == 1);
    const auto& back = getDevice(imported.tracks[0].chain.fxChainElements[0]);
    REQUIRE(back.pluginId == "magda_compressor");
    REQUIRE(back.format == PluginFormat::Internal);
    REQUIRE_FALSE(back.isInstrument);

    auto value = [&](int slot) -> float {
        for (const auto& p : back.parameters)
            if (p.paramIndex == slot)
                return p.currentValue;
        FAIL("missing param slot " << slot);
        return 0.0f;
    };
    REQUIRE(value(1) == Catch::Approx(-18.0f));  // Threshold (dB)
    REQUIRE(value(2) == Catch::Approx(4.0f));    // Ratio (linear)
    REQUIRE(value(3) == Catch::Approx(50.0f));   // Attack (ms)
    REQUIRE(value(4) == Catch::Approx(100.0f));  // Release (ms)
    REQUIRE(value(8) == Catch::Approx(3.0f));    // Output (dB)
    REQUIRE(value(14) == Catch::Approx(1.0f));   // Autogain
}

TEST_CASE("DawProjectXmlAdapter routes a master-role channel to the master track",
          "[project][serialization][dawproject][builtin]") {
    const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Structure>
    <Track contentType="audio" loaded="true" id="t1" name="Audio 1">
      <Channel audioChannels="2" role="regular" id="c1"/>
    </Track>
    <Track contentType="audio notes" loaded="true" id="t2" name="Master">
      <Channel audioChannels="2" role="master" id="c2"/>
    </Track>
  </Structure>
</Project>)";

    ProjectDocument doc;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, doc, error));
    REQUIRE(doc.tracks.size() == 2);

    int masterCount = 0;
    for (const auto& t : doc.tracks)
        if (t.type == TrackType::Master)
            ++masterCount;
    REQUIRE(masterCount == 1);

    // The master-role track is peeled into staged.masterTrack (merged onto
    // MAGDA's singleton master) and excluded from the regular track list.
    auto staged = NativeProjectDocumentAdapter::toStagedProjectData(doc);
    REQUIRE(staged.masterTrack != nullptr);
    REQUIRE(staged.masterTrack->name == "Master");
    REQUIRE(staged.tracks.size() == 1);
    REQUIRE(staged.tracks[0].name == "Audio 1");
}

TEST_CASE("DawProjectXmlAdapter imports effect tracks as aux returns with send routing",
          "[project][serialization][dawproject][builtin]") {
    const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Structure>
    <Track contentType="audio" loaded="true" id="t1" name="Audio 1">
      <Channel audioChannels="2" role="regular" id="c1">
        <Sends>
          <Send destination="cFx" type="post" id="s1">
            <Enable value="true" id="se1"/>
            <Volume max="1.0" min="0.0" unit="linear" value="0.5" id="sv1" name="Send"/>
          </Send>
          <Send destination="cFx" type="post" id="s2">
            <Enable value="false" id="se2"/>
            <Volume max="1.0" min="0.0" unit="linear" value="0.0" id="sv2" name="Send"/>
          </Send>
        </Sends>
      </Channel>
    </Track>
    <Track contentType="audio" loaded="true" id="t2" name="Reverb FX">
      <Channel audioChannels="2" role="effect" id="cFx"/>
    </Track>
  </Structure>
</Project>)";

    ProjectDocument doc;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, doc, error));
    REQUIRE(doc.tracks.size() == 2);

    const auto& source = doc.tracks[0];
    const auto& fx = doc.tracks[1];
    REQUIRE(fx.type == TrackType::Aux);
    REQUIRE(fx.auxBusIndex >= 0);

    // The enabled send wires onto the aux bus; the disabled one is dropped.
    REQUIRE(source.sends.size() == 1);
    REQUIRE(source.sends[0].busIndex == fx.auxBusIndex);
    REQUIRE(source.sends[0].level == Catch::Approx(0.5f));
    REQUIRE_FALSE(source.sends[0].preFader);
    REQUIRE(source.sends[0].destTrackId == fx.id);
}

TEST_CASE("DawProjectXmlAdapter exports master role and send routing and roundtrips",
          "[project][serialization][dawproject][builtin]") {
    ProjectDocument document;
    document.info.name = "Routing Test";
    document.info.version = "0.route";
    document.info.tempo = 120.0;

    TrackInfo audio;
    audio.id = 1;
    audio.name = "Audio 1";
    audio.type = TrackType::Audio;

    TrackInfo aux;
    aux.id = 2;
    aux.name = "Reverb FX";
    aux.type = TrackType::Aux;
    aux.auxBusIndex = 0;

    TrackInfo master;
    master.id = MASTER_TRACK_ID;
    master.name = "Master";
    master.type = TrackType::Master;

    SendInfo send;
    send.busIndex = aux.auxBusIndex;
    send.level = 0.5f;
    send.preFader = false;
    send.destTrackId = aux.id;
    audio.sends.push_back(send);

    document.tracks.push_back(audio);
    document.tracks.push_back(aux);
    document.tracks.push_back(master);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("role=\"master\""));
    REQUIRE(xml.contains("role=\"effect\""));
    REQUIRE(xml.contains("<Sends"));
    REQUIRE(xml.contains("<Send "));
    REQUIRE(xml.contains("destination=\"" + juce::String("channel") + juce::String(aux.id) + "\""));

    juce::String validationError;
    const bool valid = DawProjectValidator::validateProjectXml(xml, validationError);
    INFO(validationError);
    REQUIRE(valid);

    // Roundtrip: master peels back onto the master slot, the effect track is an
    // aux, and the send is restored at its level.
    ProjectDocument back;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, back, error));
    auto staged = NativeProjectDocumentAdapter::toStagedProjectData(back);
    REQUIRE(staged.masterTrack != nullptr);
    REQUIRE(staged.masterTrack->name == "Master");
    REQUIRE(staged.tracks.size() == 2);

    const TrackInfo* auxBack = nullptr;
    const TrackInfo* audioBack = nullptr;
    for (const auto& t : staged.tracks) {
        if (t.type == TrackType::Aux)
            auxBack = &t;
        else
            audioBack = &t;
    }
    REQUIRE(auxBack != nullptr);
    REQUIRE(audioBack != nullptr);
    REQUIRE(audioBack->sends.size() == 1);
    REQUIRE(audioBack->sends[0].busIndex == auxBack->auxBusIndex);
    REQUIRE(audioBack->sends[0].level == Catch::Approx(0.5f));
    REQUIRE(audioBack->sends[0].destTrackId == auxBack->id);
}

namespace {
// Build a single-device ProjectDocument for a native builtin and return the XML.
ProjectDocument makeBuiltinDoc(const juce::String& pluginId, const juce::String& name,
                               const std::vector<ParameterInfo>& params) {
    ProjectDocument document;
    document.info.name = name + " Test";
    document.info.tempo = 120.0;
    TrackInfo track;
    track.id = 1;
    track.name = "Bus";
    DeviceInfo device;
    device.id = 1;
    device.name = name;
    device.pluginId = pluginId;
    device.deviceType = DeviceType::Effect;
    device.parameters = params;
    track.chain.fxChainElements.push_back(device);
    document.tracks.push_back(track);
    return document;
}

ParameterInfo namedParam(const char* name, int slot, float value, float lo, float hi,
                         const char* unit) {
    ParameterInfo p;
    p.name = name;
    p.paramIndex = slot;
    p.currentValue = value;
    p.minValue = lo;
    p.maxValue = hi;
    p.unit = unit;
    return p;
}

float importedSlot(const DeviceInfo& d, int slot) {
    for (const auto& p : d.parameters)
        if (p.paramIndex == slot)
            return p.currentValue;
    FAIL("missing imported slot " << slot);
    return 0.0f;
}
}  // namespace

TEST_CASE("DawProjectXmlAdapter maps the native gate to a <NoiseGate> builtin",
          "[project][serialization][dawproject][builtin]") {
    // Slots: Attack=0, Release=1, Mix=2, Output=3, Threshold=4, Ratio=5, Range=6.
    auto document = makeBuiltinDoc("magda_gate_expander", "Gate",
                                   {namedParam("Attack", 0, 5.0f, 0.1f, 100.0f, "ms"),
                                    namedParam("Release", 1, 120.0f, 5.0f, 1000.0f, "ms"),
                                    namedParam("Mix", 2, 1.0f, 0.0f, 1.0f, ""),
                                    namedParam("Output", 3, 0.0f, -24.0f, 24.0f, "dB"),
                                    namedParam("Threshold", 4, -40.0f, -80.0f, 0.0f, "dB"),
                                    namedParam("Ratio", 5, 4.0f, 1.0f, 50.0f, ""),
                                    namedParam("Range", 6, 60.0f, 0.0f, 80.0f, "dB")});

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<NoiseGate"));
    REQUIRE(xml.contains("<Range"));
    REQUIRE(xml.contains("<Ratio"));
    REQUIRE(xml.contains("unit=\"seconds\""));   // attack/release
    REQUIRE_FALSE(xml.contains("<OutputGain"));  // gate has no output field
    REQUIRE_FALSE(xml.contains(".vstpreset"));

    juce::String validationError;
    INFO(validationError);
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    const auto& back = getDevice(imported.tracks[0].chain.fxChainElements[0]);
    REQUIRE(back.pluginId == "magda_gate_expander");
    REQUIRE(importedSlot(back, 0) == Catch::Approx(5.0f));    // Attack ms
    REQUIRE(importedSlot(back, 1) == Catch::Approx(120.0f));  // Release ms
    REQUIRE(importedSlot(back, 4) == Catch::Approx(-40.0f));  // Threshold dB
    REQUIRE(importedSlot(back, 5) == Catch::Approx(4.0f));    // Ratio
    REQUIRE(importedSlot(back, 6) == Catch::Approx(60.0f));   // Range dB
}

TEST_CASE("DawProjectXmlAdapter maps the native limiter to a <Limiter> builtin",
          "[project][serialization][dawproject][builtin]") {
    // Slots: Threshold=0, Attack=1, Release=2, Output=3.
    auto document = makeBuiltinDoc("magda_limiter", "Limiter",
                                   {namedParam("Threshold", 0, -1.0f, -24.0f, 0.0f, "dB"),
                                    namedParam("Attack", 1, 1.0f, 0.1f, 50.0f, "ms"),
                                    namedParam("Release", 2, 200.0f, 10.0f, 2000.0f, "ms"),
                                    namedParam("Output", 3, -3.0f, -24.0f, 0.0f, "dB")});

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<Limiter"));
    REQUIRE(xml.contains("<OutputGain"));
    REQUIRE_FALSE(xml.contains("<Ratio"));       // limiter has no ratio
    REQUIRE_FALSE(xml.contains("<AutoMakeup"));  // nor automakeup

    juce::String validationError;
    INFO(validationError);
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    const auto& back = getDevice(imported.tracks[0].chain.fxChainElements[0]);
    REQUIRE(back.pluginId == "magda_limiter");
    REQUIRE(importedSlot(back, 0) == Catch::Approx(-1.0f));   // Threshold dB
    REQUIRE(importedSlot(back, 1) == Catch::Approx(1.0f));    // Attack ms
    REQUIRE(importedSlot(back, 2) == Catch::Approx(200.0f));  // Release ms
    REQUIRE(importedSlot(back, 3) == Catch::Approx(-3.0f));   // Output dB
}

TEST_CASE("DawProjectXmlAdapter maps the native EQ to an <Equalizer> builtin",
          "[project][serialization][dawproject][builtin]") {
    // magda_eq: 8 bands x 5 slots (Enabled,Type,Freq,Gain,Q); Output at slot 40.
    auto band = [](int b, bool enabled, float type, float freq, float gain, float q) {
        const int base = b * 5;
        return std::vector<ParameterInfo>{
            namedParam("Enabled", base + 0, enabled ? 1.0f : 0.0f, 0.0f, 1.0f, ""),
            namedParam("Type", base + 1, type, 0.0f, 5.0f, ""),
            namedParam("Freq", base + 2, freq, 20.0f, 20000.0f, "Hz"),
            namedParam("Gain", base + 3, gain, -24.0f, 24.0f, "dB"),
            namedParam("Q", base + 4, q, 0.1f, 10.0f, "")};
    };
    std::vector<ParameterInfo> params;
    auto append = [&](const std::vector<ParameterInfo>& v) {
        params.insert(params.end(), v.begin(), v.end());
    };
    append(band(0, true, 2.0f, 1000.0f, 3.0f, 0.7f));   // bell
    append(band(1, true, 3.0f, 8000.0f, -2.0f, 0.7f));  // highShelf
    append(band(2, false, 2.0f, 5000.0f, 0.0f, 1.0f));  // disabled -> not emitted
    params.push_back(namedParam("Output", 40, -1.0f, -24.0f, 12.0f, "dB"));

    auto document = makeBuiltinDoc("magda_eq", "EQ", params);

    auto xml = DawProjectXmlAdapter::toProjectXml(document);
    REQUIRE(xml.contains("<Equalizer"));
    REQUIRE(xml.contains("<Band"));
    REQUIRE(xml.contains("type=\"bell\""));
    REQUIRE(xml.contains("type=\"highShelf\""));
    REQUIRE(xml.contains("unit=\"semitones\""));  // DAWproject EQ freq is pitch, not Hz
    REQUIRE(xml.contains("<OutputGain"));

    juce::String validationError;
    INFO(validationError);
    REQUIRE(DawProjectValidator::validateProjectXml(xml, validationError));

    ProjectDocument imported;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, imported, error));
    const auto& back = getDevice(imported.tracks[0].chain.fxChainElements[0]);
    REQUIRE(back.pluginId == "magda_eq");
    // Only the two enabled bands survive, in order: band0 (bell) and band1 (highShelf).
    REQUIRE(importedSlot(back, 1) == Catch::Approx(2.0f));     // band0 type = bell
    REQUIRE(importedSlot(back, 2) == Catch::Approx(1000.0f));  // band0 freq
    REQUIRE(importedSlot(back, 6) == Catch::Approx(3.0f));     // band1 type = highShelf
    REQUIRE(importedSlot(back, 7) == Catch::Approx(8000.0f));  // band1 freq
    REQUIRE(importedSlot(back, 40) == Catch::Approx(-1.0f));   // output dB
    // The disabled third band was not emitted, so its type slot is absent.
    bool hasBand2 = false;
    for (const auto& p : back.parameters)
        if (p.paramIndex == 11)
            hasBand2 = true;
    REQUIRE_FALSE(hasBand2);
}

TEST_CASE("DawProjectXmlAdapter imports Bitwig EQ semitones and dynamics percent ratio",
          "[project][serialization][dawproject][builtin]") {
    // Bitwig writes EQ band Freq in semitones (MIDI pitch) and dynamics Ratio
    // as a 0-100% amount. Both must convert, not be read as Hz / linear.
    const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Structure>
    <Track contentType="audio" loaded="true" id="t1" name="Bus">
      <Channel audioChannels="2" role="regular" id="c1">
        <Devices>
          <Compressor deviceID="x" deviceName="Compressor" deviceRole="audioFX" loaded="true" id="cmp" name="Compressor">
            <Ratio max="100.0" min="0.0" unit="percent" value="75.0" id="cr" name="Ratio"/>
          </Compressor>
          <Equalizer deviceID="y" deviceName="EQ+" deviceRole="audioFX" loaded="true" id="eq" name="EQ+">
            <Band type="bell" order="0">
              <Freq max="135.076232" min="15.486821" unit="semitones" value="43.869309" id="bf" name="Band 1 Frequency"/>
              <Gain max="30.0" min="-30.0" unit="decibel" value="6.0" id="bg" name="Band 1 Gain"/>
              <Q max="40.0" min="0.025" unit="linear" value="1.0" id="bq" name="Band 1 Q"/>
            </Band>
          </Equalizer>
          <NoiseGate deviceID="z" deviceName="Gate" deviceRole="audioFX" loaded="true" id="gate" name="Gate">
            <Ratio max="100.0" min="0.0" unit="percent" value="80.0" id="gr" name="Ratio"/>
          </NoiseGate>
        </Devices>
      </Channel>
    </Track>
  </Structure>
</Project>)";

    ProjectDocument doc;
    juce::String error;
    REQUIRE(DawProjectXmlAdapter::fromProjectXml(xml, doc, error));
    REQUIRE(doc.tracks.size() == 1);
    const auto& fx = doc.tracks[0].chain.fxChainElements;
    REQUIRE(fx.size() == 3);

    const auto& comp = getDevice(fx[0]);
    REQUIRE(comp.pluginId == "magda_compressor");
    // 75% amount -> ratio 1/(1 - 0.75) = 4:1, not a literal 75.
    REQUIRE(importedSlot(comp, 2) == Catch::Approx(4.0f));

    const auto& eq = getDevice(fx[1]);
    REQUIRE(eq.pluginId == "magda_eq");
    // 43.869 semitones -> ~103 Hz (440 * 2^((43.869-69)/12)), not 43.87 Hz.
    REQUIRE(importedSlot(eq, 2) == Catch::Approx(103.0f).margin(1.0));

    const auto& gate = getDevice(fx[2]);
    REQUIRE(gate.pluginId == "magda_gate_expander");
    // 80% amount -> ratio 1/(1 - 0.8) = 5:1, not a literal 80.
    REQUIRE(importedSlot(gate, 5) == Catch::Approx(5.0f));
}
