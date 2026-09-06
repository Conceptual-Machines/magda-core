#include "DawProjectRoundTrip.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "core/SourcePool.hpp"
#include "magda/daw/project/serialization/DawProjectArchive.hpp"
#include "magda/daw/project/serialization/ProjectDocument.hpp"

namespace magda::nulldiff {

// =============================================================================
// The losses
// =============================================================================
//
// Each entry names a field, says why DAWproject cannot hold it, and puts it
// back. Nothing here is a tolerance: a restore either copies the original's
// value over an imported default or does nothing, and doing nothing is what the
// runner fails on.
//
// The bar is in the header. A field belongs here when the case sets it and the
// trip does not bring it back, whether or not anything reads it.

namespace {

/// The clip of @p value named @p name, or nullptr. Names are unique per case
/// and the runner asserts it, so a second match cannot silently win.
const ClipInfo* clipNamed(const Case& value, const juce::String& name) {
    for (const auto& clip : value.clips)
        if (clip.name == name)
            return &clip;
    return nullptr;
}

/// Walk both cases' primary audio events in matched pairs, applying @p apply.
///
/// Matched by clip name rather than by index: the importer's clip order follows
/// the file, and a restore that paired by position would put one clip's fade on
/// another the first time that order changed.
bool forEachAudioEvent(Case& imported, const Case& original,
                       const std::function<bool(AudioEvent&, const AudioEvent&)>& apply) {
    bool restored = false;

    for (auto& clip : imported.clips) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            continue;

        const auto* source = clipNamed(original, clip.name);
        if (source == nullptr)
            continue;

        const auto* sourceEvent = source->primaryEvent();
        if (sourceEvent == nullptr)
            continue;

        restored = apply(*event, *sourceEvent) || restored;
    }

