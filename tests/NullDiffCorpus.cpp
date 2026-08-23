#include <array>
#include <cmath>
#include <functional>
#include <map>

#include "NullDiffCase.hpp"
#include "NullDiffGain.hpp"
#include "NullDiffMaterial.hpp"
#include "core/DeviceInfo.hpp"
#include "core/SourcePool.hpp"
#include "core/TimeStretchModes.hpp"

/**
 * The corpus itself (#2040): every case, as model values.
 *
 * The table is the argument. Each case names one thing the two engines have to
 * agree about, plays material chosen so that a residual can only be a bug, and
 * declares what agreement it claims. Nothing here knows how either engine is
 * driven.
 *
 * Two things are deliberately absent, and both for the reason the issue gives.
 *
 * Overlaps, and therefore holes and crossfades. Tracktion has no correct
 * behaviour to diff against there, and giving it one is the double work that
 * decision avoided. A hole is what occlusion leaves behind and occlusion is
 * what an overlap is, and a crossfade is an overlap the model shaped, so all
 * three are one exclusion rather than three. They are covered by the model
 * suites, which pin every rule, and by reference-executor assertions: a fully
 * covered region renders silent, a crossfade sums to unity, a play-through
 * overlap sums both sources.
 *
 * Reverse together with warp. The incumbent cannot do it at all: it bakes one
 * rendered proxy per clip and WaveAudioClip::createRenderJob returns the
 * reverse job, so the markers are silently lost. There is nothing to diff, and
 * the case would be asserting that the engine reproduces a bug.
 */

namespace magda::nulldiff {

const char* const kGrooveName = "null-diff swing";

namespace {

constexpr double kRate = 44100.0;
constexpr double kBpm = 120.0;

constexpr TrackId kTrack = 1;
constexpr DeviceId kInstrument = 900;

/// Long enough that no case runs out of material, short enough that twenty of
/// them cost a second of disk.
constexpr double kSourceSeconds = 12.0;

// --- tracks ------------------------------------------------------------------

TrackInfo plainTrack() {
    TrackInfo track;
    track.id = kTrack;
    track.type = TrackType::Audio;
    track.name = "Null Diff";
    track.audioOutputDevice = "master";
    return track;
}

/// A track whose chain consumes MIDI, which is what makes the plan compile a
/// ClipMidi op at all: no consumer, no source. The device stands for the synth
/// a MIDI clip would be playing, and in both legs it is replaced by something
/// that records what arrives instead of making a sound. That is the comparison
/// point, and the only one that means anything: what a synth receives.
TrackInfo instrumentTrackOn(TrackId trackId, DeviceId deviceId, const char* name) {
    TrackInfo track;
    track.id = trackId;
    track.type = TrackType::Audio;
    track.name = name;
    track.audioOutputDevice = "master";

    DeviceInfo device;
    device.id = deviceId;
    device.name = "Capture";
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    // What it is, rather than what the struct defaults to. DeviceInfo::format
    // starts at VST3, and nothing in either leg reads it, so the corpus has been
    // declaring three external plugins it does not host. The block-size gate
    // does read it (#2078): a project with a plugin in it is compared within an
    // epsilon and one without is held to bit identity, so a stand-in device
    // mislabelled as a VST3 would hand the whole corpus an allowance it has no
    // use for and no right to.
    device.format = PluginFormat::Internal;
    track.chain.fxChainElements.emplace_back(std::move(device));

    return track;
}

TrackInfo instrumentTrack() {
    return instrumentTrackOn(kTrack, kInstrument, "Null Diff");
}

/// A second MIDI-consuming device behind the first.
///
/// The chain's MIDI survives a device that does not replace it, so both devices
/// receive the same notes. That makes the number of devices on a track a thing
/// the two legs can disagree about: the incumbent records once, at the head of
/// the plugin list, and a native leg reading every Device op would record the
/// same note once per device. Only a track with two of them can say which
/// happened.
void addSecondInstrument(TrackInfo& track, DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Capture 2";
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.format = PluginFormat::Internal;
    track.chain.fxChainElements.emplace_back(std::move(device));
}

/// An audio effect, which consumes no MIDI as far as the compiler is concerned.
///
/// Two different things follow from that, and only one of them is about this
/// device's own MIDI input. Every device is wired to the chain's MIDI
/// (makeRoutingNode sets MidiInputPolicy::Chain for all of them), so an effect
/// sitting ahead of an instrument still receives the same notes and taking it as
/// the tap would read the right stream anyway.
///
/// What it does change is the track. chainConsumesMidi asks whether any device
/// is an instrument or accepts MIDI, so a track carrying only this one consumes
/// none: the plan compiles no ClipMidi op for it and the incumbent puts no
/// capture on it. A harness that nominated a tap for every track with a device
/// would hand back an empty stream for that track against the incumbent's
/// nothing at all, and the runner would report it as captured by one leg only.
void addEffect(TrackInfo& track, DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect";
    device.deviceType = DeviceType::Effect;
    device.isInstrument = false;
    device.canReceiveMidi = false;
    device.format = PluginFormat::Internal;

    track.chain.fxChainElements.emplace_back(std::move(device));
}

TrackInfo masterTrack() {
    TrackInfo master;
    master.id = MASTER_TRACK_ID;
    master.type = TrackType::Master;
    master.name = "Master";
    return master;
}

/// One of several tracks in a mixer case, all feeding master.
///
/// The mixer cases are the first in the corpus with more than one track, and
/// that is the point of them: a fader, a pan law, a mute and a solo are all
/// resolved by the value layer and none of it has ever been compared against
/// the incumbent. They are arithmetic, so nothing interpolates between the two
/// engines and the ordinary floor applies.
TrackInfo mixTrack(TrackId id, const char* name) {
    TrackInfo track;
    track.id = id;
    track.type = TrackType::Audio;
    track.name = name;
    track.audioOutputDevice = "master";
    return track;
}

/// A track carrying one gain device, the only device either engine really runs
/// (#2123, NullDiffGain.hpp). The parameter's stored value is the case's, which
/// is what a host write is; a case that leaves it at unity has none in it.
TrackInfo gainTrackOn(TrackId trackId, DeviceId deviceId, const char* name, float base) {
    TrackInfo track;
    track.id = trackId;
    track.type = TrackType::Audio;
    track.name = name;
    track.audioOutputDevice = "master";
    track.chain.fxChainElements.emplace_back(gainDevice(deviceId, base));
    return track;
}

// --- racks -------------------------------------------------------------------
//
// A rack is the one structure in the chain that is not a list: parallel chains
// over one input, each with its own fader and its own switches, summed at a
// rack output that has a second fader with a different law behind it. Every
// piece of that is built here out of the same gain device the parameter cases
// run, so what a rack case measures is the topology rather than a new device.

/// A rack chain holding gain devices in the order and at the values given.
ChainInfo gainChain(ChainId id, const char* name,
                    const std::vector<std::pair<DeviceId, float>>& gains) {
    ChainInfo chain;
    chain.id = id;
    chain.name = name;
    for (const auto& [deviceId, base] : gains)
        chain.elements.emplace_back(gainDevice(deviceId, base));
    return chain;
}

/// One gain device in a chain of its own, which is the common shape.
ChainInfo gainChain(ChainId id, const char* name, DeviceId deviceId, float base) {
    return gainChain(id, name, {{deviceId, base}});
}

/// A rack over the chains given, at unity and centred.
RackInfo rackOf(RackId id, const char* name, std::vector<ChainInfo> chains) {
    RackInfo rack;
    rack.id = id;
    rack.name = name;
    rack.chains = std::move(chains);
    return rack;
}

/// The instrument stand-in, as a chain element.
///
/// The same device `instrumentTrackOn` puts on a track: an instrument as far as
/// the compiler is concerned, replaced in both legs by something that records
/// what arrives rather than making a sound.
DeviceInfo chainInstrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Capture";
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.format = PluginFormat::Internal;
    return device;
}

/// A track whose FX chain is that one rack.
TrackInfo rackTrack(TrackId trackId, const char* name, RackInfo rack) {
    TrackInfo track;
    track.id = trackId;
    track.type = TrackType::Audio;
    track.name = name;
    track.audioOutputDevice = "master";
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));
    return track;
}

/// Where a link or a lane addresses that device's one parameter.
ControlTarget gainTarget(TrackId trackId, DeviceId deviceId) {
    return ControlTarget::pluginParam(ChainNodePath::topLevelDevice(trackId, deviceId),
                                      kGainParamIndex);
}

