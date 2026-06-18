#include "DawProjectXmlAdapter.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>
#include <set>

#include "../../core/TempoUtils.hpp"
#include "version.hpp"

namespace magda {
namespace {

juce::String idFor(const char* prefix, int value) {
    return juce::String(prefix) + juce::String(value);
}

juce::String colourToDawProject(const juce::Colour colour) {
    return "#" + colour.toDisplayString(false).substring(2).toLowerCase();
}

juce::Colour colourFromDawProject(const juce::String& value) {
    if (value.startsWithChar('#'))
        return juce::Colour::fromString("ff" + value.substring(1));
    return juce::Colours::transparentBlack;
}

double linearToDawProjectPan(float pan) {
    return juce::jlimit(0.0, 1.0, (static_cast<double>(pan) + 1.0) * 0.5);
}

float dawProjectToLinearPan(double pan) {
    return static_cast<float>(juce::jlimit(-1.0, 1.0, (pan * 2.0) - 1.0));
}

juce::String contentTypeForTrack(const TrackInfo& track, const std::vector<ClipInfo>& clips) {
    if (track.type == TrackType::Group)
        return "tracks";

    // MAGDA tracks are all hybrid (no distinct MIDI/instrument type), so decide
    // the DAWproject contentType from what the track actually carries. A track
    // holding MIDI clips is a "notes" track even with no instrument loaded;
    // otherwise Bitwig (and others) import it as an audio track.
    bool hasMidiClip = false;
    bool hasAudioClip = false;
    for (const auto& clip : clips) {
        if (clip.trackId != track.id)
            continue;
        hasMidiClip = hasMidiClip || clip.isMidi();
        hasAudioClip = hasAudioClip || clip.isAudio();
    }

    const bool notes = hasMidiClip || track.hasInstrument();
    if (notes && hasAudioClip)
        return "notes audio";
    if (notes)
        return "notes";
    return "audio";
}

juce::XmlElement* addRealParameter(juce::XmlElement& parent, const juce::String& tag,
                                   const juce::String& id, const juce::String& name,
                                   const juce::String& unit, double value, double min, double max) {
    auto* parameter = parent.createNewChildElement(tag);
    parameter->setAttribute("id", id);
    parameter->setAttribute("name", name);
    parameter->setAttribute("unit", unit);
    parameter->setAttribute("value", value);
    parameter->setAttribute("min", min);
    parameter->setAttribute("max", max);
    return parameter;
}

juce::XmlElement* addBoolParameter(juce::XmlElement& parent, const juce::String& tag,
                                   const juce::String& id, const juce::String& name, bool value) {
    auto* parameter = parent.createNewChildElement(tag);
    parameter->setAttribute("id", id);
    parameter->setAttribute("name", name);
    parameter->setAttribute("value", value ? "true" : "false");
    return parameter;
}

juce::XmlElement* addNotes(juce::XmlElement& clipElement, const ClipInfo& clip,
                           const juce::String& id) {
    auto* notesElement = clipElement.createNewChildElement("Notes");
    notesElement->setAttribute("id", id);

    for (const auto& note : clip.midiNotes) {
        auto* noteElement = notesElement->createNewChildElement("Note");
        noteElement->setAttribute("time", note.startBeat);
        noteElement->setAttribute("duration", note.lengthBeats);
        noteElement->setAttribute("channel", 0);
        noteElement->setAttribute("key", note.noteNumber);
        noteElement->setAttribute("vel", static_cast<double>(note.velocity) / 127.0);
        noteElement->setAttribute("rel", static_cast<double>(note.velocity) / 127.0);
    }

    return notesElement;
}

// Real channel count / sample rate / duration read from the audio file header.
// DAWproject requires channels and sampleRate on <Audio>; importers (Bitwig) use
// them to interpret the file, so they must reflect the actual media, not guesses.
struct AudioFileFacts {
    int channels = 0;
    int sampleRate = 0;
    double durationSeconds = 0.0;
};

AudioFileFacts readAudioFileFacts(const juce::File& file) {
    AudioFileFacts facts;
    if (!file.existsAsFile())
        return facts;

    // A local format manager per read: sharing one isn't thread-safe and export
    // touches only a handful of files, so this matches the codebase convention.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file)); reader) {
        facts.channels = static_cast<int>(reader->numChannels);
        facts.sampleRate = static_cast<int>(reader->sampleRate);
        if (reader->sampleRate > 0.0)
            facts.durationSeconds =
                static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }
    return facts;
}

void addAudioContent(juce::XmlElement& clipElement, const ClipInfo& clip, const juce::String& id,
                     const std::map<juce::String, juce::String>& embeddedBySource) {
    const auto source = clip.audio().source.filePath;
    const auto facts = readAudioFileFacts(juce::File(source));

    auto* audio = clipElement.createNewChildElement("Audio");
    audio->setAttribute("id", id);
    audio->setAttribute("channels", facts.channels > 0 ? facts.channels : 2);
    audio->setAttribute("sampleRate", facts.sampleRate);
    // Prefer the clip model's source duration; fall back to the file's length
    // when the model hasn't recorded one.
    audio->setAttribute("duration", clip.audio().source.durationSeconds > 0.0
                                        ? clip.audio().source.durationSeconds
                                        : facts.durationSeconds);

    auto* file = audio->createNewChildElement("File");

    // Prefer an embedded, archive-relative reference so the project is portable
    // (other DAWs resolve it from inside the .dawproject). Fall back to an
    // external absolute path only when the source file is missing and cannot be
    // embedded.
    const auto it = embeddedBySource.find(source);
    if (it != embeddedBySource.end()) {
        file->setAttribute("path", it->second);
        file->setAttribute("external", "false");
    } else {
        file->setAttribute("path", source);
        file->setAttribute("external", "true");
    }
}

ClipInfo clipFromXml(const juce::XmlElement& clipElement, TrackId trackId, ClipId clipId) {
    ClipInfo clip;
    clip.id = clipId;
    clip.trackId = trackId;
    clip.name = clipElement.getStringAttribute("name");
    clip.colour = colourFromDawProject(clipElement.getStringAttribute("color"));
    clip.view = ClipView::Arrangement;
    clip.setPlacementBeats(clipElement.getDoubleAttribute("time", 0.0),
                           clipElement.getDoubleAttribute("duration", 0.0));

    if (auto* notesElement = clipElement.getChildByName("Notes")) {
        clip.setMidiContent();
        for (auto* noteElement : notesElement->getChildWithTagNameIterator("Note")) {
            MidiNote note;
            note.startBeat = noteElement->getDoubleAttribute("time", 0.0);
            note.lengthBeats = noteElement->getDoubleAttribute("duration", 1.0);
            note.noteNumber = noteElement->getIntAttribute("key", 60);
            note.velocity = juce::jlimit(
                0, 127,
                static_cast<int>(std::round(noteElement->getDoubleAttribute("vel", 0.8) * 127.0)));
            clip.midiNotes.push_back(note);
        }
        return clip;
    }

    if (auto* audioElement = clipElement.getChildByName("Audio")) {
        clip.setAudioContent();
        clip.audio().source.durationSeconds = audioElement->getDoubleAttribute("duration", 0.0);
        if (auto* fileElement = audioElement->getChildByName("File"))
            clip.audio().source.filePath = fileElement->getStringAttribute("path");
        return clip;
    }

    clip.setMidiContent();
    return clip;
}

}  // namespace