    return restored;
}

bool forEachClip(Case& imported, const Case& original,
                 const std::function<bool(ClipInfo&, const ClipInfo&)>& apply) {
    bool restored = false;

    for (auto& clip : imported.clips)
        if (const auto* source = clipNamed(original, clip.name))
            restored = apply(clip, *source) || restored;

    return restored;
}

Loss fades() {
    return {.field = "AudioEvent::fadeInSeconds, fadeOutSeconds, fadeInType, fadeOutType, "
                     "fadeInBehaviour, fadeOutBehaviour",
            .reason = "a DAWproject clip has no fade of its own. The format puts a level "
                      "envelope on an automation lane instead, which is a different object "
                      "with different semantics: it cannot say that this clip's edge is a "
                      "speed ramp rather than a gain ramp, and it outlives the clip being "
                      "moved",
            .restore = [](Case& imported, const Case& original) {
                return forEachAudioEvent(imported, original,
                                         [](AudioEvent& event, const AudioEvent& source) {
                                             if (event.fadeInSeconds == source.fadeInSeconds &&
                                                 event.fadeOutSeconds == source.fadeOutSeconds &&
                                                 event.fadeInType == source.fadeInType &&
                                                 event.fadeOutType == source.fadeOutType &&
                                                 event.fadeInBehaviour == source.fadeInBehaviour &&
                                                 event.fadeOutBehaviour == source.fadeOutBehaviour)
                                                 return false;

                                             event.fadeInSeconds = source.fadeInSeconds;
                                             event.fadeOutSeconds = source.fadeOutSeconds;
                                             event.fadeInType = source.fadeInType;
                                             event.fadeOutType = source.fadeOutType;
                                             event.fadeInBehaviour = source.fadeInBehaviour;
                                             event.fadeOutBehaviour = source.fadeOutBehaviour;
                                             return true;
                                         });
            }};
}

Loss reversed() {
    return {.field = "AudioEvent::reversed",
            .reason = "the format has no reverse flag, and its warp markers cannot stand in "
                      "for one: they map source time onto timeline time monotonically, so a "
                      "run of them going backwards is not expressible",
            .restore = [](Case& imported, const Case& original) {
                return forEachAudioEvent(imported, original,
                                         [](AudioEvent& event, const AudioEvent& source) {
                                             if (event.reversed == source.reversed)
                                                 return false;
                                             event.reversed = source.reversed;
                                             return true;
                                         });
            }};
}

Loss speedRatio() {
    return {.field = "AudioEvent::speedRatio",
            .reason = "a fixed playback ratio has no attribute in the format. Warp markers "
                      "could express the same mapping, but MAGDA's exporter writes them only "
                      "for a beat-locked clip, where the ratio is the tempo's rather than the "
                      "clip's",
            .restore = [](Case& imported, const Case& original) {
                return forEachAudioEvent(imported, original,
                                         [](AudioEvent& event, const AudioEvent& source) {
                                             if (event.speedRatio == source.speedRatio)
                                                 return false;
                                             event.speedRatio = source.speedRatio;
                                             return true;
                                         });
            }};
}

Loss stretchMode() {
    return {.field = "AudioEvent::timeStretchMode",
            .reason = "which stretcher runs is an engine choice rather than project data, and "
                      "the format is deliberately host neutral about it. A file naming MAGDA's "
                      "stretcher would be naming something no other host has",
            .restore = [](Case& imported, const Case& original) {
                return forEachAudioEvent(imported, original,
                                         [](AudioEvent& event, const AudioEvent& source) {
                                             if (event.timeStretchMode == source.timeStretchMode)
                                                 return false;
                                             event.timeStretchMode = source.timeStretchMode;
                                             return true;
                                         });
            }};
}

Loss pitch() {
    return {.field = "AudioEvent::analogPitch, transpose",
            .reason = "the format carries no clip transposition. Analog pitch would not fit "
                      "one if it did: it folds the interval into the read rate, so it is a "
                      "property of how the clip is played rather than a number to restore",
            .restore = [](Case& imported, const Case& original) {
                return forEachAudioEvent(imported, original,
                                         [](AudioEvent& event, const AudioEvent& source) {
                                             if (event.analogPitch == source.analogPitch &&
                                                 event.transpose == source.transpose)
                                                 return false;
                                             event.analogPitch = source.analogPitch;
                                             event.transpose = source.transpose;
                                             return true;
                                         });
            }};
}

Loss warp() {
    return {.field = "AudioEvent::warpEnabled, warpMarkers",
            .reason = "MAGDA's exporter writes <Warps> only for a beat-locked clip, and then "
                      "as the two linear markers that carry its tempo interpretation. A clip's "
                      "own marker list goes out as neither, so it comes back unwarped",
            .restore = [](Case& imported, const Case& original) {
                return forEachAudioEvent(imported, original,
                                         [](AudioEvent& event, const AudioEvent& source) {
                                             if (event.warpEnabled == source.warpEnabled &&
                                                 event.warpMarkers == source.warpMarkers)
                                                 return false;
                                             event.warpEnabled = source.warpEnabled;
                                             event.warpMarkers = source.warpMarkers;
                                             return true;
                                         });
            }};
}

Loss takesAndComp() {
    return {.field = "AudioClipModel::takes, currentTakeIndex, comp, compActive",
            .reason = "loop-record takes and a comp assignment are MAGDA's own recording "
                      "state. The format has one source per clip and no vocabulary for the "
                      "passes behind it",
            .restore = [](Case& imported, const Case& original) {
                return forEachClip(imported, original, [](ClipInfo& clip, const ClipInfo& source) {
                    if (!clip.isAudio() || !source.isAudio())
                        return false;

                    auto& audio = clip.audio();
                    const auto& from = source.audio();
                    if (audio.takes == from.takes && audio.comp == from.comp &&
                        audio.currentTakeIndex == from.currentTakeIndex &&
                        audio.compActive == from.compActive)
                        return false;

                    audio.takes = from.takes;
                    audio.comp = from.comp;
                    audio.currentTakeIndex = from.currentTakeIndex;
                    audio.compActive = from.compActive;
                    return true;
                });
            }};
}

Loss controllers() {
    return {.field = "ClipInfo::midiCCData, midiPitchBendData, MidiNote::pitchExpression",
            .reason = "the format's <Notes> carries pitch, velocity and duration and nothing "
                      "else. Continuous controllers, pitch bend and per-note expression have "
                      "no element to go in",
            .restore = [](Case& imported, const Case& original) {
                return forEachClip(imported, original, [](ClipInfo& clip, const ClipInfo& source) {
                    bool restored = false;

                    if (clip.midiCCData != source.midiCCData) {
                        clip.midiCCData = source.midiCCData;
                        restored = true;
                    }
                    if (clip.midiPitchBendData != source.midiPitchBendData) {
                        clip.midiPitchBendData = source.midiPitchBendData;
                        restored = true;
                    }

                    // Expression is per note, and the notes themselves did make
                    // the trip, so it goes back onto them in order rather than
                    // by replacing the list: a note that came back wrong has to
                    // stay wrong for the comparison to find it.
                    const auto notes = std::min(clip.midiNotes.size(), source.midiNotes.size());
                    for (std::size_t index = 0; index < notes; ++index) {
                        if (clip.midiNotes[index].pitchExpression ==
                            source.midiNotes[index].pitchExpression)
                            continue;
                        clip.midiNotes[index].pitchExpression =
                            source.midiNotes[index].pitchExpression;
                        restored = true;
                    }

                    return restored;
                });
            }};
}

Loss groove() {
    return {.field = "ClipInfo::grooveTemplate, grooveStrength",
            .reason = "a groove names a template in a library the project does not contain, so "
                      "there is nothing portable to write. The format's own answer is to bake "
                      "the shifted times into the notes, which is a different clip",
            .restore = [](Case& imported, const Case& original) {
                return forEachClip(imported, original, [](ClipInfo& clip, const ClipInfo& source) {
                    if (clip.grooveTemplate == source.grooveTemplate &&
                        clip.grooveStrength == source.grooveStrength)
                        return false;
                    clip.grooveTemplate = source.grooveTemplate;
                    clip.grooveStrength = source.grooveStrength;
                    return true;
                });
            }};
}

Loss internalDevices() {
    return {.field = "TrackInfo::chain",
            .reason = "MAGDA's own devices and its racks have no DAWproject representation. The "
                      "format standardises four dynamics and EQ builtins and otherwise carries "
                      "VST3 and AU by class id and state chunk, and an internal device is "
                      "neither: it would come back as a plugin no host could load",
            .restore = [](Case& imported, const Case& original) {
                bool restored = false;

                // The whole chain, and only when the count changed. Two things
                // follow, and both are deliberate.
                //
                // Counted rather than compared, because a ChainElement holds a
                // rack by owning pointer and has no equality. A chain that came
                // back the right length carrying the wrong devices is therefore
                // left alone here, which is the correct outcome: the plan dump
                // prints every device's id and section, so the comparison is
                // where that belongs rather than in a restore.
                //
                // Wholesale, because every device in this corpus is internal
                // and none of them survives. The day a case carries a VST3
                // alongside one, restoring the chain would put the exported
                // device back too and stop comparing what the format did carry.
                // That case does not exist yet (#1893 brings the first hosted
                // plugin), and a surgical version written for it now would be
                // code no test runs.
                const auto restoreChain = [&restored](TrackInfo& track, const TrackInfo& source) {
                    if (track.chain.fxChainElements.size() == source.chain.fxChainElements.size() &&
                        track.chain.postFxChainElements.size() ==
                            source.chain.postFxChainElements.size())
                        return;

                    track.chain = source.chain;
                    restored = true;
                };

                for (auto& track : imported.tracks)
                    for (const auto& source : original.tracks)
                        if (source.name == track.name)
                            restoreChain(track, source);

                restoreChain(imported.master, original.master);
                return restored;
            }};
}

Loss tempoMap() {
    return {.field = "Case::tempo",
            .reason = "the format's <Transport> holds one tempo. Later changes belong on a "
                      "tempo automation lane, which MAGDA's exporter does not write and its "
                      "importer does not read, so a project with a tempo change comes back "
                      "playing its first tempo throughout",
            .restore = [](Case& imported, const Case& original) {
                if (imported.tempo.size() == original.tempo.size())
                    return false;
                imported.tempo = original.tempo;
                return true;
            }};
}

/// The table. Keyed by case name so that the corpus stays a description of what
/// the two engines have to agree about, with nothing in it about a file format.
/// A track's own modulation, which rides on TrackInfo rather than on its chain.
///
/// Separate from internalDevices(): the chain is lost because the format has no
/// internal device, this is lost because it has no modulation at all.
///
/// Restored by counting what is linked rather than by comparing the arrays.
/// Every track carries sixteen macros and the empty ones survive as empty, so
/// comparing the arrays would fire on a case that lost nothing.
Loss trackModulation() {
    return {.field = "TrackInfo::macros, TrackInfo::mods",
            .reason = "DAWproject has no representation for modulation: no macro knob, and "
                      "nothing that means an LFO linked to a parameter at a depth. A track's "
                      "macros and modifiers come back as the empty defaults",
            .restore = [](Case& imported, const Case& original) {
                const auto links = [](const TrackInfo& track) {
                    auto count = std::size_t{0};
                    for (const auto& macro : track.macros)
                        count += macro.links.size();
                    for (const auto& mod : track.mods)
                        if (mod.enabled)
                            count += mod.links.size();
                    return count;
                };

                bool restored = false;

                for (auto& track : imported.tracks)
                    for (const auto& source : original.tracks) {
                        if (source.name != track.name || links(track) == links(source))
                            continue;

                        track.macros = source.macros;
                        track.mods = source.mods;
                        restored = true;
                    }

                return restored;
            }};
}

Loss multiOutRouting() {
    return {.field = "TrackInfo::type, TrackInfo::multiOutLink",
            .reason = "an instrument's further output pairs have no DAWproject representation, "
                      "and neither does a track whose whole content is one of them. The format "
                      "gives a channel one audio output and a destination; a MultiOut track is "
                      "a second reader of a device that lives on another track, which is a "
                      "relationship between a track and a device rather than between two "
                      "channels. It comes back as an ordinary audio track with no link",
            .restore = [](Case& imported, const Case& original) {
                bool restored = false;

                for (auto& track : imported.tracks)
                    for (const auto& source : original.tracks) {
                        if (source.name != track.name)
                            continue;
                        if (track.type == source.type &&
                            track.multiOutLink.has_value() == source.multiOutLink.has_value())
                            continue;

                        track.type = source.type;
                        track.multiOutLink = source.multiOutLink;
                        restored = true;
                    }

                return restored;
            }};
}

/// Every top-level device of @p track, paired with the one it went out as.
/// What the two device-level losses below both need.
///
/// By id, which is exact and which these can rely on: the trip has already put
/// the surviving devices back onto the ids they went out under, by portable
/// identity and occurrence, before any loss is restored. Pairing by name here
/// would reintroduce the ambiguity that mapping exists to avoid.
void forEachPairedDevice(TrackInfo& track, const TrackInfo& source,
                         const std::function<void(DeviceInfo&, const DeviceInfo&)>& visit) {
    for (auto& element : track.chain.fxChainElements) {
        if (!isDevice(element))
            continue;

        auto& device = getDevice(element);

        for (const auto& candidate : source.chain.fxChainElements) {
            if (!isDevice(candidate) || getDevice(candidate).id != device.id)
                continue;

            visit(device, getDevice(candidate));
            break;
        }
    }
}

/// A device's key input, which is a relationship rather than a property (#2246).
Loss deviceSidechain() {
    return {.field = "DeviceInfo::sidechain",
            .reason = "DAWproject has no cross-track key routing. A device arrives as a plugin "
                      "identity and a state blob, and which track feeds its sidechain is neither "
                      "of those: it is a reference from a device to a track, which the format "
                      "has nowhere to put. It comes back unrouted",
            .restore = [](Case& imported, const Case& original) {
                bool restored = false;

                for (auto& track : imported.tracks)
                    for (const auto& source : original.tracks) {
                        if (source.name != track.name)
                            continue;

                        forEachPairedDevice(
                            track, source,
                            [&restored](DeviceInfo& device, const DeviceInfo& saved) {
                                if (device.sidechain.isActive() == saved.sidechain.isActive())
                                    return;

                                device.sidechain = saved.sidechain;
                                restored = true;
                            });
                    }

                return restored;
            }};
}

/// The dry and wet levels in front of a hosted plugin, which are the host's own
/// numbers and not the plugin's (#2246).
Loss deviceWrapperMix() {
    return {.field = "DeviceInfo::wrapperParameters",
            .reason = "the pair of levels in front of an external plugin belongs to the host "
                      "rather than to the plugin: the fork injects it, MAGDA persists it, and "
                      "the plugin has never heard of it. DAWproject carries a device as an "
                      "identity and a blob of its own state, and neither has anywhere to put a "
                      "number the plugin did not author. It comes back empty, which the engine "
                      "reads as the pair's own default of fully wet -- so a project saved at "
                      "40% wet returns as one that is not",
            .restore = [](Case& imported, const Case& original) {
                const auto sameMix = [](const DeviceInfo& device, const DeviceInfo& saved) {
                    if (device.wrapperParameters.size() != saved.wrapperParameters.size())
                        return false;

                    for (std::size_t index = 0; index < saved.wrapperParameters.size(); ++index)
                        if (device.wrapperParameters[index].currentValue !=
                            saved.wrapperParameters[index].currentValue)
                            return false;

                    return true;
                };

                bool restored = false;

                for (auto& track : imported.tracks)
                    for (const auto& source : original.tracks) {
                        if (source.name != track.name)
                            continue;

                        forEachPairedDevice(
                            track, source,
                            [&restored, &sameMix](DeviceInfo& device, const DeviceInfo& saved) {
                                if (sameMix(device, saved))
                                    return;

                                device.wrapperParameters = saved.wrapperParameters;
                                restored = true;
                            });
                    }

                return restored;
            }};
}

/// What a device's MIDI ports do, which is read off a live plugin (#2246).
Loss deviceMidiPorts() {
    return {.field = "DeviceInfo::producesMidi, DeviceInfo::midiInThru",
            .reason = "a device's MIDI capabilities are what the live plugin reported and what "
                      "the user chose to do with them, and the format carries neither: it names "
                      "a plugin and hands over its state. They come back at the defaults, which "
                      "is a plugin that emits no MIDI and a chain that passes its own through",
            .restore = [](Case& imported, const Case& original) {
                bool restored = false;

                for (auto& track : imported.tracks)
                    for (const auto& source : original.tracks) {
                        if (source.name != track.name)
                            continue;

                        forEachPairedDevice(
                            track, source,
                            [&restored](DeviceInfo& device, const DeviceInfo& saved) {
                                if (device.producesMidi == saved.producesMidi &&
                                    device.midiInThru == saved.midiInThru)
                                    return;

                                device.producesMidi = saved.producesMidi;
                                device.midiInThru = saved.midiInThru;
                                restored = true;
                            });
                    }

                return restored;
            }};
}

const std::map<std::string, std::vector<Loss>>& lossTable() {
    static const std::map<std::string, std::vector<Loss>> table{
        {"fades.curves", {fades()}},
        {"fades.speedramp", {fades()}},
        {"reverse.plain", {reversed()}},
        {"speed.ratio", {speedRatio()}},
        {"pitch.analog", {pitch()}},
        {"tempo.auto", {stretchMode(), tempoMap()}},
        {"stretch.signalsmith", {speedRatio(), stretchMode()}},
        {"stretch.soundtouch.normal", {speedRatio(), stretchMode()}},
        {"stretch.soundtouch.better", {speedRatio(), stretchMode()}},
        {"stretch.broadband", {speedRatio(), stretchMode()}},
        {"warp.audio", {warp(), stretchMode()}},
        {"takes.comp", {takesAndComp()}},
        {"param.base", {internalDevices()}},
        {"param.automation", {internalDevices()}},
        {"param.modifier", {internalDevices(), trackModulation()}},
        {"param.both", {internalDevices(), trackModulation()}},
        {"param.hostwrite.automation", {internalDevices()}},
        {"param.hostwrite.modifier", {internalDevices(), trackModulation()}},
        {"macro.track", {internalDevices(), trackModulation()}},
        {"macro.device", {internalDevices()}},
        // Rack-scope macros and a device-scope follower both ride inside the
        // chain -- on RackInfo::macros and on DeviceInfo::mods -- so the one
        // declaration that loses the chain loses them with it, and restoring
        // the chain puts them back. Neither wants trackModulation() beside it:
        // that covers what rides on TrackInfo, and a declared loss that finds
        // nothing to restore fails here rather than sitting in the table.
        //
        // The three send cases are deliberately absent. DAWproject has sends
        // and aux channels of its own, so they make the trip intact, and that
        // is a result rather than an omission: the format carries the one piece
        // of mixer topology the corpus has.
        {"macro.rack", {internalDevices()}},
        {"mod.follower", {internalDevices()}},
        // The rack cases (#2139). A rack is chains of internal devices, and the
        // format has nowhere to put either half, so the same declaration covers
        // the whole of it: the chain comes back empty and is restored wholesale.
        {"rack.parallel", {internalDevices()}},
        {"rack.chain.fader", {internalDevices()}},
        {"rack.fader", {internalDevices()}},
        {"rack.chain.mute", {internalDevices()}},
        {"rack.chain.solo", {internalDevices()}},
        {"rack.deltasolo.device", {internalDevices()}},
        {"rack.deltasolo.rack", {internalDevices()}},
        {"rack.latency", {internalDevices()}},
        {"rack.nested", {internalDevices()}},
        {"rack.nested.latency", {internalDevices()}},
        {"rack.aux", {internalDevices()}},
        {"rack.mono", {internalDevices()}},
        {"rack.instrument", {internalDevices()}},
        // The one hosted case with a rack in it (#2246). The rack goes the way
        // every other rack goes, and it takes the plugin inside it along:
        // there is no chain to put a device back into once the container it
        // sat in has no representation.
        {"plugin.narrow.slot", {internalDevices()}},
        {"plugin.wetdry", {deviceWrapperMix()}},
        {"plugin.wetdry.dry", {deviceWrapperMix()}},
        {"plugin.sidechain", {deviceSidechain()}},
        {"plugin.instrument.midiout", {deviceMidiPorts()}},
        {"plugin.instrument.midiout.thru", {deviceMidiPorts()}},
        {"plugin.faust.instrument", {internalDevices()}},
        {"plugin.sampler", {internalDevices()}},
        {"plugin.compiled.instrument", {internalDevices()}},
        {"multiout.pair", {internalDevices(), multiOutRouting()}},
        {"project.mixed", {internalDevices()}},
        {"midi.notes", {internalDevices()}},
        {"midi.cc", {internalDevices(), controllers()}},
        {"midi.mpe", {internalDevices(), controllers()}},
        {"midi.fold", {internalDevices(), groove()}},
        {"midi.offset", {internalDevices()}},
        // The session cases (#2441). The slot itself makes the trip -- the
        // format has scenes and clip slots, and the clip comes back in the
        // right one with its notes and its length -- so the only declaration
        // either of them needs is the one every instrument track in the corpus
        // needs, and the audio one has no chain to lose.
        {"session.launch.midi", {internalDevices()}},
    };

    return table;
}

}  // namespace