/// A curve that holds and jumps rather than one that travels.
///
/// Both engines settle a parameter at the top of a block, so they agree
/// everywhere a curve is holding still and can differ by up to a block wherever
/// it moves: a ramping case would measure block alignment. So the steps land on
/// the half beat and the impulses land on the beat, which at 120 bpm is 11025
/// samples of silence either side of every jump -- a block at every size the
/// invariance gate renders at (#2078).
AutomationLaneInfo stepLane(AutomationLaneId id, const ControlTarget& target,
                            std::vector<std::pair<double, double>> steps) {
    AutomationLaneInfo lane;
    lane.id = id;
    lane.target = target;
    lane.type = AutomationLaneType::Absolute;
    lane.authorityState = AutomationAuthorityState::Reading;
    lane.paramName = "Gain";

    AutomationPointId pointId = 1;
    for (const auto& [beat, value] : steps) {
        AutomationPoint point;
        point.id = pointId++;
        point.beatPosition = beat;
        point.value = value;
        point.curveType = AutomationCurveType::Step;
        lane.absolutePoints.push_back(point);
    }

    return lane;
}

/// A square LFO locked to the transport, driving @p target.
///
/// Square because its output holds still between its edges, which is what lets
/// a modulated case be compared sample for sample: both engines advance a
/// modifier once per block, so a continuous shape moves at a different instant
/// in each and at every block size.
///
/// Transport-locked rather than free running for the same reason: a free LFO
/// accumulates phase per block, a transport-locked one is a function of where
/// the block is (ModLfo.cpp).
///
/// A two-second cycle with an eighth of a cycle of offset puts its edges at
/// 0.75, 1.75, 2.75 and 3.75 seconds, a quarter of a beat off every impulse.
///
/// @p tempoSynced picks which way that period is said. Half a hertz and one bar
/// at 120 bpm are the same LFO everywhere but in the fork, where they are
/// separate branches of one timer: hertz runs a ramp off the edit time, a
/// division reads the bar grid. Both are covered, because a corpus exercising
/// one would have found neither the bar-origin bug (#2128) nor the
/// negative-ramp one below it.
ModInfo squareLfo(ModId id, const ControlTarget& target, float amount, bool tempoSynced = false) {
    ModInfo mod;
    mod.id = id;
    mod.name = "LFO " + juce::String(static_cast<int>(id) + 1);
    mod.type = ModType::LFO;
    mod.enabled = true;
    mod.waveform = LFOWaveform::Square;
    mod.rate = 0.5f;
    mod.phaseOffset = 0.125f;
    mod.tempoSync = tempoSynced;
    mod.syncDivision = SyncDivision::Whole;
    mod.triggerMode = LFOTriggerMode::Transport;

    ModLink link;
    link.target = target;
    link.amount = amount;
    link.bipolar = false;
    link.enabled = true;
    mod.links.push_back(link);

    return mod;
}

/// A macro knob at @p value, linked to @p target with @p amount. Written into
/// the array a scope already has rather than appended: a macro is addressed by
/// its index, so its identity is where it sits.
void linkMacro(MacroArray& macros, int index, float value, const ControlTarget& target,
               float amount) {
    auto& macro = macros[static_cast<std::size_t>(index)];
    macro.value = value;

    MacroLink link;
    link.target = target;
    link.amount = amount;
    link.bipolar = false;
    macro.links.push_back(link);
}

// --- material ----------------------------------------------------------------

SourceFact writeSource(const juce::File& directory, const juce::String& name,
                       const MaterialSpec& spec) {
    const auto file = writeMaterial(directory, name, spec);

    auto& pool = SourcePool::getInstance();
    const auto path = file.getFullPathName();

    // Seeded rather than probed. Both legs read a file's rate and duration from
    // the pool, and a case that let each of them probe separately would have
    // two answers to one question the first time a decoder rounded a length.
    pool.seedFactsForTesting(path, spec.durationSeconds, spec.sampleRate);

    SourceFact fact;
    fact.id = pool.acquire(path);
    fact.path = path;
    fact.sampleRate = spec.sampleRate;
    fact.durationSeconds = spec.durationSeconds;

    pool.resolveFacts(fact.id);
    if (auto* source = pool.getMutable(fact.id))
        source->durationSeconds = spec.durationSeconds;

    return fact;
}

MaterialSpec impulses(double intervalSeconds = 0.25) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Impulses;
    spec.sampleRate = kRate;
    spec.durationSeconds = kSourceSeconds;
    spec.intervalSeconds = intervalSeconds;
    return spec;
}

MaterialSpec stepsEvery(double intervalSeconds) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Steps;
    spec.sampleRate = kRate;
    spec.durationSeconds = kSourceSeconds;
    spec.intervalSeconds = intervalSeconds;
    return spec;
}

MaterialSpec steps() {
    return stepsEvery(0.5);
}

MaterialSpec tone(double sampleRate = kRate, double frequency = 220.0) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Tone;
    spec.sampleRate = sampleRate;
    spec.durationSeconds = kSourceSeconds;
    spec.frequency = frequency;
    return spec;
}

/// A tone with a slow swell in it, for the cases judged on their envelope.
MaterialSpec pulsedTone() {
    MaterialSpec spec;
    spec.kind = MaterialKind::PulsedTone;
    spec.sampleRate = kRate;
    spec.durationSeconds = kSourceSeconds;
    spec.frequency = 220.0;
    return spec;
}

MaterialSpec noise() {
    MaterialSpec spec;
    spec.kind = MaterialKind::Noise;
    spec.sampleRate = kRate;
    spec.durationSeconds = kSourceSeconds;
    return spec;
}

// --- clips -------------------------------------------------------------------

ClipInfo audioClipOn(TrackId trackId, ClipId id, double startBeat, double lengthBeats,
                     const SourceFact& source) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = trackId;
    clip.name = "clip " + juce::String(id);
    clip.view = ClipView::Arrangement;
    clip.setAudioContent();

    AudioEvent event;
    event.sourceId = source.id;
    clip.audio().addEvent(std::move(event));

    clip.setPlacementBeats(startBeat, lengthBeats);
    clip.deriveTimesFromBeats(kBpm);
    return clip;
}

ClipInfo audioClip(ClipId id, double startBeat, double lengthBeats, const SourceFact& source) {
    return audioClipOn(kTrack, id, startBeat, lengthBeats, source);
}

AudioEvent& eventOf(ClipInfo& clip) {
    return *clip.primaryEvent();
}

ClipInfo midiClipOn(TrackId trackId, ClipId id, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = trackId;
    clip.name = "midi " + juce::String(id);
    clip.view = ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, lengthBeats);
    clip.deriveTimesFromBeats(kBpm);
    return clip;
}

ClipInfo midiClip(ClipId id, double startBeat, double lengthBeats) {
    return midiClipOn(kTrack, id, startBeat, lengthBeats);
}

MidiNote note(int pitch, double startBeat, double lengthBeats, int velocity = 100) {
    MidiNote value;
    value.noteNumber = pitch;
    value.velocity = velocity;
    value.startBeat = startBeat;
    value.lengthBeats = lengthBeats;
    return value;
}

// --- the case shells ---------------------------------------------------------

Case newTrackCase(const char* name, const char* covers, std::vector<TrackInfo> tracks) {
    Case value;
    value.name = name;
    value.covers = covers;
    value.tracks = std::move(tracks);
    value.master = masterTrack();
    value.sampleRate = kRate;
    value.startBeat = 0.0;
    value.endBeat = 16.0;
    return value;
}

Case newMixCase(const char* name, const char* covers, std::vector<TrackInfo> tracks) {
    auto value = newTrackCase(name, covers, std::move(tracks));

    // Two bars rather than four. What a mixer case asserts is a constant gain,
    // and a constant covers itself in two bars as well as in four; every case
    // is also rendered at four block sizes by the invariance gate (#2078), so
    // its length is paid for five times. The rule the budget follows is
    // #2040's, restated by #2078: when the corpus will not fit, cases get
    // shorter rather than sizes getting dropped.
    //
    // Only the mixer cases. Everything above plays material whose length is part
    // of what it covers: a loop has to tile, a stretched clip has to hold its
    // ratio, and a folding groove needs the bars to fold over.
    value.endBeat = 8.0;
    return value;
}

/// A `param.*` case: one track, one gain device, two bars. Two bars for the
/// reason the mixer cases are -- what these assert repeats every beat, and the
/// invariance gate renders each case four more times.
Case newParamCase(const char* name, const char* covers, TrackInfo track) {
    std::vector<TrackInfo> tracks;
    tracks.push_back(std::move(track));
    auto value = newTrackCase(name, covers, std::move(tracks));
    value.endBeat = 8.0;
    return value;
}

/// A `rack.*` case: one track carrying one rack, two bars.
///
/// Two bars for the reason the mixer and parameter cases are: what a rack case
/// asserts is a constant, and the invariance gate renders every case four more
/// times (#2078).
Case newRackCase(const char* name, const char* covers, TrackInfo track) {
    std::vector<TrackInfo> tracks;
    tracks.push_back(std::move(track));
    auto value = newTrackCase(name, covers, std::move(tracks));
    value.endBeat = 8.0;
    return value;
}