juce::String DawProjectXmlAdapter::toProjectXml(const ProjectDocument& document) {
    juce::XmlElement project("Project");
    project.setAttribute("version", "1.0");

    // Audio sources that will be embedded in the archive, keyed by on-disk path,
    // so audio clips reference the archive-relative copy instead of a local path.
    std::map<juce::String, juce::String> embeddedBySource;
    for (const auto& embedded : collectEmbeddedAudio(document))
        embeddedBySource[embedded.sourcePath] = embedded.archivePath;

    auto* application = project.createNewChildElement("Application");
    application->setAttribute("name", "MAGDA");
    application->setAttribute("version", document.info.version.isNotEmpty() ? document.info.version
                                                                            : MAGDA_VERSION);

    auto* transport = project.createNewChildElement("Transport");
    addRealParameter(*transport, "Tempo", "transportTempo", "Tempo", "bpm", document.info.tempo,
                     20.0, 666.0);
    auto* timeSignature = transport->createNewChildElement("TimeSignature");
    timeSignature->setAttribute("id", "transportTimeSignature");
    timeSignature->setAttribute("numerator", document.info.timeSignatureNumerator);
    timeSignature->setAttribute("denominator", document.info.timeSignatureDenominator);

    auto* structure = project.createNewChildElement("Structure");
    for (const auto& track : document.tracks) {
        auto* trackElement = structure->createNewChildElement("Track");
        trackElement->setAttribute("id", idFor("track", track.id));
        trackElement->setAttribute("name", track.name);
        trackElement->setAttribute("contentType", contentTypeForTrack(track, document.clips));
        trackElement->setAttribute("loaded", "true");
        if (!track.colour.isTransparent())
            trackElement->setAttribute("color", colourToDawProject(track.colour));

        auto* channel = trackElement->createNewChildElement("Channel");
        channel->setAttribute("id", idFor("channel", track.id));
        channel->setAttribute("role", "regular");
        channel->setAttribute("audioChannels", 2);
        channel->setAttribute("solo", track.soloed ? "true" : "false");
        addBoolParameter(*channel, "Mute", idFor("mute", track.id), "Mute", track.muted);
        addRealParameter(*channel, "Pan", idFor("pan", track.id), "Pan", "normalized",
                         linearToDawProjectPan(track.pan), 0.0, 1.0);
        addRealParameter(*channel, "Volume", idFor("volume", track.id), "Volume", "linear",
                         track.volume, 0.0, 2.0);
    }

    auto* arrangement = project.createNewChildElement("Arrangement");
    arrangement->setAttribute("id", "arrangement");
    auto* rootLanes = arrangement->createNewChildElement("Lanes");
    rootLanes->setAttribute("id", "arrangementLanes");
    rootLanes->setAttribute("timeUnit", "beats");

    for (const auto& track : document.tracks) {
        auto* trackLanes = rootLanes->createNewChildElement("Lanes");
        trackLanes->setAttribute("id", idFor("trackLanes", track.id));
        trackLanes->setAttribute("track", idFor("track", track.id));

        auto* clips = trackLanes->createNewChildElement("Clips");
        clips->setAttribute("id", idFor("clips", track.id));

        for (const auto& clip : document.clips) {
            if (clip.trackId != track.id || clip.view != ClipView::Arrangement)
                continue;

            auto* clipElement = clips->createNewChildElement("Clip");
            clipElement->setAttribute("time", clip.placement.startBeat);
            clipElement->setAttribute("duration", clip.placement.lengthBeats);
            clipElement->setAttribute("playStart", 0.0);
            // The arrangement Lanes are timeUnit="beats" (governs clip time/
            // duration), but a clip's inner content (Note times, Audio duration)
            // lives in its own content time. Declare it explicitly: MIDI note
            // times are clip-relative beats, audio duration is in seconds.
            // Omitting this lets the importing DAW guess, which misplaces notes.
            clipElement->setAttribute("contentTimeUnit", clip.isAudio() ? "seconds" : "beats");
            if (clip.name.isNotEmpty())
                clipElement->setAttribute("name", clip.name);
            if (!clip.colour.isTransparent())
                clipElement->setAttribute("color", colourToDawProject(clip.colour));

            if (clip.isMidi())
                addNotes(*clipElement, clip, idFor("notes", clip.id));
            else if (clip.isAudio())
                addAudioContent(*clipElement, clip, idFor("audio", clip.id), embeddedBySource);
        }
    }

    project.createNewChildElement("Scenes");
    return project.toString();
}