const std::vector<Loss>& declaredLosses(const std::string& caseName) {
    static const std::vector<Loss> none;

    const auto& table = lossTable();
    const auto found = table.find(caseName);
    return found == table.end() ? none : found->second;
}

std::vector<std::string> namesWithDeclaredLosses() {
    std::vector<std::string> names;
    for (const auto& entry : lossTable())
        names.push_back(entry.first);
    return names;
}

// =============================================================================
// The trip
// =============================================================================

namespace {

/// The project half of a case, as the interchange boundary wants it.
///
/// The master goes last, after every ordinary track. Not for the importer's
/// benefit, which reads a channel's role rather than its position, but for the
/// file's: `destination` is an IDREF to the master's channel, and a reader that
/// resolved references in one pass would want the tracks that reference it
/// written first. Ids are not recovered from order either way; see the header.
ProjectDocument documentFor(const Case& value) {
    ProjectDocument document;
    document.info.name = value.name;
    document.info.tempo = value.startBpm();

    document.tracks = value.tracks;
    document.tracks.push_back(value.master);
    document.clips = value.clips;

    return document;
}

/// Names, mapped to the ids they had before the trip.
///
/// Empty when a name repeats, which the caller turns into a refusal: a mapping
/// that guessed could put a clip on the wrong track and then certify the plan
/// it compiled to.
template <typename T>
std::map<juce::String, decltype(T::id)> idsByName(const std::vector<T>& values) {
    std::map<juce::String, decltype(T::id)> byName;

    for (const auto& value : values)
        if (!byName.emplace(value.name, value.id).second)
            return {};

    return byName;
}

/**
 * @brief The names a case went out under, handed back one at a time.
 *
 * Both halves of the mapping need this, and the discipline it enforces is not
 * the obvious one. Rejecting duplicates in the ORIGINAL is necessary and not
 * sufficient: an import that returns two tracks called "One" where "One" and
 * "Two" went out resolves both onto One's id, and a count of what came back
 * still matches, because two things came back and two went out. The harness
 * would have turned a track that went missing into a duplicate of its
 * neighbour, renumbered the pair into a consistent-looking project, and then
 * compared that against the original. Normalising the exact failure a name
 * mapping exists to make impossible.
 *
 * So a name is claimed rather than looked up, and the two ways that can go
 * wrong are separate refusals: a name nothing went out under, and a name a
 * second thing has already taken. Anything left unclaimed at the end did not
 * come back at all, which is the third.
 */
template <typename Id> class NameClaims {
  public:
    NameClaims(const char* what, std::map<juce::String, Id> byName)
        : what_(what), byName_(std::move(byName)) {}

    /// The id @p name went out under, or nothing, with @p refusal set.
    std::optional<Id> claim(const juce::String& name, std::string& refusal) {
        const auto found = byName_.find(name);
        if (found == byName_.end()) {
            refusal = std::string("a ") + what_ + " came back named '" + name.toStdString() +
                      "', which nothing went out under";
            return std::nullopt;
        }

        if (!claimed_.insert(name).second) {
            refusal = std::string("two ") + what_ + "s came back named '" + name.toStdString() +
                      "', so one of them is standing in for something that did not come back";
            return std::nullopt;
        }

        return found->second;
    }

    /// Whether everything that went out came back, naming one thing that did
    /// not in @p refusal.
    bool allClaimed(std::string& refusal) const {
        for (const auto& entry : byName_) {
            if (claimed_.count(entry.first) > 0)
                continue;

            refusal =
                std::string("no ") + what_ + " came back named '" + entry.first.toStdString() + "'";
            return false;
        }

        return true;
    }

  private:
    const char* what_;
    std::map<juce::String, Id> byName_;
    std::set<juce::String> claimed_;
};

/**
 * @brief Everything the imported clips play, as the native leg wants to be
 *        told it.
 *
 * Read back out of the pool rather than carried over from the original case.
 * The archive extracted these files into @p media under their archive-relative
 * names and the importer pooled them there, so a source list copied from the
 * original would point the render at the files that never made the trip.
 *
 * And every one of them has to be under @p media, which is the half that is not
 * obvious. The exporter falls back to an absolute `external="true"` reference
 * for a source it cannot embed, and the importer leaves such a path alone
 * because there is nothing in the zip to extract. Both are correct behaviour on
 * their own, and together they hand this back the ORIGINAL file: the clip
 * resolves, the render nulls, and the case has certified an export that
 * embedded nothing and an import that extracted nothing. That is the one way
 * this harness could report a null it had not earned, so a source resolving
 * outside the extraction directory is a refusal rather than a source.
 */
std::vector<SourceFact> pooledSourcesFor(const std::vector<ClipInfo>& clips,
                                         const juce::File& media, std::string& refusal) {
    std::vector<SourceFact> sources;
    std::set<SourceId> seen;

    for (const auto& clip : clips) {
        const auto* event = clip.primaryEvent();
        if (event == nullptr || event->sourceId == INVALID_SOURCE_ID)
            continue;
        if (!seen.insert(event->sourceId).second)
            continue;

        const auto* source = SourcePool::getInstance().get(event->sourceId);
        if (source == nullptr) {
            refusal = "a clip came back pointing at a source the pool does not have";
            return {};
        }

        if (!juce::File(source->filePath).isAChildOf(media)) {
            refusal = "a clip came back playing '" + source->filePath.toStdString() +
                      "', which is not a file this trip extracted: the export embedded it as an "
                      "external reference, so nothing was written to the archive and nothing was "
                      "read back out of it";
            return {};
        }

        sources.push_back(
            {source->id, source->filePath, source->sampleRate, source->durationSeconds});
    }

    return sources;
}

/**
 * @brief Imported track ids, mapped back onto the ids the same names went out
 *        under.
 *
 * Both sides of the map have to be one to one, and they are one to one for
 * different reasons. The values are, because NameClaims hands each original id
 * out once. The keys are not guaranteed by anything the importer does, so they
 * are checked here: two imported tracks that came back under distinct names
 * carrying one id would otherwise collapse to a single entry, and takeTracks
 * would then look both of them up and give both the same original id. Every
 * clip on either would follow it.
 *
 * That case has no count to fall back on. A map with one entry for two tracks
 * still leaves every name claimed, so this is the only thing standing between
 * an import that duplicated an id and a project renumbered into looking
 * consistent.
 */
std::map<TrackId, TrackId> mapTrackIds(const ProjectDocument& imported,
                                       const std::vector<TrackInfo>& original,
                                       std::string& refusal) {
    auto byName = idsByName(original);
    if (byName.empty() && !original.empty()) {
        refusal = "two of this case's tracks share a name, so ids cannot be mapped back";
        return {};
    }

    NameClaims<TrackId> claims("track", std::move(byName));
    std::map<TrackId, TrackId> mapped;

    for (const auto& track : imported.tracks) {
        const auto id = claims.claim(track.name, refusal);
        if (!id.has_value())
            return {};

        if (!mapped.emplace(track.id, *id).second) {
            refusal = "two tracks came back carrying id " + std::to_string(track.id) +
                      ", the second of them named '" + track.name.toStdString() +
                      "', so one would have been mapped onto the other";
            return {};
        }
    }

    if (!claims.allClaimed(refusal))
        return {};

    return mapped;
}

/// Split the imported tracks into the shape the compiler takes, renumbering as
/// they go. The master arrives as an ordinary track whose channel says so.
bool takeTracks(ProjectDocument& imported, const std::map<TrackId, TrackId>& trackIds, Case& value,
                std::string& refusal) {
    bool sawMaster = false;

    for (auto& track : imported.tracks) {
        track.id = trackIds.at(track.id);

        if (track.type != TrackType::Master) {
            value.tracks.push_back(std::move(track));
            continue;
        }

        if (sawMaster) {
            refusal = "the project came back with two master tracks";
            return false;
        }

        sawMaster = true;
        value.master = std::move(track);
    }

    if (!sawMaster) {
        refusal = "the project came back with no master track";
        return false;
    }

    return true;
}

bool takeClips(ProjectDocument& imported, const std::map<TrackId, TrackId>& trackIds,
               const Case& original, Case& value, std::string& refusal) {
    auto clipIds = idsByName(original.clips);
    if (clipIds.empty() && !original.clips.empty()) {
        refusal = "two of this case's clips share a name, so ids cannot be mapped back";
        return false;
    }

    NameClaims<ClipId> claims("clip", std::move(clipIds));

    for (auto& clip : imported.clips) {
        const auto track = trackIds.find(clip.trackId);
        if (track == trackIds.end()) {
            refusal = "a clip came back on a track that is not in the project";
            return false;
        }
        clip.trackId = track->second;

        const auto id = claims.claim(clip.name, refusal);
        if (!id.has_value())
            return false;

        clip.id = *id;
        value.clips.push_back(std::move(clip));
    }

    return claims.allClaimed(refusal);
}

/**
 * @brief The identity the format carries for a device, on either side of the
 *        trip (#2246).
 *
 * The same preference the exporter writes its `deviceID` from -- a VST3's class
 * id, then JUCE's own identifier string, then the file it was loaded from --
 * which is what makes it comparable across the trip: the importer puts that one
 * attribute back into all three fields, so a device asked this question before
 * and after answers with the same string.
 *
 * Deliberately not the display name. A name is what a user typed and what a
 * browser showed; two instances of one plugin on one chain share it, and a
 * mapping keyed on it would have to refuse a project that is perfectly ordinary.
 *
 * Empty for a device the format could not identify at all, which is a device it
 * did not carry either: nothing comes back for it, and the chain it sat in is
 * restored from the original as a declared loss.
 */
juce::String portableIdentity(const DeviceInfo& device) {
    if (device.vst3ClassId.isNotEmpty())
        return device.vst3ClassId;
    if (device.uniqueId.isNotEmpty())
        return device.uniqueId;
    return device.fileOrIdentifier;
}

/// Every top-level device of @p elements, in chain order.
std::vector<const DeviceInfo*> topLevelDevices(const std::vector<ChainElement>& elements) {
    std::vector<const DeviceInfo*> devices;

    for (const auto& element : elements)
        if (isDevice(element))
            devices.push_back(&getDevice(element));

    return devices;
}

/**
 * @brief Put the devices that came back onto the ids they went out under
 *        (#2246).
 *
 * The same rule tracks and clips live by, applied one level further in, and it
 * arrived with the first case whose device survives the trip: a hosted plugin
 * is carried by the format, so a case with one compiles a plan whose ops are
 * keyed on a device id the importer chose. That is a renumbering rather than a
 * loss, and the difference matters -- restoring the chain wholesale, the way a
 * case of internal devices does, would put the exported plugin back too and
 * stop comparing what the format actually carried.
 *
 * Matched by portable identity and occurrence rather than by name. Two copies of
 * one plugin on one chain is an ordinary project, and a mapping that refused it
 * would be writing a harness limitation into what the corpus is allowed to
 * contain: so the nth device carrying an identity takes the id of the nth device
 * that went out carrying it, in chain order, and the name is only ever printed.
 *
 * That keeps a reorder visible, which is the property this could have lost. Two
 * instances of one plugin swapped are the same project and compile to the same
 * plan; two different devices swapped keep their own ids and compile to a plan
 * whose ops are in the other order, which the dump comparison reads straight
 * off.
 *
 * Unlike a track or a clip, a device that did not come back is not a refusal
 * here. Most of them do not: MAGDA's own devices and its racks have no
 * representation at all, and that is declared as a loss and restored from the
 * original. What is refused is a device that came back carrying an identity
 * nothing went out under, or a further copy of one, which are the two ways a
 * mapping could quietly put one device's id on another.
 *
 * Top-level chain devices only. It is where the corpus's hosted plugins sit,
 * and the sections the format has no shape for arrive empty rather than
 * renumbered.
 */
bool mapDeviceIds(Case& value, const Case& original, std::string& refusal) {
    const auto mapTrack = [&refusal](TrackInfo& track, const TrackInfo& source) {
        // The ids that went out, per identity, in chain order. A deque because
        // matching is first come first served: the second instance of a plugin
        // to come back is the second that went out.
        std::map<juce::String, std::deque<DeviceId>> byIdentity;
        for (const auto* device : topLevelDevices(source.chain.fxChainElements))
            byIdentity[portableIdentity(*device)].push_back(device->id);

        for (auto& element : track.chain.fxChainElements) {
            if (!isDevice(element))
                continue;

            auto& device = getDevice(element);
            const auto identity = portableIdentity(device);

            const auto found = byIdentity.find(identity);
            if (found == byIdentity.end() || found->second.empty()) {
                refusal = "a device came back on track '" + track.name.toStdString() + "' as '" +
                          device.name.toStdString() + "' (" + identity.toStdString() +
                          "), which nothing went out as";
                return false;
            }

            device.id = found->second.front();
            found->second.pop_front();
        }

        return true;
    };

    for (auto& track : value.tracks) {
        const auto source =
            std::find_if(original.tracks.begin(), original.tracks.end(),
                         [&track](const TrackInfo& candidate) { return candidate.id == track.id; });

        if (source == original.tracks.end()) {
            refusal = "a track came back that nothing went out as";
            return false;
        }

        if (!mapTrack(track, *source))
            return false;
    }

    return mapTrack(value.master, original.master);
}

}  // namespace

