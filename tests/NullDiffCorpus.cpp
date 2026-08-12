#include <array>
#include <cmath>
#include <map>

#include "NullDiffCase.hpp"
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
TrackInfo instrumentTrack() {
    auto track = plainTrack();

    DeviceInfo device;
    device.id = kInstrument;
    device.name = "Capture";
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    track.chain.fxChainElements.emplace_back(std::move(device));

    return track;
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

MaterialSpec steps() {
    MaterialSpec spec;
    spec.kind = MaterialKind::Steps;
    spec.sampleRate = kRate;
    spec.durationSeconds = kSourceSeconds;
    spec.intervalSeconds = 0.5;
    return spec;
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

ClipInfo midiClip(ClipId id, double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = kTrack;
    clip.name = "midi " + juce::String(id);
    clip.view = ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, lengthBeats);
    clip.deriveTimesFromBeats(kBpm);
    return clip;
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
    // is also rendered twice by the block-size test, so its length is paid for
    // three times. The rule the budget follows is #2040's: when the corpus will
    // not fit, cases get shorter rather than fewer.
    //
    // Only the mixer cases. Everything above plays material whose length is part
    // of what it covers: a loop has to tile, a stretched clip has to hold its
    // ratio, and a folding groove needs the bars to fold over.
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
    // Sends are absent on purpose. The native leg could render one today, and
    // the incumbent's live on te::AuxSendPlugin instances that PluginManagerSync
    // creates from a PluginManager and the device layer behind it. Writing those
    // plugins straight into the leg would be the second sync this corpus refuses
    // to have. They belong with the rest of the routing graph, in #1892.

    {
        auto value = newMixCase("mix.summing", "three tracks summed into master",
                                {mixTrack(1, "One"), mixTrack(2, "Two"), mixTrack(3, "Three")});

        // Three different impulse grids rather than three copies of one. Equal
        // values sum exactly whatever order they are added in, so identical
        // sources would agree by arithmetic rather than by the two engines
        // summing the same things.
        const std::array<double, 3> intervals{0.25, 0.3, 0.5};
        for (auto index = 0; index < 3; ++index) {
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
        // rather than twice. Silence is in here too, and it is the interesting
        // one: the position floors at -100 dB, so whether that is zero or merely
        // very quiet is a thing the two engines can disagree about at a level
        // the floor would catch.
        auto value = newMixCase(
            "mix.volume", "the fader law: unity, attenuation, the top of the range, and silence",
            {mixTrack(1, "Unity"), mixTrack(2, "Down"), mixTrack(3, "Up"), mixTrack(4, "Silent")});

        const std::array<float, 4> volumes{1.0f, 0.5f, 2.0f, 0.0f};
        for (auto index = 0; index < 4; ++index) {
            value.tracks[static_cast<std::size_t>(index)].volume =
                volumes[static_cast<std::size_t>(index)];

            const auto source = writeSource(scratchDirectory, "vol" + juce::String(index), steps());
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
        auto value = newMixCase("mix.pan", "the linear pan law across its range",
                                {mixTrack(1, "Left"), mixTrack(2, "Centre"), mixTrack(3, "Right"),
                                 mixTrack(4, "Part")});

        const std::array<float, 4> pans{-1.0f, 0.0f, 1.0f, 0.4f};
        for (auto index = 0; index < 4; ++index) {
            value.tracks[static_cast<std::size_t>(index)].pan =
                pans[static_cast<std::size_t>(index)];

            const auto source = writeSource(scratchDirectory, "pan" + juce::String(index), steps());
            value.sources.push_back(source);
            value.clips.push_back(audioClipOn(static_cast<TrackId>(index + 1),
                                              static_cast<ClipId>(220 + index), 0.0, 4.0, source));
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
        // The master's own fader and pan, applied once to the sum. Two tracks
        // rather than one, so that a master gain applied per track instead of
        // once to the mix would be a different number rather than the same one.
        auto value = newMixCase("mix.master", "master volume and pan, applied once",
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

const std::vector<Case>& sharedCorpus(const juce::File& scratchDirectory) {
    static std::map<juce::String, std::vector<Case>> built;

    const auto key = scratchDirectory.getFullPathName();
    if (const auto found = built.find(key); found != built.end())
        return found->second;

    return built.emplace(key, buildCorpus(scratchDirectory)).first->second;
}

}  // namespace magda::nulldiff
