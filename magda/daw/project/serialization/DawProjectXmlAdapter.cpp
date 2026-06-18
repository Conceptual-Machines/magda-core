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

// Build the <Audio> media element (channels/sampleRate/duration in seconds +
// the File reference) under `parent`, returning it. The Audio descriptor is
// always seconds-domain; whether the clip plays it in seconds or beats is
// decided by the parent (plain clip vs <Warps>).
juce::XmlElement& addAudioElement(juce::XmlElement& parent, const ClipInfo& clip,
                                  const juce::String& id,
                                  const std::map<juce::String, juce::String>& embeddedBySource) {
    const auto source = clip.audio().source.filePath;
    const auto facts = readAudioFileFacts(juce::File(source));

    auto* audio = parent.createNewChildElement("Audio");
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
    return *audio;
}

// Plain seconds-domain audio: the <Audio> sits directly in the clip, whose
// contentTimeUnit is "seconds".
void addAudioContent(juce::XmlElement& clipElement, const ClipInfo& clip, const juce::String& id,
                     const std::map<juce::String, juce::String>& embeddedBySource) {
    addAudioElement(clipElement, clip, id, embeddedBySource);
}

// Beat-locked (autoTempo) audio: the clip's content time is beats, and a <Warps>
// maps that beat timeline onto the source's seconds via two linear warp markers
// (clip start -> 0s, source length in beats -> source length in seconds). This
// is how DAWproject keeps stretched audio tempo-synced instead of one-shot.
void addWarpedAudioContent(juce::XmlElement& clipElement, const ClipInfo& clip,
                           const juce::String& id,
                           const std::map<juce::String, juce::String>& embeddedBySource) {
    auto* warps = clipElement.createNewChildElement("Warps");
    warps->setAttribute("contentTimeUnit", "seconds");
    warps->setAttribute("timeUnit", "beats");

    auto& audio = addAudioElement(*warps, clip, id, embeddedBySource);

    const double sourceSeconds =
        audio.getDoubleAttribute("duration", clip.audio().source.durationSeconds);
    const double totalBeats = clip.audio().interpretation.totalBeats;

    auto* start = warps->createNewChildElement("Warp");
    start->setAttribute("time", 0.0);
    start->setAttribute("contentTime", 0.0);
    auto* end = warps->createNewChildElement("Warp");
    end->setAttribute("time", totalBeats);
    end->setAttribute("contentTime", sourceSeconds);
}

// ---- Devices (VST3 / AU hosted plugins) -----------------------------------
//
// VST2 is unsupported in MAGDA and CLAP is not wired yet, so only VST3/AU are
// exported. Native MAGDA devices and racks are skipped: they have no portable
// DAWproject representation (a follow-up could emit them as opaque BuiltinDevices
// for MAGDA<->MAGDA only).

// DeviceInfo::format isn't always set correctly (an AU can come through as VST3),
// but uniqueId is JUCE's createIdentifierString, which prefixes the real format.
// Trust that prefix, falling back to the format field.
PluginFormat resolveDeviceFormat(const DeviceInfo& device) {
    if (device.uniqueId.startsWith("AudioUnit"))
        return PluginFormat::AU;
    if (device.uniqueId.startsWith("VST3"))
        return PluginFormat::VST3;
    if (device.uniqueId.startsWith("VST"))
        return PluginFormat::VST;  // VST2
    return device.format;
}

bool isExportableDevice(const DeviceInfo& device) {
    const auto format = resolveDeviceFormat(device);
    return format == PluginFormat::VST3 || format == PluginFormat::AU;
}

const char* deviceElementTag(const DeviceInfo& device) {
    return resolveDeviceFormat(device) == PluginFormat::AU ? "AuPlugin" : "Vst3Plugin";
}

juce::String deviceStateArchivePath(DeviceId id) {
    return "plugins/device-" + juce::String(static_cast<int>(id)) + ".bin";
}

// Top-level FX-chain + post-FX devices that map to DAWproject. Racks (parallel
// routing) are intentionally not descended into.
void collectExportableDevices(const TrackInfo& track, std::vector<const DeviceInfo*>& out) {
    for (const auto& element : track.chain.fxChainElements)
        if (isDevice(element) && isExportableDevice(getDevice(element)))
            out.push_back(&getDevice(element));
    for (const auto& postFx : track.chain.postFxChainElements)
        if (isExportableDevice(postFx.device))
            out.push_back(&postFx.device);
}