RoundTrip exportAndReimport(const Case& original, const juce::File& scratchDirectory) {
    RoundTrip result;

    scratchDirectory.createDirectory();

    // One archive and one extraction directory per case, named after it. Shared
    // ones would let a case read the audio the previous case extracted, which
    // is a null nobody earned, and the names are what make a failure
    // inspectable afterwards without rerunning anything.
    const auto archive = scratchDirectory.getChildFile(juce::String(original.name) + ".dawproject");
    const auto media = scratchDirectory.getChildFile(juce::String(original.name) + "-media");
    archive.deleteFile();
    media.deleteRecursively();

    juce::String error;
    if (!DawProjectArchive::writeToFile(archive, documentFor(original), error)) {
        result.refusal = "the export refused the project: " + error.toStdString();
        return result;
    }

    ProjectDocument imported;
    if (!DawProjectArchive::readFromFile(archive, imported, error, media)) {
        result.refusal = "the import refused the archive: " + error.toStdString();
        return result;
    }

    auto originalTracks = original.tracks;
    originalTracks.push_back(original.master);

    const auto trackIds = mapTrackIds(imported, originalTracks, result.refusal);
    if (!result.refusal.empty())
        return result;

    // The environment, the beat range, the tier and its allowances are how the
    // corpus renders a case rather than anything the file has an opinion about,
    // so they are carried over. Everything the trip is actually about is
    // cleared and refilled from what came back.
    result.value = original;
    result.value.tracks.clear();
    result.value.clips.clear();
    result.value.sources.clear();
    result.value.tempo = {{.beat = 0.0, .bpm = imported.info.tempo}};

    if (!takeTracks(imported, trackIds, result.value, result.refusal))
        return result;
    if (!takeClips(imported, trackIds, original, result.value, result.refusal))
        return result;

    if (!mapDeviceIds(result.value, original, result.refusal))
        return result;

    result.value.sources = pooledSourcesFor(result.value.clips, media, result.refusal);
    if (!result.refusal.empty())
        return result;

    for (const auto& loss : declaredLosses(original.name))
        result.losses.push_back({loss.field, loss.reason, loss.restore(result.value, original)});

    return result;
}

}  // namespace magda::nulldiff