Case newCase(const char* name, const char* covers, TrackInfo track) {
    std::vector<TrackInfo> tracks;
    tracks.push_back(std::move(track));
    return newTrackCase(name, covers, std::move(tracks));
}

/// A stretched case: the fork primes its stretcher with the material at the
/// clip's start rather than before it, so its clip begins about a window late.
///
/// The expected size is left unset here on purpose, and a Spectral case with no
/// declared expectation is refused rather than measured. The figure to put here
/// is what the engine says its own stretcher primes with, which the corpus
/// prints beside the measured offset for every one of these; until somebody has
/// read those two against each other and written the relationship down, the
/// corpus should not be certifying an alignment it has nothing to check.
void expectsPrimingShift(Case& value) {
    value.tier = AudioTier::Spectral;
    value.mechanism =
        "the fork primes its stretcher with the material at the clip's start rather than "
        "before it, so its render begins about a window late";
}

}  // namespace

std::vector<Case> buildCorpus(const juce::File& scratchDirectory) {
    scratchDirectory.createDirectory();

    std::vector<Case> corpus;

    // --- placement, trims, fades: sample for sample --------------------------

    {
        auto value = newCase("placement.grid", "four clips on beat boundaries", plainTrack());
        const auto source = writeSource(scratchDirectory, "placement", impulses());
        value.sources.push_back(source);
        for (auto index = 0; index < 4; ++index)
            value.clips.push_back(audioClip(static_cast<ClipId>(10 + index),
                                            static_cast<double>(index) * 4.0, 2.0, source));
        corpus.push_back(std::move(value));
    }

    {
        auto value =
            newCase("placement.trims", "left and right trim, content offset", plainTrack());
        const auto source = writeSource(scratchDirectory, "trims", impulses());
        value.sources.push_back(source);

        // A left trim is where in the file reading begins, which is a source
        // position rather than a timeline one.
        auto trimmed = audioClip(20, 0.0, 4.0, source);
        eventOf(trimmed).sourceAnchorSamples =
            static_cast<std::int64_t>(std::llround(0.375 * source.sampleRate));
        value.clips.push_back(std::move(trimmed));

        auto shortened = audioClip(21, 8.0, 1.5, source);
        eventOf(shortened).sourceAnchorSamples =
            static_cast<std::int64_t>(std::llround(1.125 * source.sampleRate));
        value.clips.push_back(std::move(shortened));

        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("fades.curves", "all four shapes, in and out", plainTrack());
        const auto source = writeSource(scratchDirectory, "fades", steps());
        value.sources.push_back(source);

        // A constant level with a gain envelope on it renders the envelope
        // itself, so a curve wrong in the fourth decimal is visible.
        const int curves[] = {
            static_cast<int>(FadeCurve::Linear), static_cast<int>(FadeCurve::Convex),
            static_cast<int>(FadeCurve::Concave), static_cast<int>(FadeCurve::SCurve)};
        for (auto index = 0; index < 4; ++index) {
            auto clip = audioClip(static_cast<ClipId>(30 + index), static_cast<double>(index) * 4.0,
                                  3.0, source);
            auto& event = eventOf(clip);
            event.fadeInSeconds = 0.5;
            event.fadeOutSeconds = 0.5;
            event.fadeInType = curves[index];
            event.fadeOutType = curves[index];
            value.clips.push_back(std::move(clip));
        }

        corpus.push_back(std::move(value));
    }

    {
        // The other thing a clip's fade pair can mean: the material accelerates
        // into the clip and decelerates out of it, pitch and all. It resamples
        // rather than stretches, so the material is band limited.
        auto value = newCase("fades.speedramp", "a fade pair read as a speed ramp", plainTrack());
        const auto source = writeSource(scratchDirectory, "speedramp", tone());
        value.sources.push_back(source);

        auto clip = audioClip(40, 0.0, 8.0, source);
        auto& event = eventOf(clip);
        event.fadeInSeconds = 1.0;
        event.fadeOutSeconds = 1.0;
        event.fadeInBehaviour = 1;
        event.fadeOutBehaviour = 1;
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    // --- looping, rate, reverse ---------------------------------------------

    {
        auto value =
            newCase("loop.tiling", "loop start off zero, non-integer loop length", plainTrack());
        const auto source = writeSource(scratchDirectory, "loop", impulses(0.125));
        value.sources.push_back(source);

        auto clip = audioClip(50, 0.0, 12.0, source);
        clip.loopEnabled = true;
        auto& event = eventOf(clip);
        event.setLoopStartSeconds(0.25);
        event.setLoopLengthSeconds(0.75);
        event.setAnchorSeconds(0.25);
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("rate.48k", "a 48 kHz file on a 44.1 kHz render", plainTrack());
        const auto source = writeSource(scratchDirectory, "rate48", tone(48000.0));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(60, 0.0, 8.0, source));

        // Band limited on purpose. The rate converter is a four-point cubic
        // Lagrange here and JUCE's five-point in the fork, and at a few hundred
        // hertz both curves are the same curve to far below the floor, while a
        // wrong position or a dropped sample is as loud as it ever was.
        //
        // The residual here is timing shaped: the two renders sit -0.852 of a
        // sample apart, which is the size a difference between two
        // interpolation kernels would be.
        //
        // That reading has been tried and does not hold. Aligning by the
        // measured offset makes the case worse rather than better (-32.2 dB to
        // -28.5 dB), and a fixed offset is precisely the thing one number can
        // undo. So whatever separates these two is not a constant delay, and
        // the case stays under calibration rather than carrying a mechanism its
        // own evidence contradicts. The next reading to try is a difference in
        // the mapping itself, where the offset varies with position rather than
        // sitting still.
        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("reverse.plain", "a clip that plays backwards", plainTrack());
        const auto source = writeSource(scratchDirectory, "reverse", impulses());
        value.sources.push_back(source);

        auto clip = audioClip(70, 0.0, 8.0, source);
        eventOf(clip).reversed = true;
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    // --- speed, pitch, tempo -------------------------------------------------

    {
        auto value = newCase("speed.ratio", "a speed ratio with no stretcher", plainTrack());
        const auto source = writeSource(scratchDirectory, "speed", tone());
        value.sources.push_back(source);

        auto clip = audioClip(80, 0.0, 8.0, source);
        auto& event = eventOf(clip);
        event.speedRatio = 1.5;
        event.timeStretchMode = time_stretch_mode::kDisabled;
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("pitch.analog", "pitch folded into the ratio", plainTrack());
        const auto source = writeSource(scratchDirectory, "analog", tone());
        value.sources.push_back(source);

        auto clip = audioClip(90, 0.0, 8.0, source);
        auto& event = eventOf(clip);
        event.analogPitch = true;
        event.transpose = 5;
        event.timeStretchMode = time_stretch_mode::kDisabled;
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("tempo.auto", "auto tempo across a step tempo change", plainTrack());
        const auto source = writeSource(scratchDirectory, "autotempo", pulsedTone());
        value.sources.push_back(source);

        auto clip = audioClip(100, 0.0, 12.0, source);
        auto& event = eventOf(clip);
        event.autoTempo = true;
        event.interpBpm = 100.0;
        event.interpTotalBeats = kSourceSeconds * 100.0 / 60.0;
        event.timeStretchMode = time_stretch_mode::kSignalsmith;
        value.clips.push_back(std::move(clip));

        // A step rather than a ramp: the two engines resolve a ramped tempo
        // differently, and the render cases must not be where that shows up.
        value.tempo = {{0.0, kBpm}, {8.0, 140.0}};

        // Auto tempo is a ratio that is not one, so a stretcher runs, so the
        // fork primes it late. Nothing about the tempo change exempts it. What
        // this case adds over the plain stretch ones is the question a moving
        // ratio asks: the shift is measured once, at the start, and the null
        // test then runs across the tempo change. A lateness that grows when
        // the ratio moves fails here rather than being fitted away.
        expectsPrimingShift(value);

        corpus.push_back(std::move(value));
    }

    // --- the stretchers ------------------------------------------------------

    struct StretchCase {
        const char* name;
        const char* covers;
        int mode;
    };

    for (const auto& stretch :
         {StretchCase{"stretch.signalsmith", "kSignalsmith", time_stretch_mode::kSignalsmith},
          StretchCase{"stretch.soundtouch.normal", "kSoundTouchNormal",
                      time_stretch_mode::kSoundTouchNormal},
          StretchCase{"stretch.soundtouch.better", "kSoundTouchBetter",
                      time_stretch_mode::kSoundTouchBetter}}) {
        auto value = newCase(stretch.name, stretch.covers, plainTrack());
        const auto source = writeSource(scratchDirectory, juce::String(stretch.name), pulsedTone());
        value.sources.push_back(source);

        auto clip = audioClip(110, 0.0, 8.0, source);
        auto& event = eventOf(clip);
        event.speedRatio = 1.25;
        event.timeStretchMode = stretch.mode;
        value.clips.push_back(std::move(clip));

        expectsPrimingShift(value);
        corpus.push_back(std::move(value));
    }

    {
        // The one case that measures rather than asserts. What it says is how
        // far apart the two stretchers are on material that has everything in
        // it, which is a number worth watching and not a claim about playback.
        auto value =
            newCase("stretch.broadband", "how far apart the two stretchers are", plainTrack());
        const auto source = writeSource(scratchDirectory, "broadband", noise());
        value.sources.push_back(source);

        auto clip = audioClip(120, 0.0, 8.0, source);
        auto& event = eventOf(clip);
        event.speedRatio = 1.25;
        event.timeStretchMode = time_stretch_mode::kSignalsmith;
        value.clips.push_back(std::move(clip));

        value.tier = AudioTier::Measured;
        value.seed = noise().seed;
        value.mechanism =
            "two phase vocoders fed material with everything in it, framed differently by "
            "each engine's own reading; measured and printed rather than asserted";
        corpus.push_back(std::move(value));
    }

    // --- warp ----------------------------------------------------------------

    {
        // The warp semantics live in the map comparison, which is exact. This
        // is here for the one thing the map cannot say: that the voice reads
        // through it. Warp forces a stretcher on, so the material is band
        // limited and the case carries the priming shift.
        auto value =
            newCase("warp.audio", "a warped clip actually read through the map", plainTrack());
        const auto source = writeSource(scratchDirectory, "warp", pulsedTone());
        value.sources.push_back(source);

        auto clip = audioClip(130, 0.0, 8.0, source);
        auto& event = eventOf(clip);
        event.warpEnabled = true;
        event.warpMarkers = {{0.0, 0.0}, {1.0, 1.5}, {3.0, 3.0}};
        event.timeStretchMode = time_stretch_mode::kSignalsmith;
        value.clips.push_back(std::move(clip));

        expectsPrimingShift(value);
        corpus.push_back(std::move(value));
    }

    // --- takes and comping ---------------------------------------------------

    {
        // Neither engine reads the take list: what plays is the event's source,
        // and comping is a rendered composite that arrives the same way. So
        // what this pins is that neither of them invents anything from it.
        auto value = newCase("takes.comp", "a comped clip and a non-default take", plainTrack());
        const auto first = writeSource(scratchDirectory, "take0", impulses(0.25));
        const auto second = writeSource(scratchDirectory, "take1", impulses(0.375));
        value.sources.push_back(first);
        value.sources.push_back(second);

        auto clip = audioClip(140, 0.0, 8.0, second);
        auto& audio = clip.audio();
        audio.takes.push_back({first.path, first.durationSeconds});
        audio.takes.push_back({second.path, second.durationSeconds});
        audio.currentTakeIndex = 1;
        audio.comp.push_back({0.0, 2.0, 1});
        audio.comp.push_back({2.0, 4.0, 0});
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    // --- the mixer ------------------------------------------------------------
    //
    // The first cases in the corpus with more than one track, and the first that
    // touch the value layer at all. Everything above renders at unity through a
    // default master, so the fader law, the pan law, mute inheritance and
    // solo-through-destination have never been compared against the incumbent.
    //
    // All arithmetic. Nothing interpolates between the two engines on these
    // paths, so the material is impulses and steps and the ordinary floor
    // applies: a fader law that differs in the fourth decimal shows up.
    //
    // **Every track plays something different**, and that is load bearing rather
    // than decorative. Give two tracks the same waveform at the same instant and
    // the case stops being able to tell which track a value was applied to:
    // swapping the fader values between two of them leaves the sum identical and
    // the case passes with the mapping wrong. It also makes the two renders
    // ambiguous to the shift search, which then reports an offset that is really
    // the material repeating. Tracktion has its own reason to object: a node's
    // identity is a hash of what it plays and where, so two tracks playing the
    // same file at the same position produce colliding node IDs and trip the
    // graph's uniqueness assertion.
    //
    // These cases used to be capped at three tracks, because a fourth collided
    // in the fork's node-identity hash and tripped the graph's uniqueness
    // assertion. That was #2085: tracktion::hash_combine did not avalanche, so
    // every node id in an Edit agreed in its top thirty-odd bits. The cap is
    // gone with it, and
    // mix.summing carries a fourth track to keep the case that found it.
    //
    // Sends are absent on purpose. The native leg could render one today, and
    // the incumbent's live on te::AuxSendPlugin instances that PluginManagerSync
    // creates from a PluginManager and the device layer behind it. Writing those
    // plugins straight into the leg would be the second sync this corpus refuses
    // to have. They belong with the rest of the routing graph, in #1892.

    {
        // Four tracks, which is where the node-identity collision in #2085 used
        // to fire. A three-track sum covers the arithmetic just as well; the
        // fourth is here so that the case which found the collision keeps
        // looking for it.
        auto value = newMixCase(
            "mix.summing", "four tracks summed into master",
            {mixTrack(1, "One"), mixTrack(2, "Two"), mixTrack(3, "Three"), mixTrack(4, "Four")});

        // Four different impulse grids rather than four copies of one. Equal
        // values sum exactly whatever order they are added in, so identical
        // sources would agree by arithmetic rather than by the two engines
        // summing the same things.
        const std::array<double, 4> intervals{0.25, 0.3, 0.5, 0.7};
        for (auto index = 0; index < 4; ++index) {
            const auto source = writeSource(scratchDirectory, "sum" + juce::String(index),
                                            impulses(intervals[static_cast<std::size_t>(index)]));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(200 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    {
        // The fader across its range, which is a slider position rather than a
        // gain: unity, six down, and the top of the range, which is six up
        // rather than twice. The law is a curve, so points on it have to be
        // taken rather than derived, which is why this is three cases' worth of
        // track and mix.pan is not.
        auto value =
            newMixCase("mix.volume", "the fader law: unity, attenuation and the top of the range",
                       {mixTrack(1, "Unity"), mixTrack(2, "Down"), mixTrack(3, "Up")});

        const std::array<float, 3> volumes{1.0f, 0.5f, 2.0f};
        for (auto index = 0; index < 3; ++index) {
            value.tracks[static_cast<std::size_t>(index)].volume =
                volumes[static_cast<std::size_t>(index)];

            const auto source = writeSource(scratchDirectory, "vol" + juce::String(index),
                                            stepsEvery(0.5 + 0.125 * index));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(210 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    {
        // The linear pan law, at the ends and in between. MAGDA boosts the near
        // side rather than attenuating the far one, so hard left is (2, 0) and
        // centre is unity: a pad through a constant-power law would sit 3 dB
        // quieter than the same signal on a track, and this is the case that
        // would say so.
        // Three points rather than four: the law is linear in the pan position,
        // so its ends and its centre determine it and a partial pan would be a
        // fourth reading of a line already fixed by three.
        auto value = newMixCase("mix.pan", "the linear pan law across its range",
                                {mixTrack(1, "Left"), mixTrack(2, "Centre"), mixTrack(3, "Right")});

        const std::array<float, 3> pans{-1.0f, 0.0f, 1.0f};
        for (auto index = 0; index < 3; ++index) {
            value.tracks[static_cast<std::size_t>(index)].pan =
                pans[static_cast<std::size_t>(index)];

            const auto source = writeSource(scratchDirectory, "pan" + juce::String(index),
                                            stepsEvery(0.5 + 0.125 * index));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(220 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    {
        // The bottom of the fader, which is its own question rather than a
        // fourth point on the curve above: the position floors at -100 dB, so
        // whether a fader at zero is silence or merely very quiet is something
        // the two engines can disagree about at a level the floor would catch.
        auto value = newMixCase("mix.volume.silent", "a fader at the bottom of its range",
                                {mixTrack(1, "Unity"), mixTrack(2, "Silent")});
        value.tracks[1].volume = 0.0f;

        for (auto index = 0; index < 2; ++index) {
            const auto source = writeSource(scratchDirectory, "silent" + juce::String(index),
                                            stepsEvery(0.5 + 0.125 * index));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(215 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    {
        auto value = newMixCase("mix.mute", "a muted track contributes nothing",
                                {mixTrack(1, "Heard"), mixTrack(2, "Muted")});
        value.tracks[1].muted = true;

        for (auto index = 0; index < 2; ++index) {
            const auto source = writeSource(scratchDirectory, "mute" + juce::String(index),
                                            impulses(index == 0 ? 0.25 : 0.3));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(230 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    {
        auto value = newMixCase("mix.solo", "a soloed track silences its siblings",
                                {mixTrack(1, "Solo"), mixTrack(2, "Other"), mixTrack(3, "Third")});
        value.tracks[0].soloed = true;

        for (auto index = 0; index < 3; ++index) {
            const auto source = writeSource(scratchDirectory, "solo" + juce::String(index),
                                            impulses(0.25 + 0.05 * index));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(240 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    {
        // The master's own fader and pan, on a sum rather than on one track.
        //
        // What this does not claim, and cannot: that the master matrix is
        // applied once. Gain and the linear pan law both distribute over
        // addition, so M(a + b) and M(a) + M(b) are the same samples, and no
        // render of a linear graph can tell the two apart. Where the master
        // stage sits is a question for the plan goldens (#2076), which compare
        // structure; what this case pins is that the two engines agree about the
        // master's law and its values, which is the half a render can answer.
        auto value = newMixCase("mix.master", "the master fader and pan over a sum",
                                {mixTrack(1, "One"), mixTrack(2, "Two")});
        value.master.volume = 0.5f;
        value.master.pan = 0.3f;

        for (auto index = 0; index < 2; ++index) {
            const auto source = writeSource(scratchDirectory, "master" + juce::String(index),
                                            impulses(index == 0 ? 0.25 : 0.4));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(250 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    // --- the fader's own ends -------------------------------------------------

    {
        // The fader past both ends of its range, which mix.volume's three
        // points cannot reach.
        //
        // The fader stores a slider position rather than a gain: a volume
        // becomes decibels, the decibels a position, and the position clamps to
        // [0, 1] before it is a gain again. Above the clamp the fader tops out
        // at +6 dB however large the number; below it the position floors and
        // the track goes silent rather than merely quiet. A model that scaled
        // instead of clipping plays back louder than it was mixed.
        auto value = newMixCase("mix.volume.clamp", "the fader past both ends of its range",
                                {mixTrack(1, "Over"), mixTrack(2, "Top"), mixTrack(3, "Under")});

        // Four times unity is +12 dB, well past the +6 the position allows;
        // 1.9952623 is +6 dB exactly, which is the position at its top; and a
        // millionth is -120 dB, past the -100 the position floors at.
        const std::array<float, 3> volumes{4.0f, 1.9952623f, 0.000001f};
        for (auto index = 0; index < 3; ++index) {
            value.tracks[static_cast<std::size_t>(index)].volume =
                volumes[static_cast<std::size_t>(index)];

            const auto source = writeSource(scratchDirectory, "clamp" + juce::String(index),
                                            stepsEvery(0.5 + 0.125 * index));
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(270 + index), 0.0, 4.0, source));
        }

        corpus.push_back(std::move(value));
    }

    // --- parameters, automation, modifiers and macros --------------------------
    //
    // The first cases that run a device under both engines, and therefore the
    // first that put any of #1891 in front of the fork.
    //
    // Each plays impulses on the beat through a gain whose parameter is what
    // the case is about, so the render is the parameter's own curve sampled
    // eight times. Nothing else is in the path: no fader but unity, no pan, no
    // interpolator, no stretcher.
    //
    // **The steps land on the half beat.** Both engines settle a parameter at
    // the top of a block, so eleven thousand samples of silence either side of
    // every jump is what makes the ordinary floor apply rather than a
    // tolerance. Choose the material so a residual can only be a bug.
    //
    // **Rack scope is missing from the macro cases**, and deliberately. A
    // rack's macros live on a te::RackType that RackSyncManager builds out of a
    // PluginManager, and writing one into the leg is the second sync this
    // corpus refuses to have. It is the boundary sends stop at, and it moves
    // with #1892.
    //
    // **Three of the four modifier engines are not here either**, each after
    // being tried rather than for want of a case.
    //
    // The random walk cannot be nulled by anybody: the fork seeds from the
    // clock, so it does not render the same numbers twice (ModRandom.hpp).
    //
    // The envelope follower is fed by a FollowerSourceTapPlugin PluginManager
    // installs, so without the device layer the fork's follower is handed
    // nothing. Same boundary as the sends.
    //
    // The envelope has no gate a render can open. Its note gate is behind that
    // same boundary, and its transport gate is the fork asking
    // TransportControl::isPlaying(), which is false throughout every offline
    // render: a transport-gated envelope does nothing in a bounce there, while
    // the engine plays it. The case was written and renders 0.625 against
    // silence; pinning it would ask the engine to reproduce a bug. The LFO
    // carries this dimension instead, in both rate modes.

    {
        auto value = newParamCase("param.base", "a device parameter's stored value, heard",
                                  gainTrackOn(kTrack, 910, "Base", 0.5f));

        const auto source = writeSource(scratchDirectory, "parambase", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(280, 0.0, 8.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A curve over a parameter, and the first time the bake has been
        // compared against anything but itself. One lane, two legs: the engine
        // compiles it into segments, the incumbent emits it into a
        // te::AutomationCurve through the app's own bake.
        auto value = newParamCase("param.automation", "a step curve over a device parameter",
                                  gainTrackOn(kTrack, 911, "Automated", 1.0f));

        const auto source = writeSource(scratchDirectory, "paramauto", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(281, 0.0, 8.0, source));

        value.lanes.push_back(
            stepLane(1, gainTarget(kTrack, 911),
                     {{0.0, 1.0}, {1.5, 0.25}, {2.5, 0.75}, {4.5, 0.125}, {6.5, 1.0}}));

        corpus.push_back(std::move(value));
    }

    {
        // A modifier over a parameter. Unipolar at full depth over a base of
        // zero, so the parameter is the modifier's own output and the render is
        // its shape: the two engines agree about the square, or the impulses
        // come out at different heights.
        auto track = gainTrackOn(kTrack, 912, "Modulated", 0.0f);
        track.mods.push_back(squareLfo(0, gainTarget(kTrack, 912), 1.0f));

        auto value = newParamCase("param.modifier", "a square LFO over a device parameter",
                                  std::move(track));

        const auto source = writeSource(scratchDirectory, "parammod", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(282, 0.0, 8.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // Both lanes at once, which is what the precedence rules exist for:
        // automation replaces the base, modulation is added to whichever
        // applied. Every combination lands somewhere different, so a leg that
        // dropped either renders a different height at every impulse.
        //
        // Tempo synced, where param.modifier runs in hertz: same cycle, the
        // fork's other timer branch.
        auto track = gainTrackOn(kTrack, 913, "Both", 1.0f);
        track.mods.push_back(squareLfo(0, gainTarget(kTrack, 913), 0.25f, true));

        auto value = newParamCase("param.both", "automation and a modifier on one parameter",
                                  std::move(track));

        const auto source = writeSource(scratchDirectory, "paramboth", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(283, 0.0, 8.0, source));

        value.lanes.push_back(
            stepLane(1, gainTarget(kTrack, 913), {{0.0, 0.125}, {2.5, 0.75}, {5.5, 0.375}}));

        corpus.push_back(std::move(value));
    }

    {
        // A host write under automation: a lane that covers the block replaces
        // the base rather than adding to it. The stored value is nothing the
        // curve reaches, so a leg that let it through renders an impulse the
        // other does not have. The fork agrees for its own reason, an automated
        // parameter being rewritten from its curve every block, and the two
        // arriving at the same audio from different arrangements is the point.
        auto value = newParamCase("param.hostwrite.automation",
                                  "a stored value under a curve that covers it",
                                  gainTrackOn(kTrack, 914, "Written", 0.9f));

        const auto source = writeSource(scratchDirectory, "paramwriteauto", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(284, 0.0, 8.0, source));

        value.lanes.push_back(
            stepLane(1, gainTarget(kTrack, 914), {{0.0, 0.2}, {1.5, 0.6}, {4.5, 0.2}}));

        corpus.push_back(std::move(value));
    }

    {
        // A host write under a modifier: the stored value is what modulation is
        // added to, not something it replaces. A quarter under half a square,
        // so the parameter is a quarter or three quarters and neither is
        // reachable without both lanes.
        //
        // What it does not pin, checked rather than assumed: that a write
        // arriving mid-playback survives. That bug is a runtime one --
        // setParameter returns early only while the modifier's output is
        // non-zero at that instant -- and an offline render has nobody's hand
        // on a knob during it. Swapping the leg's write for setParameter leaves
        // this case nulling, which is how that was found.
        auto track = gainTrackOn(kTrack, 915, "Written", 0.25f);
        track.mods.push_back(squareLfo(0, gainTarget(kTrack, 915), 0.5f));

        auto value = newParamCase("param.hostwrite.modifier",
                                  "a stored value under an active modifier", std::move(track));

        const auto source = writeSource(scratchDirectory, "paramwritemod", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(285, 0.0, 8.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A macro at track scope: a knob that owns nothing and drives what it
        // is linked to. Base zero and full depth, so the parameter is the
        // macro's position.
        auto track = gainTrackOn(kTrack, 916, "Track Macro", 0.0f);
        linkMacro(track.macros, 0, 0.625f, gainTarget(kTrack, 916), 1.0f);

        auto value = newParamCase("macro.track", "a track macro driving a device parameter",
                                  std::move(track));

        const auto source = writeSource(scratchDirectory, "macrotrack", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(286, 0.0, 8.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // The same knob at device scope: a different macro array and a
        // different node in the walker, so a leg that resolved every macro
        // against the track would render this one identically and prove
        // nothing. Three quarters depth over an eighth of base, so the answer
        // is neither the macro's position nor the base.
        auto track = gainTrackOn(kTrack, 917, "Device Macro", 0.125f);
        auto& device = magda::getDevice(track.chain.fxChainElements.front());
        linkMacro(device.macros, 0, 0.5f, gainTarget(kTrack, 917), 0.75f);

        auto value = newParamCase("macro.device", "a device macro driving its own parameter",
                                  std::move(track));

        const auto source = writeSource(scratchDirectory, "macrodevice", impulses(0.5));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(287, 0.0, 8.0, source));

        corpus.push_back(std::move(value));
    }

    // --- racks -----------------------------------------------------------------
    //
    // The rack graph, through the oracle (#2139, slice 5 of #1892). Slices one
    // to four put aux outputs, delta solo, nesting and channel counts into the
    // compiler and pinned each against the plan goldens; goldens compare
    // structure, and nothing had yet compared the sound.
    //
    // Every one of these is arithmetic over impulses, so the ordinary floor
    // applies and a law that differs in the fourth decimal shows up. What makes
    // them worth rendering rather than dumping is that a rack is where the two
    // engines are built least alike: the incumbent wires a te::RackType, one
    // VolumeAndPan per chain and a connection matrix, and the plan compiles a
    // fader per chain, a mix and a fader over it. Two structures that agree op
    // for op could still disagree about a value, and two that disagree about
    // structure can still render the same samples. Only the render says which.
    //
    // The incumbent leg builds these through the app's own RackSyncManager
    // rather than through a rack builder written for the harness, for the
    // reason the mixer cases go through TrackController: the sync layer is part
    // of what is being validated.
    //
    // **The chains of a rack all read the same input.** That is what a rack is,
    // and it is why these cases cannot use the mixer's trick of giving every
    // track different material: two chains are fed the same samples by
    // construction. So the asymmetry is put in the chains themselves -- a
    // different number of devices, a different value, a different pan -- and
    // every case below is built so that swapping what its chains hold changes
    // what comes out. A case whose chains are interchangeable asserts the sum
    // and nothing about which chain did what.

    {
        // Two chains summing at the rack output, and serial order inside one of
        // them. The second chain holds two devices rather than one so that the
        // two are not interchangeable: 0.5 against 0.5 * 0.25 is 0.625 out of
        // unity, and no swap of values between the chains reproduces it.
        auto value =
            newRackCase("rack.parallel", "parallel chains summed at the rack output",
                        rackTrack(kTrack, "Parallel",
                                  rackOf(700, "Parallel",
                                         {gainChain(1, "One", 940, 0.5f),
                                          gainChain(2, "Two", {{941, 0.5f}, {942, 0.25f}})})));

        const auto source = writeSource(scratchDirectory, "rackparallel", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(300, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // The chain fader's two halves, which are the track fader's laws and
        // not the rack's: the volume is decibels through the fader curve, and
        // the pan is the linear law that boosts the near side rather than
        // attenuating the far one.
        //
        // One chain carries the volume and the other the pan, so the case reads
        // both at once and neither can stand in for the other: a leg applying
        // the pan law to the volume chain would move a channel that should not
        // have moved.
        auto rack = rackOf(701, "Chain Fader",
                           {gainChain(1, "Down", 943, 1.0f), gainChain(2, "Left", 944, 0.5f)});
        rack.chains[0].volume = -6.0f;
        rack.chains[1].pan = -1.0f;

        auto value =
            newRackCase("rack.chain.fader", "the chain fader: decibels, and the linear pan law",
                        rackTrack(kTrack, "Chain Fader", std::move(rack)));

        const auto source = writeSource(scratchDirectory, "rackchainfader", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(301, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // The rack's own fader, which is the other law: it is the rack
        // instance's output levels rather than a fader, so the pan attenuates
        // the far side instead of boosting the near one and the volume is
        // decibels straight rather than through the fader curve. A leg that
        // reused the chain law here would put the case's left channel 6 dB out.
        //
        // One chain, because what this measures is the fader over the mix and a
        // second chain would only change what reaches it.
        auto rack = rackOf(702, "Rack Fader", {gainChain(1, "One", 945, 0.5f)});
        rack.volume = -6.0f;
        rack.pan = 0.5f;

        auto value = newRackCase("rack.fader", "the rack fader: decibels, and the far-side pan law",
                                 rackTrack(kTrack, "Rack Fader", std::move(rack)));

        const auto source = writeSource(scratchDirectory, "rackfader", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(302, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A muted chain contributes nothing, and its sibling still does. The
        // two hold different values so that the case can tell which one
        // survived: a leg that muted the wrong chain would render 0.25 where
        // this expects 0.75.
        auto rack = rackOf(703, "Chain Mute",
                           {gainChain(1, "Heard", 946, 0.75f), gainChain(2, "Muted", 947, 0.25f)});
        rack.chains[1].muted = true;

        auto value = newRackCase("rack.chain.mute", "a muted chain contributes nothing",
                                 rackTrack(kTrack, "Chain Mute", std::move(rack)));

        const auto source = writeSource(scratchDirectory, "rackchainmute", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(303, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // Mute against a sibling's solo, which is the pair rather than either
        // one: a soloed chain takes its siblings out of the mix, and a chain
        // that is muted as well as unsoloed is out for two reasons at once. The
        // third chain is what makes the answer a value rather than a flag --
        // solo has to silence a plain sibling as well as a muted one, and a
        // case with only the muted one could not tell the two rules apart.
        auto rack = rackOf(704, "Chain Solo",
                           {gainChain(1, "Solo", 948, 0.75f), gainChain(2, "Muted", 949, 0.5f),
                            gainChain(3, "Other", 950, 0.25f)});
        rack.chains[0].solo = true;
        rack.chains[1].muted = true;

        auto value =
            newRackCase("rack.chain.solo", "a soloed chain against a muted and a plain one",
                        rackTrack(kTrack, "Chain Solo", std::move(rack)));

        const auto source = writeSource(scratchDirectory, "rackchainsolo", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(304, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // Delta solo at device scope: what the device added, which for a gain
        // of 0.25 is the input at -0.75 of itself. Inverted rather than merely
        // quieter, so a leg that took the difference the other way round is a
        // sign flip rather than a level and the case says so at full scale.
        //
        // The sibling chain is at unity and carries no delta, so the rack's
        // output is the sum of a difference and a passthrough. That is the
        // arrangement the button is used in: delta on one device of one chain,
        // with the rest of the rack still playing.
        auto rack = rackOf(705, "Device Delta",
                           {gainChain(1, "Delta", 951, 0.25f), gainChain(2, "Plain", 952, 1.0f)});
        magda::getDevice(rack.chains[0].elements.front()).deltaSolo = true;

        auto value =
            newRackCase("rack.deltasolo.device", "delta solo on a device inside a rack chain",
                        rackTrack(kTrack, "Device Delta", std::move(rack)));

        const auto source = writeSource(scratchDirectory, "rackdeltadevice", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(305, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // Delta solo at rack scope, which subtracts the rack's own input from
        // its output rather than one device's. Two chains, so that what is
        // subtracted is the sum of both and not either alone: their total is
        // 0.75, so the rack renders the input at -0.25 of itself, and a leg
        // that took the difference against one chain would render silence or
        // twice as much.
        auto rack = rackOf(706, "Rack Delta",
                           {gainChain(1, "One", 953, 0.5f), gainChain(2, "Two", 954, 0.25f)});
        rack.deltaSolo = true;

        auto value = newRackCase("rack.deltasolo.rack", "delta solo at rack scope",
                                 rackTrack(kTrack, "Rack Delta", std::move(rack)));

        const auto source = writeSource(scratchDirectory, "rackdeltarack", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(306, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A rack inside a chain of another rack, which is the case the op-key
        // identity slice was written for (#2137): the inner rack's chains and
        // devices are addressed through the path they sit at, and two racks
        // holding a device with the same number are two different devices.
        //
        // The inner rack sits behind a device in the outer chain rather than
        // alone in it, so the case also asserts the order of the two: 0.5 into
        // an inner rack summing 0.5 and 0.25 is 0.375, and a leg that ran the
        // rack first would render the same number, which is why the outer
        // chain's sibling is here. It carries a value neither product can be
        // confused with.
        auto inner =
            rackOf(708, "Inner", {gainChain(1, "One", 957, 0.5f), gainChain(2, "Two", 958, 0.25f)});

        ChainInfo outerChain;
        outerChain.id = 1;
        outerChain.name = "Nesting";
        outerChain.elements.emplace_back(gainDevice(955, 0.5f));
        outerChain.elements.push_back(makeRackElement(std::move(inner)));

        std::vector<ChainInfo> outerChains;
        outerChains.push_back(std::move(outerChain));
        outerChains.push_back(gainChain(2, "Sibling", 956, 0.125f));

        auto value =
            newRackCase("rack.nested", "a rack inside a chain of another rack",
                        rackTrack(kTrack, "Nested", rackOf(707, "Outer", std::move(outerChains))));

        const auto source = writeSource(scratchDirectory, "racknested", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(307, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A chain routed to an aux output of the rack, which reaches nothing in
        // either engine: RackSyncManager wires it to output pins three and up
        // and the rack instance on the track reads pins one and two, so there
        // is no other end to it. The plan compiles nothing for such a chain and
        // says so in a diagnostic.
        //
        // Written as a case because "both engines drop it" is a claim about the
        // engines and not about the model, and the day one of them starts
        // carrying an aux output is the day this has to be looked at rather
        // than the day a project quietly changes. The audible chain is what
        // makes the assertion a comparison instead of two silences agreeing.
        auto rack =
            rackOf(709, "Aux", {gainChain(1, "Main", 959, 0.5f), gainChain(2, "Aux", 960, 1.0f)});
        rack.chains[1].outputIndex = 1;

        auto value = newRackCase("rack.aux", "a chain routed to an aux output reaches nothing",
                                 rackTrack(kTrack, "Aux", std::move(rack)));

        // Named rather than suppressed: the plan is right to say so, and a case
        // that stopped getting this diagnostic would be measuring a compiler
        // that had started carrying the aux output without anybody deciding to.
        value.expectedDiagnostics.push_back("rack 709 chain 2: aux output 1 reaches nothing");

        const auto source = writeSource(scratchDirectory, "rackaux", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(308, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A mono device between stereo ones. What it measures is the bus rather
        // than the device: the chain arrives stereo, narrows to one channel at
        // the middle device, and widens again behind it, and both engines have
        // to narrow and widen at the same points and in the same direction.
        //
        // The direction is the half a symmetric signal cannot ask about, which
        // is why this is the one case in the corpus that plays noise on purpose:
        // every other generator writes the same samples to both channels, and a
        // fold of two identical channels is the identity whichever way it is
        // done. With the two sides different, a device that read the right
        // channel instead of the left, or summed the two instead of taking one,
        // renders something this case can see.
        //
        // Behind it the chain is stereo again, and it has to be the same on both
        // sides: a mono port's channel is copied to both, so the stereo device
        // after it reads the narrow device's output twice rather than the
        // original right channel once.
        auto value = newRackCase(
            "rack.mono", "a mono device between stereo ones",
            rackTrack(
                kTrack, "Mono",
                rackOf(710, "Mono",
                       {gainChain(1, "Narrowing", {{961, 0.5f}, {962, 0.5f}, {963, 0.5f}})})));

        magda::getRack(value.tracks.front().chain.fxChainElements.front())
            .chains.front()
            .elements[1] = monoGainDevice(962, 0.5f);

        auto material = noise();
        material.seed = 0x9E3779B9u;
        value.seed = material.seed;

        const auto source = writeSource(scratchDirectory, "rackmono", material);
        value.sources.push_back(source);
        value.clips.push_back(audioClip(309, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // An instrument behind an audio source in the same chain. What it
        // measures is one rule: an instrument's output is added to the bus it
        // was handed rather than replacing it, so the audio already travelling
        // the chain survives it.
        //
        // The instrument makes no sound in either leg -- it is the stand-in the
        // MIDI cases use, and the incumbent instantiates nothing for it -- so
        // what comes out is the gain in front of it and nothing else. That is
        // exactly what makes the case sharp: a leg that let the instrument
        // replace the bus would render silence, and half of full scale against
        // silence is not a difference anybody has to measure carefully.
        //
        // The instrument is second so that the audio reaches it. First would
        // ask a different and weaker question, because there would be nothing
        // travelling the chain yet for it to be added to.
        ChainInfo chain;
        chain.id = 1;
        chain.name = "Source then instrument";
        chain.elements.emplace_back(gainDevice(964, 0.5f));
        chain.elements.emplace_back(chainInstrument(965));

        std::vector<ChainInfo> chains;
        chains.push_back(std::move(chain));

        auto value = newRackCase(
            "rack.instrument", "an instrument behind an audio source in one chain",
            rackTrack(kTrack, "Instrument", rackOf(711, "Instrument", std::move(chains))));

        const auto source = writeSource(scratchDirectory, "rackinstrument", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClip(310, 0.0, 4.0, source));

        corpus.push_back(std::move(value));
    }

    {
        // A multi-out instrument and the track that reads its second pair.
        //
        // The other half of the aux question, and a different mechanism from
        // rack.aux above: a chain routed to a rack's aux output reaches
        // nothing, while an output pair of an instrument reaches the MultiOut
        // track that opened it. The compiler says as much where it refuses the
        // first; this is where the second is rendered rather than asserted in
        // a comment.
        //
        // MIDI drives it, so nothing here rests on two engines agreeing about
        // a free-running clock: on every note-on the device writes an impulse
        // to pair 0 and half of one to pair 1 (NullDiffGain.hpp), and where a
        // note lands is what the `midi.*` cases already pin.
        //
        // The two tracks are panned apart, and that is what makes the case able
        // to say which pair reached which track rather than only that the total
        // was right. Both feed the master, so a leg that sent both pairs down
        // one track, or swapped them, would sum to the same figure on a case
        // that let them overlap; hard left against hard right puts the source
        // pair at full scale in one channel and the second pair at half scale
        // in the other, and any mixing up of the two moves both.
        auto source = instrumentTrackOn(1, 970, "Source");
        source.chain.fxChainElements.clear();
        source.chain.fxChainElements.emplace_back(multiOutSynthDevice(970, 2));
        source.pan = -1.0f;

        TrackInfo pairTrack;
        pairTrack.id = 2;
        pairTrack.type = TrackType::MultiOut;
        pairTrack.name = "Pair";
        pairTrack.audioOutputDevice = "master";
        pairTrack.pan = 1.0f;
        pairTrack.multiOutLink = MultiOutTrackLink{1, 970, 1};

        auto value = newTrackCase("multiout.pair",
                                  "an instrument's second output pair and the track that reads it",
                                  {std::move(source), std::move(pairTrack)});
        value.endBeat = 8.0;

        auto clip = midiClipOn(1, 320, 0.0, 8.0);
        // Full velocity, so the level is exactly one and the pair's half is
        // exactly a half: a corpus that had to compare 0.7874 against 0.7874
        // would be measuring the same arithmetic with more places to go wrong.
        for (auto index = 0; index < 8; ++index)
            clip.midiNotes.push_back(note(60 + index, static_cast<double>(index), 0.5, 127));
        value.clips.push_back(std::move(clip));

        corpus.push_back(std::move(value));
    }

    // --- a project that asserts both -------------------------------------------

    {
        // The case this slice exists for, and until it was written nothing ran
        // the redesign end to end. Every MIDI case above sets the audio tier to
        // None and every audio case leaves the MIDI flag off, so the two
        // assertions had never both been made about one project: the capture
        // placed on a track that is not the first, the aggregation of more than
        // one capture, and a verdict that is the audio AND the MIDI could all
        // have regressed with every case still green.
        //
        // An audio track first, then two instrument tracks. The order is the
        // point of the first: a leg that put its capture on tracks.front() would
        // hand back an empty stream against a full one. The second instrument is
        // the point of the pair: one capture read where two exist compares half
        // a project against all of it.
        //
        // The two instruments play an octave apart at staggered instants, which
        // is what makes a dropped stream visible. It is not what makes a
        // misattributed one visible: a MidiEvent carries nothing but its bytes
        // and its position, so two synths that received each other's notes
        // produce the same flat stream as two that received their own. The
        // streams are therefore compared per track, which is where that identity
        // still exists, and the pitches are only there to make the failure
        // readable when it happens.
        // Synth A carries two MIDI-consuming devices, which is the only way to
        // tell the two legs' comparison points apart: the incumbent records at
        // the head of the plugin list, once, and a native leg reading every
        // Device op would report each of that track's notes twice.
        auto synthA = instrumentTrackOn(2, 901, "Synth A");
        addSecondInstrument(synthA, 903);

        // Synth B opens with an audio effect, so the device that reads the
        // chain's MIDI is not the first one on its track.
        auto synthB = instrumentTrackOn(3, 902, "Synth B");
        addEffect(synthB, 904);
        std::rotate(synthB.chain.fxChainElements.begin(), synthB.chain.fxChainElements.end() - 1,
                    synthB.chain.fxChainElements.end());

        // The audio track carries a device too, and deliberately one that
        // consumes no MIDI. It is what makes this project able to tell a tap
        // chosen per MIDI-consuming track from a tap chosen per track that
        // happens to have a device: the incumbent captures nothing here, so
        // anything captured on this side is a track only one leg saw.
        auto audio = mixTrack(1, "Audio");
        addEffect(audio, 905);

        auto value = newMixCase("project.mixed", "an audio track beside two instrument tracks",
                                {std::move(audio), std::move(synthA), std::move(synthB)});

        const auto source = writeSource(scratchDirectory, "mixed", impulses(0.25));
        value.sources.push_back(source);
        value.clips.push_back(audioClipOn(1, 260, 0.0, 4.0, source));

        auto first = midiClipOn(2, 261, 0.0, 8.0);
        for (auto index = 0; index < 4; ++index)
            first.midiNotes.push_back(note(60 + index, static_cast<double>(index) * 2.0, 0.75));
        value.clips.push_back(std::move(first));

        auto second = midiClipOn(3, 262, 0.0, 8.0);
        for (auto index = 0; index < 4; ++index)
            second.midiNotes.push_back(
                note(72 + index, static_cast<double>(index) * 2.0 + 1.0, 0.5, 90));
        value.clips.push_back(std::move(second));

        // Both, which is the whole point. The audio tier judges the impulse
        // track, which the two capture devices contribute silence to, and the
        // MIDI comparison judges what the two of them received.
        value.tier = AudioTier::Exact;
        value.compareMidiStreams = true;
        corpus.push_back(std::move(value));
    }

    // --- MIDI ----------------------------------------------------------------

    {
        auto value = newCase("midi.notes", "notes and velocities", instrumentTrack());
        auto clip = midiClip(200, 0.0, 8.0);
        for (auto index = 0; index < 8; ++index)
            clip.midiNotes.push_back(note(60 + index, static_cast<double>(index),
                                          index % 2 == 0 ? 0.75 : 0.5, 40 + index * 10));
        value.clips.push_back(std::move(clip));
        value.tier = AudioTier::None;
        value.compareMidiStreams = true;
        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("midi.cc", "dense CC and pitch bend", instrumentTrack());
        auto clip = midiClip(210, 0.0, 8.0);
        clip.midiNotes.push_back(note(60, 0.0, 8.0));

        // A slow sweep and a fast one, because the two fail differently: the
        // fork's grid is far too dense for the first and far too sparse for the
        // second.
        for (auto index = 0; index <= 8; ++index) {
            MidiCCData point;
            point.controller = 1;
            point.value = index * 15 > 127 ? 127 : index * 15;
            point.beatPosition = static_cast<double>(index);
            clip.midiCCData.push_back(point);
        }

        for (auto index = 0; index <= 4; ++index) {
            MidiPitchBendData point;
            point.value = 8192 - index * 2048;
            point.beatPosition = 4.0 + static_cast<double>(index) * 0.05;
            clip.midiPitchBendData.push_back(point);
        }

        value.clips.push_back(std::move(clip));
        value.tier = AudioTier::None;
        value.compareMidiStreams = true;
        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("midi.mpe", "per-note pitch expression", instrumentTrack());
        auto clip = midiClip(220, 0.0, 8.0);

        for (auto index = 0; index < 3; ++index) {
            auto value1 = note(60 + index * 4, static_cast<double>(index) * 0.5, 4.0);
            value1.pitchExpression = {{0.0, 0.0}, {2.0, 1.0 + index}, {4.0, 0.0}};
            clip.midiNotes.push_back(std::move(value1));
        }

        value.clips.push_back(std::move(clip));
        value.tier = AudioTier::None;
        value.compareMidiStreams = true;
        corpus.push_back(std::move(value));
    }

    {
        // The single trickiest thing ported in slice 6. An odd loop length under
        // a per-beat groove means each pass starts somewhere else in the
        // pattern, so the groove is anchored to the project grid and looked up
        // per pass rather than baked at compile time. Until this case, that was
        // pinned only by the engine's own tests, which is to say by one reading
        // of LoopedMidiEventGenerator.
        auto value =
            newCase("midi.fold", "an odd loop length under a per-beat groove", instrumentTrack());

        auto clip = midiClip(230, 0.0, 12.0);
        clip.loopEnabled = true;
        clip.loopStartBeats = 0.0;
        clip.loopLengthBeats = 1.5;
        clip.grooveTemplate = kGrooveName;
        clip.grooveStrength = 1.0f;
        clip.midiNotes.push_back(note(60, 0.0, 0.4));
        clip.midiNotes.push_back(note(64, 0.5, 0.4));
        clip.midiNotes.push_back(note(67, 1.0, 0.4));
        value.clips.push_back(std::move(clip));

        value.grooveXml =
            "<GROOVETEMPLATES>"
            "<GROOVETEMPLATE name=\"null-diff swing\" numberOfNotes=\"4\" notesPerBeat=\"2\" "
            "parameterized=\"0\">"
            "<SHIFT delta=\"0\"/><SHIFT delta=\"0.25\"/>"
            "<SHIFT delta=\"0\"/><SHIFT delta=\"0.25\"/>"
            "</GROOVETEMPLATE>"
            "</GROOVETEMPLATES>";

        value.tier = AudioTier::None;
        value.compareMidiStreams = true;
        corpus.push_back(std::move(value));
    }

    {
        auto value = newCase("midi.offset", "midiOffset on an unlooped clip", instrumentTrack());

        auto clip = midiClip(240, 0.0, 8.0);
        clip.midiOffset = 0.5;

        // From beat 1 rather than beat 0, so the offset moves every note and
        // clips none of them. A note at the very start would be pulled before
        // the clip's own edge, where the engine trims it and the fork does not,
        // and that one note would be measuring the trim rather than the offset.
        for (auto index = 0; index < 4; ++index)
            clip.midiNotes.push_back(
                note(60 + index * 3, 1.0 + static_cast<double>(index) * 2.0, 1.0));
        value.clips.push_back(std::move(clip));

        value.tier = AudioTier::None;
        value.compareMidiStreams = true;

        // The fork's arranger path drops midiOffset while its session path
        // applies it, which is a gap in the sync layer rather than a semantic.
        // Applying it either way is the reading the engine takes: the engine
        // pulls the content earlier by the offset and the fork leaves it where
        // it was written, so every one of the fork's notes lands later by
        // exactly that. Declared here and asserted, so a note that moves for any
        // other reason still breaks.
        value.declaredMidiShiftBeats = clip.midiOffset;
        value.mechanism =
            "the fork's arranger path drops midiOffset on an unlooped clip while its session "
            "path applies it; the engine applies it either way";

        corpus.push_back(std::move(value));
    }

    return corpus;
}

std::vector<std::string> externalDevicesIn(const Case& value) {
    std::vector<std::string> external;

    // Recursive, because a rack is a chain of chains and a plugin four levels
    // down frames its own work exactly like one at the top. A walk that stopped
    // at the first level would report a rack full of plugins as an internal
    // project and hold it to bit identity, which is the one way this can be
    // wrong in the direction nobody notices: the gate would fail, and the
    // failure would name no cause.
    const std::function<void(const std::vector<ChainElement>&)> walk =
        [&](const std::vector<ChainElement>& elements) {
            for (const auto& element : elements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    if (device.format != PluginFormat::Internal)
                        external.push_back(device.name.toStdString());
                    continue;
                }

                for (const auto& chain : getRack(element).chains)
                    walk(chain.elements);
            }
        };

    // The post-FX stage and the mixer rail's analysis devices are as much of the
    // project as the insert chain is, and a plugin in either one reaches the
    // render the same way.
    const auto walkTrack = [&](const TrackInfo& track) {
        walk(track.chain.fxChainElements);

        for (const auto* stage :
             {&track.chain.postFxChainElements, &track.chain.mixerAnalysisElements})
            for (const auto& element : *stage)
                if (element.device.format != PluginFormat::Internal)
                    external.push_back(element.device.name.toStdString());
    };

    for (const auto& track : value.tracks)
        walkTrack(track);

    walkTrack(value.master);

    return external;
}

const std::vector<Case>& sharedCorpus(const juce::File& scratchDirectory) {
    static std::map<juce::String, std::vector<Case>> built;

    const auto key = scratchDirectory.getFullPathName();
    if (const auto found = built.find(key); found != built.end())
        return found->second;

    return built.emplace(key, buildCorpus(scratchDirectory)).first->second;
}

}  // namespace magda::nulldiff