void addDevice(juce::XmlElement& devices, const DeviceInfo& device) {
    auto* dev = devices.createNewChildElement(deviceElementTag(device));
    dev->setAttribute("deviceRole", device.isInstrument ? "instrument" : "audioFX");
    dev->setAttribute("deviceName", device.name);

    const auto deviceId = device.uniqueId.isNotEmpty() ? device.uniqueId : device.fileOrIdentifier;
    if (deviceId.isNotEmpty())
        dev->setAttribute("deviceID", deviceId);
    if (device.manufacturer.isNotEmpty())
        dev->setAttribute("deviceVendor", device.manufacturer);

    // device sequence is Parameters, Enabled, State. We skip Parameters (the
    // State chunk is what actually restores the plugin).
    addBoolParameter(*dev, "Enabled", idFor("deviceEnabled", device.id), "Enabled",
                     !device.bypassed);

    if (device.pluginState.isNotEmpty()) {
        auto* state = dev->createNewChildElement("State");
        state->setAttribute("path", deviceStateArchivePath(device.id));
        state->setAttribute("external", "false");
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

        // Read offset and loop region (content time = beats for MIDI).
        clip.offsetBeats = clipElement.getDoubleAttribute("playStart", 0.0);
        if (clipElement.hasAttribute("loopStart") && clipElement.hasAttribute("loopEnd")) {
            clip.loopEnabled = true;
            clip.loopStartBeats = clipElement.getDoubleAttribute("loopStart", 0.0);
            clip.loopLengthBeats = juce::jmax(0.0, clipElement.getDoubleAttribute("loopEnd", 0.0) -
                                                       clip.loopStartBeats);
        }
        return clip;
    }

    // Beat-locked (warped) audio: the <Audio> is wrapped in <Warps> and the clip
    // content is beats-domain. Recover the source length/BPM from the warp
    // markers and read the offset/loop region in beats.
    if (auto* warps = clipElement.getChildByName("Warps")) {
        clip.setAudioContent();
        clip.autoTempo = true;
        if (auto* audioElement = warps->getChildByName("Audio")) {
            clip.audio().source.durationSeconds = audioElement->getDoubleAttribute("duration", 0.0);
            if (auto* fileElement = audioElement->getChildByName("File"))
                clip.audio().source.filePath = fileElement->getStringAttribute("path");
        }

        double maxBeats = 0.0, maxSeconds = 0.0;
        for (auto* w : warps->getChildWithTagNameIterator("Warp")) {
            maxBeats = juce::jmax(maxBeats, w->getDoubleAttribute("time", 0.0));
            maxSeconds = juce::jmax(maxSeconds, w->getDoubleAttribute("contentTime", 0.0));
        }
        clip.audio().interpretation.totalBeats = maxBeats;
        if (maxSeconds > 0.0)
            clip.audio().interpretation.bpm = maxBeats * 60.0 / maxSeconds;

        clip.offsetBeats = clipElement.getDoubleAttribute("playStart", 0.0);
        if (clipElement.hasAttribute("loopStart") && clipElement.hasAttribute("loopEnd")) {
            clip.loopEnabled = true;
            clip.loopStartBeats = clipElement.getDoubleAttribute("loopStart", 0.0);
            clip.loopLengthBeats = juce::jmax(0.0, clipElement.getDoubleAttribute("loopEnd", 0.0) -
                                                       clip.loopStartBeats);
        }
        return clip;
    }

    if (auto* audioElement = clipElement.getChildByName("Audio")) {
        clip.setAudioContent();
        clip.audio().source.durationSeconds = audioElement->getDoubleAttribute("duration", 0.0);
        if (auto* fileElement = audioElement->getChildByName("File"))
            clip.audio().source.filePath = fileElement->getStringAttribute("path");

        // Source read offset and loop region (content time = seconds for audio).
        clip.offset = clipElement.getDoubleAttribute("playStart", 0.0);
        if (clipElement.hasAttribute("loopStart") && clipElement.hasAttribute("loopEnd")) {
            clip.loopEnabled = true;
            clip.loopStart = clipElement.getDoubleAttribute("loopStart", 0.0);
            clip.loopLength =
                juce::jmax(0.0, clipElement.getDoubleAttribute("loopEnd", 0.0) - clip.loopStart);
        }
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

        // <Devices> is first in the channel sequence (before Mute/Pan/Volume).
        std::vector<const DeviceInfo*> channelDevices;
        collectExportableDevices(track, channelDevices);
        if (!channelDevices.empty()) {
            auto* devices = channel->createNewChildElement("Devices");
            for (const auto* device : channelDevices)
                addDevice(*devices, *device);
        }

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
            // The arrangement Lanes are timeUnit="beats" (governs clip time/
            // duration), but a clip's inner content lives in its own content
            // time. MIDI note times and beat-locked (autoTempo) audio are
            // beats-domain; plain audio is seconds-domain. A beat-locked audio
            // clip carries a <Warps> that maps its beat content onto the source
            // seconds, so it stays tempo-synced rather than importing as a
            // fixed-rate one-shot.
            const bool beatContent = clip.isMidi() || (clip.isAudio() && clip.autoTempo);
            clipElement->setAttribute("contentTimeUnit", beatContent ? "beats" : "seconds");

            // Playback offset + loop region, in the clip's content time unit. A
            // loop region makes a short pattern repeat across the clip's
            // arrangement duration instead of importing as a one-shot.
            if (beatContent) {
                clipElement->setAttribute("playStart", clip.offsetBeats);
                if (clip.loopEnabled && clip.loopLengthBeats > 0.0) {
                    clipElement->setAttribute("loopStart", clip.loopStartBeats);
                    clipElement->setAttribute("loopEnd",
                                              clip.loopStartBeats + clip.loopLengthBeats);
                }
            } else {
                // Plain audio is seconds-domain. Read through the beats-authoritative
                // accessors (they derive seconds from beats where needed) rather
                // than the transitional raw seconds fields.
                clipElement->setAttribute("playStart", clip.getSourceOffset());
                if (clip.loopEnabled) {
                    const double loopStart = clip.getSourceLoopStart();
                    const double loopLen =
                        clip.getSourceLoopLength() > 0.0
                            ? clip.getSourceLoopLength()
                            : juce::jmax(0.0, clip.audio().source.durationSeconds - loopStart);
                    clipElement->setAttribute("loopStart", loopStart);
                    clipElement->setAttribute("loopEnd", loopStart + loopLen);
                }
            }

            if (clip.name.isNotEmpty())
                clipElement->setAttribute("name", clip.name);
            if (!clip.colour.isTransparent())
                clipElement->setAttribute("color", colourToDawProject(clip.colour));

            if (clip.isMidi())
                addNotes(*clipElement, clip, idFor("notes", clip.id));
            else if (clip.isAudio() && clip.autoTempo)
                addWarpedAudioContent(*clipElement, clip, idFor("audio", clip.id),
                                      embeddedBySource);
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
    DeviceId nextDeviceId = 1;
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

                // VST3/AU devices. State holds the archive path here; readFromFile
                // swaps it for the base64 chunk once the zip is available.
                if (auto* devices = channel->getChildByName("Devices")) {
                    for (auto* devEl : devices->getChildIterator()) {
                        const auto tag = devEl->getTagName();
                        DeviceInfo device;
                        if (tag == "Vst3Plugin")
                            device.format = PluginFormat::VST3;
                        else if (tag == "AuPlugin")
                            device.format = PluginFormat::AU;
                        else
                            continue;  // VST2/CLAP/BuiltinDevice not supported

                        device.id = nextDeviceId++;
                        device.name = devEl->getStringAttribute("deviceName");
                        device.uniqueId = devEl->getStringAttribute("deviceID");
                        device.fileOrIdentifier = device.uniqueId;
                        device.manufacturer = devEl->getStringAttribute("deviceVendor");
                        device.isInstrument =
                            devEl->getStringAttribute("deviceRole") == "instrument";
                        device.deviceType =
                            device.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
                        if (auto* enabled = devEl->getChildByName("Enabled"))
                            device.bypassed = !enabled->getBoolAttribute("value", true);
                        if (auto* state = devEl->getChildByName("State"))
                            device.pluginState = state->getStringAttribute("path");

                        track.chain.fxChainElements.push_back(std::move(device));
                    }
                }
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

std::vector<DawProjectXmlAdapter::EmbeddedDeviceState> DawProjectXmlAdapter::collectDeviceStates(
    const ProjectDocument& document) {
    std::vector<const DeviceInfo*> devices;
    for (const auto& track : document.tracks)
        collectExportableDevices(track, devices);

    std::vector<EmbeddedDeviceState> states;
    for (const auto* device : devices)
        if (device->pluginState.isNotEmpty())
            states.push_back({device->pluginState, deviceStateArchivePath(device->id)});
    return states;
}

}  // namespace magda
