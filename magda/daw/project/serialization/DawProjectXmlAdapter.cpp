#include "DawProjectXmlAdapter.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "../../core/ParameterUtils.hpp"
#include "../../core/TempoUtils.hpp"
#include "version.hpp"

namespace magda {
namespace {

juce::String idFor(const char* prefix, int value) {
    return juce::String(prefix) + juce::String(value);
}

juce::String colourToDawProject(const juce::Colour colour) {
    return "#" + colour.toDisplayString(false).toLowerCase();
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

const AutomationLaneInfo* findAbsoluteLane(const ProjectDocument& document,
                                           const AutomationTarget& target) {
    for (const auto& lane : document.automationLanes)
        if (lane.type == AutomationLaneType::Absolute && lane.target == target &&
            !lane.absolutePoints.empty())
            return &lane;
    return nullptr;
}

double gainToDb(double gain) {
    constexpr double minDb = -60.0;
    if (gain <= 0.0)
        return minDb;
    return 20.0 * std::log10(gain);
}

double dbToGain(double db) {
    constexpr double minDb = -60.0;
    if (db <= minDb)
        return 0.0;
    return std::pow(10.0, db / 20.0);
}

double normalizedAutomationToDawProjectValue(const AutomationTarget& target, double value) {
    const auto info = getParameterInfoForTarget(target);
    const auto normalized = static_cast<float>(juce::jlimit(0.0, 1.0, value));
    const auto real = static_cast<double>(ParameterUtils::normalizedToReal(normalized, info));

    if (target.kind == ControlTarget::Kind::TrackVolume)
        return dbToGain(real);
    if (target.kind == ControlTarget::Kind::TrackPan)
        return linearToDawProjectPan(static_cast<float>(real));
    return value;
}

double dawProjectValueToNormalizedAutomation(const AutomationTarget& target, double value) {
    const auto info = getParameterInfoForTarget(target);

    if (target.kind == ControlTarget::Kind::TrackVolume)
        return static_cast<double>(
            ParameterUtils::realToNormalized(static_cast<float>(gainToDb(value)), info));
    if (target.kind == ControlTarget::Kind::TrackPan)
        return static_cast<double>(
            ParameterUtils::realToNormalized(dawProjectToLinearPan(value), info));
    return juce::jlimit(0.0, 1.0, value);
}

const char* interpolationForCurve(AutomationCurveType curveType) {
    return curveType == AutomationCurveType::Step ? "hold" : "linear";
}

AutomationCurveType curveForInterpolation(const juce::String& interpolation) {
    return interpolation == "hold" ? AutomationCurveType::Step : AutomationCurveType::Linear;
}

void addAutomationPoints(juce::XmlElement& parent, const juce::String& id,
                         const juce::String& parameterId, const AutomationLaneInfo& lane) {
    auto* points = parent.createNewChildElement("Points");
    points->setAttribute("id", id);
    points->setAttribute("timeUnit", "beats");

    auto* target = points->createNewChildElement("Target");
    target->setAttribute("parameter", parameterId);

    for (const auto& point : lane.absolutePoints) {
        auto* realPoint = points->createNewChildElement("RealPoint");
        realPoint->setAttribute("time", point.beatPosition);
        realPoint->setAttribute("value",
                                normalizedAutomationToDawProjectValue(lane.target, point.value));
        realPoint->setAttribute("interpolation", interpolationForCurve(point.curveType));
    }
}

void addTrackAutomation(juce::XmlElement& trackLanes, const ProjectDocument& document,
                        const TrackInfo& track) {
    const auto volumeTarget = ControlTarget::trackVolume(track.id);
    if (auto* lane = findAbsoluteLane(document, volumeTarget))
        addAutomationPoints(trackLanes, idFor("volumeAutomation", track.id),
                            idFor("volume", track.id), *lane);

    const auto panTarget = ControlTarget::trackPan(track.id);
    if (auto* lane = findAbsoluteLane(document, panTarget))
        addAutomationPoints(trackLanes, idFor("panAutomation", track.id), idFor("pan", track.id),
                            *lane);
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

// MAGDA's native dynamics/EQ (compiled Faust) map to DAWproject's standardized
// builtins - parameter-based, so they round-trip to other hosts' stock devices.
bool isBuiltinCompressor(const DeviceInfo& device) {
    return device.pluginId == "magda_compressor";
}
bool isBuiltinGate(const DeviceInfo& device) {
    return device.pluginId == "magda_gate_expander";
}
bool isBuiltinLimiter(const DeviceInfo& device) {
    return device.pluginId == "magda_limiter";
}
bool isBuiltinEq(const DeviceInfo& device) {
    return device.pluginId == "magda_eq";
}
bool isBuiltinDevice(const DeviceInfo& device) {
    return isBuiltinCompressor(device) || isBuiltinGate(device) || isBuiltinLimiter(device) ||
           isBuiltinEq(device);
}

bool isExportableDevice(const DeviceInfo& device) {
    if (isBuiltinDevice(device))
        return true;
    const auto format = resolveDeviceFormat(device);
    return format == PluginFormat::VST3 || format == PluginFormat::AU;
}

const char* deviceElementTag(const DeviceInfo& device) {
    return resolveDeviceFormat(device) == PluginFormat::AU ? "AuPlugin" : "Vst3Plugin";
}

// A device's DAWproject state file: where it lives in the archive and its exact
// bytes. VST3 devices export their .vstpreset (loadable by other hosts); anything
// else falls back to MAGDA's opaque TE plugin-state blob. nullopt = no state.
struct DeviceStateFile {
    juce::String archivePath;
    juce::MemoryBlock bytes;
};

std::optional<DeviceStateFile> deviceStateFile(const DeviceInfo& device) {
    if (isBuiltinDevice(device))
        return std::nullopt;  // mapped as parameters, no opaque state file
    const auto base = "plugins/device-" + juce::String(static_cast<int>(device.id));

    if (device.vst3Preset.isNotEmpty()) {
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64(decoded, device.vst3Preset))
            return DeviceStateFile{base + ".vstpreset", decoded.getMemoryBlock()};
    }

    if (device.pluginState.isNotEmpty()) {
        const auto* utf8 = device.pluginState.toRawUTF8();
        return DeviceStateFile{base + ".bin", juce::MemoryBlock(utf8, std::strlen(utf8))};
    }

    return std::nullopt;
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

const ParameterInfo* findDeviceParam(const DeviceInfo& device, juce::StringRef name) {
    for (const auto& p : device.parameters)
        if (p.name == name)
            return &p;
    return nullptr;
}

const ParameterInfo* findDeviceParamByIndex(const DeviceInfo& device, int paramIndex) {
    for (const auto& p : device.parameters)
        if (p.paramIndex == paramIndex)
            return &p;
    return nullptr;
}

// Common opening element for a builtin device: the same attributes Bitwig writes,
// so other hosts treat it as a loaded stock device of that type.
juce::XmlElement& addBuiltinElement(juce::XmlElement& devices, const char* tag,
                                    const DeviceInfo& device) {
    auto* el = devices.createNewChildElement(tag);
    el->setAttribute("id", idFor("device", device.id));
    el->setAttribute("name", device.name);
    el->setAttribute("loaded", "true");
    el->setAttribute("deviceRole", "audioFX");
    el->setAttribute("deviceName", device.name);
    return *el;
}

// Emit one realParameter child for a builtin, scaling value/min/max (e.g. ms->s).
// No-op when MAGDA doesn't expose the named param.
void addMappedReal(juce::XmlElement& el, const DeviceInfo& device, const char* tag,
                   juce::StringRef paramName, const char* unit, double scale) {
    if (const auto* p = findDeviceParam(device, paramName))
        addRealParameter(el, tag, idFor(tag, device.id), tag, unit, p->currentValue * scale,
                         p->minValue * scale, p->maxValue * scale);
}

// MAGDA's native dynamics expose ms / dB / linear-ratio display values, the same
// units DAWproject uses (aside from ms->seconds). Each builtin emits the subset
// of params that has a field in its XSD type, in the XSD-mandated child order.

// <Compressor>: Attack, AutoMakeup, InputGain, OutputGain, Ratio, Release,
// Threshold. MAGDA's Knee/Mix/Detector/SC-HPF have no field and are dropped.
void addCompressorDevice(juce::XmlElement& devices, const DeviceInfo& device) {
    auto& comp = addBuiltinElement(devices, "Compressor", device);
    addMappedReal(comp, device, "Attack", "Attack", "seconds", 0.001);  // MAGDA stores ms
    if (const auto* autogain = findDeviceParam(device, "Autogain"))
        addBoolParameter(comp, "AutoMakeup", idFor("automakeup", device.id), "AutoMakeup",
                         autogain->currentValue >= 0.5f);
    addMappedReal(comp, device, "OutputGain", "Output", "decibel", 1.0);
    // DAWproject hosts (Bitwig) store compressor ratio as a 0-100% "amount"
    // (percent = (1 - 1/ratio) * 100), not a linear ratio, so emit percent.
    if (const auto* ratio = findDeviceParam(device, "Ratio")) {
        const double r = juce::jmax(1.0, static_cast<double>(ratio->currentValue));
        addRealParameter(comp, "Ratio", idFor("Ratio", device.id), "Ratio", "percent",
                         (1.0 - 1.0 / r) * 100.0, 0.0, 100.0);
    }
    addMappedReal(comp, device, "Release", "Release", "seconds", 0.001);  // MAGDA stores ms
    addMappedReal(comp, device, "Threshold", "Threshold", "decibel", 1.0);
}

// <NoiseGate>: Attack, Range, Ratio, Release, Threshold. MAGDA's Mix/Output have
// no field and are dropped.
void addGateDevice(juce::XmlElement& devices, const DeviceInfo& device) {
    auto& gate = addBuiltinElement(devices, "NoiseGate", device);
    addMappedReal(gate, device, "Attack", "Attack", "seconds", 0.001);
    addMappedReal(gate, device, "Range", "Range", "decibel", 1.0);
    addMappedReal(gate, device, "Ratio", "Ratio", "linear", 1.0);
    addMappedReal(gate, device, "Release", "Release", "seconds", 0.001);
    addMappedReal(gate, device, "Threshold", "Threshold", "decibel", 1.0);
}

// <Limiter>: Attack, InputGain, OutputGain, Release, Threshold. MAGDA's limiter
// has no InputGain; OutputGain maps to its post-limiter trim.
void addLimiterDevice(juce::XmlElement& devices, const DeviceInfo& device) {
    auto& lim = addBuiltinElement(devices, "Limiter", device);
    addMappedReal(lim, device, "Attack", "Attack", "seconds", 0.001);
    addMappedReal(lim, device, "OutputGain", "Output", "decibel", 1.0);
    addMappedReal(lim, device, "Release", "Release", "seconds", 0.001);
    addMappedReal(lim, device, "Threshold", "Threshold", "decibel", 1.0);
}

// DAWproject expresses EQ band frequency in semitones (MIDI-note pitch), the
// same convention Bitwig's EQ+ uses: Hz = 440 * 2^((s - 69)/12). Reading these
// as raw Hz drops every band well over an octave, so convert both ways.
double hzToSemitones(double hz) {
    return hz > 0.0 ? 69.0 + 12.0 * std::log2(hz / 440.0) : 0.0;
}
double semitonesToHz(double semitones) {
    return 440.0 * std::pow(2.0, (semitones - 69.0) / 12.0);
}

// MAGDA EQ band type (kBandTypeOffset slot value) -> DAWproject eqBandType. MAGDA
// has no bandPass, so it's never emitted.
const char* eqBandTypeString(float typeValue) {
    switch (juce::roundToInt(typeValue)) {
        case 0:
            return "highPass";
        case 1:
            return "lowShelf";
        case 3:
            return "highShelf";
        case 4:
            return "lowPass";
        case 5:
            return "notch";
        default:
            return "bell";  // 2
    }
}

// <Equalizer>: per-band <Band> (Freq/Gain/Q/Enabled + type attr) then OutputGain.
// magda_eq is 8 bands of 5 slots (Enabled,Type,Freq,Gain,Q) + Output at slot 40;
// slot index == ParameterInfo::paramIndex. Only enabled bands are emitted so the
// imported curve matches; MAGDA has no InputGain so it's omitted.
void addEqualizerDevice(juce::XmlElement& devices, const DeviceInfo& device) {
    constexpr int kBands = 8;
    constexpr int kSlotsPerBand = 5;
    constexpr int kOutputSlot = 40;
    auto bandParam = [&](int band, int offset) {
        return findDeviceParamByIndex(device, band * kSlotsPerBand + offset);
    };

    auto& eq = addBuiltinElement(devices, "Equalizer", device);
    int order = 0;
    for (int b = 0; b < kBands; ++b) {
        const auto* enabled = bandParam(b, 0);
        if (enabled == nullptr || enabled->currentValue < 0.5f)
            continue;
        const auto* type = bandParam(b, 1);
        const auto* freq = bandParam(b, 2);
        const auto* gain = bandParam(b, 3);
        const auto* q = bandParam(b, 4);

        auto* band = eq.createNewChildElement("Band");
        band->setAttribute("type", type != nullptr ? eqBandTypeString(type->currentValue) : "bell");
        band->setAttribute("order", order);
        const auto suffix = "_b" + juce::String(b);
        if (freq != nullptr)
            addRealParameter(*band, "Freq", idFor("freq", device.id) + suffix, "Freq", "semitones",
                             hzToSemitones(freq->currentValue), hzToSemitones(freq->minValue),
                             hzToSemitones(freq->maxValue));
        if (gain != nullptr)
            addRealParameter(*band, "Gain", idFor("gain", device.id) + suffix, "Gain", "decibel",
                             gain->currentValue, gain->minValue, gain->maxValue);
        if (q != nullptr)
            addRealParameter(*band, "Q", idFor("q", device.id) + suffix, "Q", "linear",
                             q->currentValue, q->minValue, q->maxValue);
        addBoolParameter(*band, "Enabled", idFor("banden", device.id) + suffix, "Enabled", true);
        ++order;
    }

    if (const auto* out = findDeviceParamByIndex(device, kOutputSlot))
        addRealParameter(eq, "OutputGain", idFor("OutputGain", device.id), "OutputGain", "decibel",
                         out->currentValue, out->minValue, out->maxValue);
}

// Reconstruct MAGDA's native Faust compressor from a DAWproject <Compressor>
// builtin (inverse of addCompressorDevice). Values are written at the plugin's
// host-slot indices (see MagdaCompressorCompiledPlugin: Threshold=1, Ratio=2,
// Attack=3, Release=4, Output=8, Autogain=14) in the plugin's display units
// (dB, ms, linear ratio), honouring each DAWproject param's `unit`. Foreign
// params we can't invert cleanly (e.g. Bitwig writes Ratio as a percent scale)
// are skipped, leaving the plugin default.
// Push one ParameterInfo at a builtin's host-slot index, in display units.
// syncFromDeviceInfo applies it via setParameterByIndex; the name is cosmetic.
void addBuiltinParam(DeviceInfo& device, int slot, juce::StringRef name, double value) {
    ParameterInfo p;
    p.paramIndex = slot;
    p.name = name;
    p.currentValue = static_cast<float>(value);
    device.parameters.push_back(std::move(p));
}

// DAWproject's standard time unit is seconds; convert to MAGDA's ms. Honour an
// explicit ms unit just in case a host writes one.
double dawSecondsToMs(const juce::XmlElement& e) {
    const double v = e.getDoubleAttribute("value");
    const auto unit = e.getStringAttribute("unit");
    return (unit == "milliseconds" || unit == "ms") ? v : v * 1000.0;
}

double dawProjectRatioToLinear(const juce::XmlElement& e) {
    const double v = e.getDoubleAttribute("value");
    if (e.getStringAttribute("unit") == "percent")
        return v >= 100.0 ? 50.0 : 1.0 / (1.0 - v / 100.0);
    return v;
}

void initBuiltinDevice(DeviceInfo& device, const juce::XmlElement& el, const char* pluginId,
                       const char* defaultName) {
    device.format = PluginFormat::Internal;
    device.pluginId = pluginId;
    device.deviceType = DeviceType::Effect;
    device.isInstrument = false;
    if (device.name.isEmpty())
        device.name = el.getStringAttribute("name", defaultName);
}

// Slots from MagdaCompressorCompiledPlugin: Threshold=1, Ratio=2, Attack=3,
// Release=4, Output=8, Autogain=14.
void parseCompressorDevice(const juce::XmlElement& comp, DeviceInfo& device) {
    initBuiltinDevice(device, comp, "magda_compressor", "Compressor");
    if (auto* t = comp.getChildByName("Threshold"))
        addBuiltinParam(device, 1, "Threshold", t->getDoubleAttribute("value"));
    if (auto* r = comp.getChildByName("Ratio")) {
        // Honour the source unit. DAWproject/Bitwig store ratio as a 0-100%
        // "amount" (ratio = 1/(1 - percent/100)); some writers use a plain
        // linear ratio. MAGDA's ratio is linear, clamped to its [1, 50] range.
        addBuiltinParam(device, 2, "Ratio", juce::jlimit(1.0, 50.0, dawProjectRatioToLinear(*r)));
    }
    if (auto* a = comp.getChildByName("Attack"))
        addBuiltinParam(device, 3, "Attack", dawSecondsToMs(*a));
    if (auto* rel = comp.getChildByName("Release"))
        addBuiltinParam(device, 4, "Release", dawSecondsToMs(*rel));
    if (auto* o = comp.getChildByName("OutputGain"))
        addBuiltinParam(device, 8, "Output", o->getDoubleAttribute("value"));
    if (auto* mk = comp.getChildByName("AutoMakeup"))
        addBuiltinParam(device, 14, "Autogain", mk->getBoolAttribute("value") ? 1.0 : 0.0);
}

// Slots from MagdaGateExpanderCompiledPlugin: Attack=0, Release=1, Threshold=4,
// Ratio=5, Range=6.
void parseGateDevice(const juce::XmlElement& gate, DeviceInfo& device) {
    initBuiltinDevice(device, gate, "magda_gate_expander", "Gate");
    if (auto* a = gate.getChildByName("Attack"))
        addBuiltinParam(device, 0, "Attack", dawSecondsToMs(*a));
    if (auto* rel = gate.getChildByName("Release"))
        addBuiltinParam(device, 1, "Release", dawSecondsToMs(*rel));
    if (auto* t = gate.getChildByName("Threshold"))
        addBuiltinParam(device, 4, "Threshold", t->getDoubleAttribute("value"));
    if (auto* r = gate.getChildByName("Ratio"))
        addBuiltinParam(device, 5, "Ratio", juce::jlimit(1.0, 50.0, dawProjectRatioToLinear(*r)));
    if (auto* rg = gate.getChildByName("Range"))
        addBuiltinParam(device, 6, "Range", rg->getDoubleAttribute("value"));
}

// Slots from MagdaLimiterCompiledPlugin: Threshold=0, Attack=1, Release=2,
// Output=3. DAWproject InputGain has no MAGDA counterpart (skipped).
void parseLimiterDevice(const juce::XmlElement& lim, DeviceInfo& device) {
    initBuiltinDevice(device, lim, "magda_limiter", "Limiter");
    if (auto* t = lim.getChildByName("Threshold"))
        addBuiltinParam(device, 0, "Threshold", t->getDoubleAttribute("value"));
    if (auto* a = lim.getChildByName("Attack"))
        addBuiltinParam(device, 1, "Attack", dawSecondsToMs(*a));
    if (auto* rel = lim.getChildByName("Release"))
        addBuiltinParam(device, 2, "Release", dawSecondsToMs(*rel));
    if (auto* o = lim.getChildByName("OutputGain"))
        addBuiltinParam(device, 3, "Output",
                        juce::jlimit(-24.0, 0.0, o->getDoubleAttribute("value")));
}

// DAWproject eqBandType -> MAGDA EQ band type slot value. MAGDA has no bandPass;
// fall back to Bell (the closest peaking-with-gain filter).
int eqBandTypeValue(const juce::String& type) {
    if (type == "highPass")
        return 0;
    if (type == "lowShelf")
        return 1;
    if (type == "highShelf")
        return 3;
    if (type == "lowPass")
        return 4;
    if (type == "notch")
        return 5;
    return 2;  // bell + bandPass fallback
}

// magda_eq: 8 bands of 5 slots (Enabled,Type,Freq,Gain,Q) + Output at slot 40.
// Take the first 8 <Band>s in document order; extra bands are dropped, missing
// MAGDA bands stay at their disabled defaults. DAWproject InputGain has no target.
void parseEqualizerDevice(const juce::XmlElement& eq, DeviceInfo& device) {
    initBuiltinDevice(device, eq, "magda_eq", "EQ");
    constexpr int kBands = 8;
    constexpr int kSlotsPerBand = 5;
    constexpr int kOutputSlot = 40;

    int b = 0;
    for (auto* band : eq.getChildWithTagNameIterator("Band")) {
        if (b >= kBands)
            break;
        const int base = b * kSlotsPerBand;
        addBuiltinParam(device, base + 1, "Type",
                        eqBandTypeValue(band->getStringAttribute("type", "bell")));
        bool enabled = true;
        if (auto* en = band->getChildByName("Enabled"))
            enabled = en->getBoolAttribute("value", true);
        addBuiltinParam(device, base + 0, "Enabled", enabled ? 1.0 : 0.0);
        if (auto* f = band->getChildByName("Freq")) {
            double hz = f->getDoubleAttribute("value");
            if (f->getStringAttribute("unit") == "semitones")
                hz = semitonesToHz(hz);
            addBuiltinParam(device, base + 2, "Freq", juce::jlimit(20.0, 20000.0, hz));
        }
        if (auto* g = band->getChildByName("Gain"))
            addBuiltinParam(device, base + 3, "Gain",
                            juce::jlimit(-24.0, 24.0, g->getDoubleAttribute("value")));
        if (auto* q = band->getChildByName("Q"))
            addBuiltinParam(device, base + 4, "Q",
                            juce::jlimit(0.1, 10.0, q->getDoubleAttribute("value")));
        ++b;
    }

    if (auto* out = eq.getChildByName("OutputGain"))
        addBuiltinParam(device, kOutputSlot, "Output",
                        juce::jlimit(-24.0, 12.0, out->getDoubleAttribute("value")));
}

void addDevice(juce::XmlElement& devices, const DeviceInfo& device) {
    if (isBuiltinCompressor(device)) {
        addCompressorDevice(devices, device);
        return;
    }
    if (isBuiltinGate(device)) {
        addGateDevice(devices, device);
        return;
    }
    if (isBuiltinLimiter(device)) {
        addLimiterDevice(devices, device);
        return;
    }
    if (isBuiltinEq(device)) {
        addEqualizerDevice(devices, device);
        return;
    }

    auto* dev = devices.createNewChildElement(deviceElementTag(device));
    // referenceable id + name and loaded="true" mirror what Bitwig writes;
    // without loaded="true" Bitwig treats the device as not-loaded and drops it.
    dev->setAttribute("id", idFor("device", device.id));
    dev->setAttribute("name", device.name);
    dev->setAttribute("loaded", "true");
    dev->setAttribute("deviceRole", device.isInstrument ? "instrument" : "audioFX");
    dev->setAttribute("deviceName", device.name);

    // Prefer the real VST3 class id (what other hosts match on); fall back to
    // MAGDA's identifier only when it isn't a VST3 / the id wasn't captured.
    const auto deviceId = device.vst3ClassId.isNotEmpty() ? device.vst3ClassId
                          : device.uniqueId.isNotEmpty()  ? device.uniqueId
                                                          : device.fileOrIdentifier;
    if (deviceId.isNotEmpty())
        dev->setAttribute("deviceID", deviceId);
    if (device.manufacturer.isNotEmpty())
        dev->setAttribute("deviceVendor", device.manufacturer);

    // device sequence is Parameters, Enabled, State. We skip Parameters (the
    // State chunk is what actually restores the plugin).
    addBoolParameter(*dev, "Enabled", idFor("deviceEnabled", device.id), "Enabled",
                     !device.bypassed);

    if (auto stateFile = deviceStateFile(device)) {
        auto* state = dev->createNewChildElement("State");
        state->setAttribute("path", stateFile->archivePath);
        state->setAttribute("external", "false");
    }
}

juce::XmlElement& addClipElement(juce::XmlElement& parent, const ClipInfo& clip, double clipTime,
                                 const std::map<juce::String, juce::String>& embeddedBySource) {
    auto* clipElement = parent.createNewChildElement("Clip");
    clipElement->setAttribute("time", clipTime);
    clipElement->setAttribute("duration", clip.placement.lengthBeats);
    // The parent timeline uses beats for clip time/duration, but a clip's inner
    // content lives in its own content time. MIDI note times and beat-locked
    // audio are beats-domain; plain audio is seconds-domain.
    const bool beatContent = clip.isMidi() || (clip.isAudio() && clip.autoTempo);
    clipElement->setAttribute("contentTimeUnit", beatContent ? "beats" : "seconds");

    // Playback offset + loop region, in the clip's content time unit.
    if (beatContent) {
        clipElement->setAttribute("playStart", clip.offsetBeats);
        if (clip.loopEnabled && clip.loopLengthBeats > 0.0) {
            clipElement->setAttribute("loopStart", clip.loopStartBeats);
            clipElement->setAttribute("loopEnd", clip.loopStartBeats + clip.loopLengthBeats);
        }
    } else {
        // Plain audio is seconds-domain. Read through the beats-authoritative
        // accessors rather than the transitional raw seconds fields.
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
        addWarpedAudioContent(*clipElement, clip, idFor("audio", clip.id), embeddedBySource);
    else if (clip.isAudio())
        addAudioContent(*clipElement, clip, idFor("audio", clip.id), embeddedBySource);

    return *clipElement;
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

    // `destination` is an IDREF, so only route to the master channel when a
    // master track is actually present in the document (it isn't for partial
    // documents built outside a full capture).
    const bool hasMaster =
        std::any_of(document.tracks.begin(), document.tracks.end(),
                    [](const TrackInfo& t) { return t.type == TrackType::Master; });

    auto* structure = project.createNewChildElement("Structure");
    std::map<TrackId, const TrackInfo*> tracksById;
    for (const auto& track : document.tracks)
        tracksById[track.id] = &track;

    auto childTracksFor = [&](const TrackInfo& parent) {
        std::vector<const TrackInfo*> children;
        for (auto childId : parent.childIds) {
            if (auto it = tracksById.find(childId); it != tracksById.end())
                children.push_back(it->second);
        }

        if (children.empty()) {
            for (const auto& candidate : document.tracks)
                if (candidate.parentId == parent.id)
                    children.push_back(&candidate);
        }

        return children;
    };

    std::function<void(juce::XmlElement&, const TrackInfo&)> addTrackElement =
        [&](juce::XmlElement& parent, const TrackInfo& track) {
            auto* trackElement = parent.createNewChildElement("Track");
            trackElement->setAttribute("id", idFor("track", track.id));
            trackElement->setAttribute("name", track.name);
            trackElement->setAttribute("contentType", contentTypeForTrack(track, document.clips));
            trackElement->setAttribute("loaded", "true");
            if (!track.colour.isTransparent())
                trackElement->setAttribute("color", colourToDawProject(track.colour));

            auto* channel = trackElement->createNewChildElement("Channel");
            channel->setAttribute("id", idFor("channel", track.id));
            // Role mirrors MAGDA's track type: the master is "master", aux/return
            // tracks are "effect", everything else "regular". Non-master channels
            // output to the master channel via `destination`.
            const char* role = track.type == TrackType::Master ? "master"
                               : track.type == TrackType::Aux  ? "effect"
                                                               : "regular";
            channel->setAttribute("role", role);
            channel->setAttribute("audioChannels", 2);
            if (track.type != TrackType::Master && hasMaster)
                channel->setAttribute("destination", idFor("channel", MASTER_TRACK_ID));
            channel->setAttribute("solo", track.soloed ? "true" : "false");

            // Channel child order is fixed by the schema: Devices, Mute, Pan,
            // Sends, Volume.
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

            // Sends to aux/return tracks. Each routes to the destination track's
            // channel; level is linear, type carries pre/post-fader.
            if (!track.sends.empty()) {
                auto* sends = channel->createNewChildElement("Sends");
                int sendCounter = 0;
                for (const auto& send : track.sends) {
                    if (send.destTrackId == INVALID_TRACK_ID)
                        continue;
                    auto* sendEl = sends->createNewChildElement("Send");
                    const auto sendId = idFor("send", track.id) + "_" + juce::String(sendCounter++);
                    sendEl->setAttribute("destination", idFor("channel", send.destTrackId));
                    sendEl->setAttribute("type", send.preFader ? "pre" : "post");
                    sendEl->setAttribute("id", sendId);
                    addBoolParameter(*sendEl, "Enable", sendId + "_en", "Enable", true);
                    addRealParameter(*sendEl, "Volume", sendId + "_vol", "Send", "linear",
                                     send.level, 0.0, 1.0);
                }
            }

            addRealParameter(*channel, "Volume", idFor("volume", track.id), "Volume", "linear",
                             track.volume, 0.0, 2.0);

            for (const auto* child : childTracksFor(track))
                addTrackElement(*trackElement, *child);
        };

    for (const auto& track : document.tracks) {
        if (track.parentId != INVALID_TRACK_ID && tracksById.count(track.parentId) > 0)
            continue;
        addTrackElement(*structure, track);
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

            addClipElement(*clips, clip, clip.placement.startBeat, embeddedBySource);
        }

        addTrackAutomation(*trackLanes, document, track);
    }

    auto* scenes = project.createNewChildElement("Scenes");
    std::set<int> sceneIndices;
    for (const auto& clip : document.clips)
        if (clip.view == ClipView::Session && clip.sceneIndex >= 0)
            sceneIndices.insert(clip.sceneIndex);

    for (int sceneIndex : sceneIndices) {
        auto* scene = scenes->createNewChildElement("Scene");
        scene->setAttribute("id", idFor("scene", sceneIndex));
        scene->setAttribute("name", "Scene " + juce::String(sceneIndex + 1));

        auto* sceneLanes = scene->createNewChildElement("Lanes");
        sceneLanes->setAttribute("id", idFor("sceneLanes", sceneIndex));
        sceneLanes->setAttribute("timeUnit", "beats");

        for (const auto& track : document.tracks) {
            for (const auto& clip : document.clips) {
                if (clip.trackId != track.id || clip.view != ClipView::Session ||
                    clip.sceneIndex != sceneIndex)
                    continue;

                auto* slot = sceneLanes->createNewChildElement("ClipSlot");
                slot->setAttribute("id", idFor("clipSlot", clip.id));
                slot->setAttribute("track", idFor("track", track.id));
                slot->setAttribute("hasStop", "true");
                addClipElement(*slot, clip, 0.0, embeddedBySource);
                break;
            }
        }
    }
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
    std::map<juce::String, AutomationTarget> parameterTargets;
    TrackId nextTrackId = 1;
    DeviceId nextDeviceId = 1;
    AutomationLaneId nextAutomationLaneId = 1;
    AutomationPointId nextAutomationPointId = 1;

    // Send routing is resolved in a second pass: a <Send> references the target
    // channel's id, which may belong to an effect/aux track declared later.
    // Map channel id -> imported track index + aux bus, and stash each send to
    // resolve once every track (and its bus) is known.
    std::map<juce::String, size_t> channelToTrackIndex;
    std::map<juce::String, int> channelToAuxBus;
    struct PendingSend {
        size_t sourceTrackIndex;
        juce::String destChannel;
        float level;
        bool preFader;
    };
    std::vector<PendingSend> pendingSends;
    int nextAuxBus = 0;

    if (auto* structure = root->getChildByName("Structure")) {
        std::function<void(juce::XmlElement*, TrackId)> parseTrackElement =
            [&](juce::XmlElement* trackElement, TrackId parentId) {
                TrackInfo track;
                track.id = nextTrackId++;
                track.name =
                    trackElement->getStringAttribute("name", "Track " + juce::String(track.id));
                track.colour = colourFromDawProject(trackElement->getStringAttribute("color"));
                const auto contentType = trackElement->getStringAttribute("contentType");
                track.type = contentType.contains("tracks") ? TrackType::Group : TrackType::Audio;
                track.parentId = parentId;
                const size_t trackIndex = document.tracks.size();

                if (auto* channel = trackElement->getChildByName("Channel")) {
                    // The channel role drives the MAGDA track type. "master" routes
                    // into MAGDA's singleton master (see toStagedProjectData);
                    // "effect" is an aux/return track and gets a unique bus that its
                    // sources' sends reference.
                    const auto role = channel->getStringAttribute("role", "regular");
                    const auto channelId = channel->getStringAttribute("id");
                    if (role == "master") {
                        track.type = TrackType::Master;
                    } else if (role == "effect") {
                        track.type = TrackType::Aux;
                        track.auxBusIndex = nextAuxBus++;
                        channelToAuxBus[channelId] = track.auxBusIndex;
                    }
                    if (channelId.isNotEmpty())
                        channelToTrackIndex[channelId] = trackIndex;

                    // Sends to aux/effect channels. Skip disabled sends (Bitwig
                    // writes a default disabled send from every track to every
                    // effect bus); resolve the destination bus in the second pass.
                    if (auto* sends = channel->getChildByName("Sends")) {
                        for (auto* sendEl : sends->getChildWithTagNameIterator("Send")) {
                            if (auto* en = sendEl->getChildByName("Enable");
                                en != nullptr && !en->getBoolAttribute("value", true))
                                continue;
                            const auto dest = sendEl->getStringAttribute("destination");
                            if (dest.isEmpty())
                                continue;
                            float level = 1.0f;
                            if (auto* vol = sendEl->getChildByName("Volume"))
                                level = static_cast<float>(vol->getDoubleAttribute("value", 1.0));
                            pendingSends.push_back({trackIndex, dest, level,
                                                    sendEl->getStringAttribute("type") == "pre"});
                        }
                    }

                    if (auto* volume = channel->getChildByName("Volume"))
                        track.volume = static_cast<float>(volume->getDoubleAttribute("value", 1.0));
                    if (auto* volume = channel->getChildByName("Volume")) {
                        const auto parameterId = volume->getStringAttribute("id");
                        if (parameterId.isNotEmpty())
                            parameterTargets[parameterId] = ControlTarget::trackVolume(track.id);
                    }
                    if (auto* pan = channel->getChildByName("Pan")) {
                        track.pan = dawProjectToLinearPan(pan->getDoubleAttribute("value", 0.5));
                        const auto parameterId = pan->getStringAttribute("id");
                        if (parameterId.isNotEmpty())
                            parameterTargets[parameterId] = ControlTarget::trackPan(track.id);
                    }
                    if (auto* mute = channel->getChildByName("Mute"))
                        track.muted = mute->getBoolAttribute("value", false);
                    track.soloed = channel->getBoolAttribute("solo", false);

                    // VST3/AU devices. State holds the archive path here; readFromFile
                    // swaps it for the base64 chunk once the zip is available.
                    if (auto* devices = channel->getChildByName("Devices")) {
                        for (auto* devEl : devices->getChildIterator()) {
                            const auto tag = devEl->getTagName();
                            DeviceInfo device;
                            device.id = nextDeviceId++;

                            // Standardized builtins: reconstruct MAGDA's native
                            // device instead of treating it as opaque.
                            if (tag == "Compressor" || tag == "NoiseGate" || tag == "Limiter" ||
                                tag == "Equalizer") {
                                if (tag == "Compressor")
                                    parseCompressorDevice(*devEl, device);
                                else if (tag == "NoiseGate")
                                    parseGateDevice(*devEl, device);
                                else if (tag == "Limiter")
                                    parseLimiterDevice(*devEl, device);
                                else
                                    parseEqualizerDevice(*devEl, device);
                                if (auto* enabled = devEl->getChildByName("Enabled"))
                                    device.bypassed = !enabled->getBoolAttribute("value", true);
                                track.chain.fxChainElements.push_back(std::move(device));
                                continue;
                            }

                            if (tag == "Vst3Plugin")
                                device.format = PluginFormat::VST3;
                            else if (tag == "AuPlugin")
                                device.format = PluginFormat::AU;
                            else
                                continue;  // VST2/CLAP/other builtins not supported

                            device.name = devEl->getStringAttribute("deviceName");
                            const auto deviceId = devEl->getStringAttribute("deviceID");
                            // deviceID is the VST3 class id (32-hex). Keep it as the
                            // portable identity so a re-export preserves it. Matching
                            // it back to an installed plugin for load is best-effort
                            // (MAGDA matches by uniqueId/path, which DAWproject lacks).
                            if (device.format == PluginFormat::VST3)
                                device.vst3ClassId = deviceId;
                            device.uniqueId = deviceId;
                            device.fileOrIdentifier = deviceId;
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
                const TrackId currentTrackId = track.id;
                document.tracks.push_back(std::move(track));

                if (parentId != INVALID_TRACK_ID) {
                    for (auto& parentTrack : document.tracks) {
                        if (parentTrack.id == parentId) {
                            parentTrack.childIds.push_back(document.tracks.back().id);
                            break;
                        }
                    }
                }

                for (auto* childElement : trackElement->getChildWithTagNameIterator("Track"))
                    parseTrackElement(childElement, currentTrackId);
            };

        for (auto* trackElement : structure->getChildWithTagNameIterator("Track"))
            parseTrackElement(trackElement, INVALID_TRACK_ID);
    }

    // Second pass: wire sends now that every aux bus is known. Drop sends whose
    // destination isn't an imported aux/effect channel.
    for (const auto& ps : pendingSends) {
        const auto busIt = channelToAuxBus.find(ps.destChannel);
        if (busIt == channelToAuxBus.end())
            continue;
        SendInfo send;
        send.busIndex = busIt->second;
        send.level = ps.level;
        send.preFader = ps.preFader;
        if (const auto idxIt = channelToTrackIndex.find(ps.destChannel);
            idxIt != channelToTrackIndex.end())
            send.destTrackId = document.tracks[idxIt->second].id;
        document.tracks[ps.sourceTrackIndex].sends.push_back(send);
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

                for (auto* pointsElement : trackLanes->getChildWithTagNameIterator("Points")) {
                    auto* targetElement = pointsElement->getChildByName("Target");
                    if (targetElement == nullptr)
                        continue;

                    auto targetIt =
                        parameterTargets.find(targetElement->getStringAttribute("parameter"));
                    if (targetIt == parameterTargets.end())
                        continue;

                    const auto& target = targetIt->second;
                    if (target.devicePath.trackId != trackIt->second)
                        continue;

                    AutomationLaneInfo lane;
                    lane.id = nextAutomationLaneId++;
                    lane.target = target;
                    lane.type = AutomationLaneType::Absolute;
                    lane.paramName =
                        target.kind == ControlTarget::Kind::TrackPan ? "Pan" : "Volume";

                    for (auto* pointElement :
                         pointsElement->getChildWithTagNameIterator("RealPoint")) {
                        AutomationPoint point;
                        point.id = nextAutomationPointId++;
                        point.beatPosition = pointElement->getDoubleAttribute("time", 0.0);
                        point.value = dawProjectValueToNormalizedAutomation(
                            target, pointElement->getDoubleAttribute("value", 0.0));
                        point.curveType = curveForInterpolation(
                            pointElement->getStringAttribute("interpolation", "linear"));
                        lane.absolutePoints.push_back(point);
                    }

                    if (!lane.absolutePoints.empty()) {
                        std::sort(lane.absolutePoints.begin(), lane.absolutePoints.end());
                        document.automationLanes.push_back(std::move(lane));
                    }
                }
            }
        }
    }

    if (auto* scenes = root->getChildByName("Scenes")) {
        int sceneOrdinal = 0;
        for (auto* sceneElement : scenes->getChildWithTagNameIterator("Scene")) {
            const auto sceneId = sceneElement->getStringAttribute("id");
            const int sceneIndex = sceneId.startsWith("scene") && sceneId.length() > 5
                                       ? sceneId.substring(5).getIntValue()
                                       : sceneOrdinal;
            std::function<void(juce::XmlElement*, juce::String)> parseSceneTimeline =
                [&](juce::XmlElement* timeline, juce::String inheritedTrackRef) {
                    if (timeline == nullptr)
                        return;

                    auto trackRef = timeline->getStringAttribute("track", inheritedTrackRef);
                    if (timeline->hasTagName("ClipSlot")) {
                        auto trackIt = trackIds.find(trackRef);
                        if (trackIt == trackIds.end())
                            return;

                        if (auto* clipElement = timeline->getChildByName("Clip")) {
                            auto clip = clipFromXml(*clipElement, trackIt->second, nextClipId++);
                            clip.view = ClipView::Session;
                            clip.sceneIndex = sceneIndex;
                            clip.setPlacementBeats(0.0, clip.placement.lengthBeats);
                            document.clips.push_back(std::move(clip));
                        }
                        return;
                    }

                    for (auto* child : timeline->getChildIterator())
                        parseSceneTimeline(child, trackRef);
                };

            parseSceneTimeline(sceneElement->getChildByName("Lanes"), {});
            if (auto* slot = sceneElement->getChildByName("ClipSlot"))
                parseSceneTimeline(slot, {});
            ++sceneOrdinal;
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
        if (auto stateFile = deviceStateFile(*device))
            states.push_back({std::move(stateFile->bytes), std::move(stateFile->archivePath)});
    return states;
}

}  // namespace magda
