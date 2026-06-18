#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

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
    REQUIRE(document.tracks.size() == 1);
    REQUIRE(document.tracks[0].name == "Keys");
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

    source.deleteFile();
    archive.deleteFile();
    extracted.getParentDirectory().getParentDirectory().deleteRecursively();
}