bool DawProjectXmlAdapter::fromProjectXml(const juce::String& xml, ProjectDocument& outDocument,
                                          juce::String& error) {
    auto root = juce::parseXML(xml);
    if (!root || !root->hasTagName("Project")) {
        error = "DAWproject XML does not contain a Project root";
        return false;
    }

    ProjectDocument document;
    document.info.version = MAGDA_VERSION;
    document.info.name = "Imported DAWproject";

    if (auto* transport = root->getChildByName("Transport")) {
        if (auto* tempo = transport->getChildByName("Tempo"))
            document.info.tempo = tempo->getDoubleAttribute("value", DEFAULT_BPM);
        if (auto* sig = transport->getChildByName("TimeSignature")) {
            document.info.timeSignatureNumerator = sig->getIntAttribute("numerator", 4);
            document.info.timeSignatureDenominator = sig->getIntAttribute("denominator", 4);
        }
    }

    std::map<juce::String, TrackId> trackIds;
    TrackId nextTrackId = 1;
    if (auto* structure = root->getChildByName("Structure")) {
        for (auto* trackElement : structure->getChildWithTagNameIterator("Track")) {
            TrackInfo track;
            track.id = nextTrackId++;
            track.name =
                trackElement->getStringAttribute("name", "Track " + juce::String(track.id));
            track.colour = colourFromDawProject(trackElement->getStringAttribute("color"));
            const auto contentType = trackElement->getStringAttribute("contentType");
            track.type = TrackType::Audio;

            if (auto* channel = trackElement->getChildByName("Channel")) {
                if (auto* volume = channel->getChildByName("Volume"))
                    track.volume = static_cast<float>(volume->getDoubleAttribute("value", 1.0));
                if (auto* pan = channel->getChildByName("Pan"))
                    track.pan = dawProjectToLinearPan(pan->getDoubleAttribute("value", 0.5));
                if (auto* mute = channel->getChildByName("Mute"))
                    track.muted = mute->getBoolAttribute("value", false);
                track.soloed = channel->getBoolAttribute("solo", false);
            }

            trackIds[trackElement->getStringAttribute("id")] = track.id;
            document.tracks.push_back(std::move(track));
        }
    }

    ClipId nextClipId = 1;
    if (auto* arrangement = root->getChildByName("Arrangement")) {
        if (auto* rootLanes = arrangement->getChildByName("Lanes")) {
            for (auto* trackLanes : rootLanes->getChildWithTagNameIterator("Lanes")) {
                const auto trackRef = trackLanes->getStringAttribute("track");
                auto trackIt = trackIds.find(trackRef);
                if (trackIt == trackIds.end())
                    continue;

                if (auto* clips = trackLanes->getChildByName("Clips")) {
                    for (auto* clipElement : clips->getChildWithTagNameIterator("Clip"))
                        document.clips.push_back(
                            clipFromXml(*clipElement, trackIt->second, nextClipId++));
                }
            }
        }
    }

    outDocument = std::move(document);
    return true;
}

std::vector<DawProjectXmlAdapter::EmbeddedAudioFile> DawProjectXmlAdapter::collectEmbeddedAudio(
    const ProjectDocument& document) {
    std::vector<EmbeddedAudioFile> files;
    std::set<juce::String> seenSources;       // dedup the same sample used by many clips
    std::set<juce::String> usedArchivePaths;  // disambiguate same-name distinct sources

    for (const auto& clip : document.clips) {
        if (!clip.isAudio())
            continue;

        const auto source = clip.audio().source.filePath;
        if (source.isEmpty() || seenSources.count(source) > 0)
            continue;
        seenSources.insert(source);

        const juce::File srcFile(source);
        if (!srcFile.existsAsFile())
            continue;  // can't embed a missing file; it stays an external reference

        juce::String archivePath = "audio/" + srcFile.getFileName();
        for (int n = 1; usedArchivePaths.count(archivePath) > 0; ++n)
            archivePath = "audio/" + srcFile.getFileNameWithoutExtension() + "-" + juce::String(n) +
                          srcFile.getFileExtension();
        usedArchivePaths.insert(archivePath);

        files.push_back({source, archivePath});
    }

    return files;
}

}  // namespace magda
