#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "clip/ClipAudioSource.hpp"
#include "clip/ClipStretcher.hpp"
#include "clip/EventPlacement.hpp"
#include "core/TimeStretchModes.hpp"
#include "io/SourceReaders.hpp"

/**
 * Playing a clip at a speed and a pitch that are not its file's (#2037).
 *
 * Two halves, and they are tested differently on purpose.
 *
 * Where in the reading a moment sits is arithmetic, so it is tested as
 * arithmetic: exactly, against numbers worked out by hand. Everything this slice
 * adds is that one function answering differently, so this is where a speed
 * ratio, auto tempo, analog pitch and a speed ramp are actually pinned down.
 *
 * What comes out of a stretcher is not arithmetic. A phase vocoder's samples are
 * its own business, so what is asserted of those is what a listener would say:
 * the block is full, the level survived, and the pitch moved or did not move.
 * The resampling path is the exception, and a useful one: cubic interpolation of
 * a straight line is exact, so a counting reader through it says precisely which
 * sample of the file was heard.
 *
 * Everything rolls, the way the other clip rigs do. A test that skipped from one
 * block to a distant one would be testing a locate rather than playback (#2016).
 */

using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::BlockInfo;
using magda::engine::ClipAudioSource;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSnapshotFeed;
using magda::engine::ClipStreamFeed;
using magda::engine::ClipStreamTable;
using magda::engine::ClipStretcher;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
using magda::engine::SnapshotSpan;
using magda::engine::StretchSetup;
using magda::engine::TrackClipPlayback;

namespace mode = magda::time_stretch_mode;

namespace {

constexpr double kSampleRate = 44100.0;

/// Long enough that a block holds a dozen cycles of the test tone, because one
/// of the questions here is what happened to the pitch.
constexpr int kBlockSize = 512;

constexpr magda::TrackId kTrack = 7;

/// The rig's tempo. Beats and seconds are two faces of one instant everywhere in
/// the engine, so a span built here carries both, and auto tempo reads the one
/// the others ignore.
constexpr double kBpm = 120.0;
constexpr double kBeatsPerSecond = kBpm / 60.0;

double blockTime(int index) {
    return index * static_cast<double>(kBlockSize) / kSampleRate;
}

RenderContext context() {
    return RenderContext{kSampleRate, kBlockSize, 2};
}

Catch::Approx approx(double value, double margin = 1e-4) {
    return Catch::Approx(value).margin(margin);
}

/// Sample n reads back as n, so which sample of the file was heard is readable
/// off the value. Through the resampling path that stays true between samples:
/// the curve is cubic and a straight line is one of the things a cubic is.
class CountingReader final : public magda::engine::AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 100000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample));
        return numSamples;
    }
};

/// A tone at a known frequency, which is what a question about pitch needs.
class SineReader final : public magda::engine::AudioFileReader {
  public:
    explicit SineReader(double frequency) : frequency_(frequency) {}

    std::int64_t lengthInSamples() const override {
        return 100000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample) {
                const auto phase = 2.0 * juce::MathConstants<double>::pi * frequency_ *
                                   static_cast<double>(startSample + sample) / kSampleRate;
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(std::sin(phase)));
            }
        return numSamples;
    }

  private:
    double frequency_;
};

/// Every sample the same, so a gain that was applied is visible and a gain that
/// was not is visible too.
class ConstantReader final : public magda::engine::AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 100000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample, 1.0f);
        return numSamples;
    }
};

/// A stretcher that records what it was asked to do and passes the material
/// through. What a voice calls, and when, is the question some of these tests
/// ask; what a phase vocoder does with it is not.
class CountingStretcher final : public magda::engine::ClipStretcher {
  public:
    int preRollSamples(double) const override {
        return 0;
    }

    void reset() override {
        ++resets;
    }

    void prime(PrefetchStream&, std::int64_t, int, double) override {
        ++primes;
    }

    void process(juce::dsp::AudioBlock<const float> input, double, double,
                 juce::dsp::AudioBlock<float> output) override {
        ++processes;

        for (std::size_t channel = 0; channel < output.getNumChannels(); ++channel)
            for (std::size_t sample = 0; sample < output.getNumSamples(); ++sample)
                output.getChannelPointer(channel)[sample] =
                    sample < input.getNumSamples() ? input.getChannelPointer(channel)[sample]
                                                   : 0.0f;
    }

    int primes = 0;
    int resets = 0;
    int processes = 0;
};

SnapshotSpan seconds(double start, double end) {
    SnapshotSpan span;
    span.startBeat = start * kBeatsPerSecond;
    span.endBeat = end * kBeatsPerSecond;
    span.startSeconds = start;
    span.endSeconds = end;
    return span;
}

SnapshotSpan blocks(int firstBlock, int lastBlock) {
    return seconds(blockTime(firstBlock), blockTime(lastBlock));
}

BlockInfo blockFrom(double startSeconds, bool continuous = true) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;
    block.seconds.start = startSeconds;
    block.seconds.end = startSeconds + kBlockSize / kSampleRate;
    block.beats.start = startSeconds * kBeatsPerSecond;
    block.beats.end = block.seconds.end * kBeatsPerSecond;
    block.continuous = continuous;
    return block;
}

AudioClipPlayback clipOver(magda::ClipId id, SnapshotSpan span, std::int64_t anchor = 0) {
    AudioClipPlayback clip;
    clip.clipId = id;
    clip.span = span;
    clip.launchFadeSamples = 0;

    AudioEventPlayback event;
    event.eventId = id;
    event.sourceId = id;
    event.filePath = "take.wav";
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 10000.0;
    event.span = span;
    event.anchorSamples = anchor;
    clip.events.push_back(std::move(event));

    return clip;
}

/// The beat face of a moment, at the rig's tempo. What the transport publishes
/// beside the seconds, and what auto tempo reads.
double beatAt(double seconds) {
    return seconds * kBeatsPerSecond;
}

/// A track's clips, the readers and stretchers behind them, and a transport that
/// rolls. Provisioned the way ClipVoicePool provisions: through the same
/// functions, so that where a stream is pointed and where a voice reads are one
/// derivation rather than two that have to be kept in step by hand.
struct Rig {
    Rig() {
        source.prepare(context());
        output.setSize(2, kBlockSize);
        output.clear();
    }

    PrefetchStream& give(magda::ClipId clipId, magda::EventId eventId,
                         std::unique_ptr<magda::engine::AudioFileReader> reader,
                         magda::engine::PrefetchSettings settings = {8192, 8}) {
        const auto* clip = clipOf(clipId);
        REQUIRE(clip != nullptr);
        const auto* event = eventOf(clipId, eventId);
        REQUIRE(event != nullptr);

        auto stream = std::make_shared<PrefetchStream>(
            magda::engine::readThrough(std::move(reader),
                                       magda::engine::sourceReadFor(*event, kSampleRate)),
            context(), settings);

        const auto setup = magda::engine::stretchSetupFor(*clip, *event, context());
        std::shared_ptr<ClipStretcher> stretcher = magda::engine::makeStretcher(setup);
        const auto preRoll =
            stretcher != nullptr ? stretcher->preRollSamples(setup.nominalRate) : 0;

        auto& created = *stream;
        table.entries.push_back(ClipStreamTable::Entry{kTrack, clipId, eventId, std::move(stream),
                                                       std::move(stretcher), preRoll});
        return created;
    }

    void publish() {
        auto compiled = std::make_shared<ClipSnapshot>();
        compiled->tracks.push_back(lane);
        clips.publish(std::move(compiled));

        std::sort(table.entries.begin(), table.entries.end(),
                  [](const ClipStreamTable::Entry& a, const ClipStreamTable::Entry& b) {
                      if (a.trackId != b.trackId)
                          return a.trackId < b.trackId;
                      if (a.clipId != b.clipId)
                          return a.clipId < b.clipId;
                      return a.eventId < b.eventId;
                  });
        streams.publish(std::make_shared<const ClipStreamTable>(table));
    }

    /// Put the transport @p lead blocks before @p targetBlock and roll to it,
    /// cueing every stream where the pool would have (ClipVoicePool::cueFor).
    void start(int targetBlock, int lead) {
        next_ = targetBlock - lead;

        for (const auto& entry : table.entries) {
            const auto* clip = clipOf(entry.clipId);
            const auto* event = eventOf(entry.clipId, entry.eventId);
            REQUIRE(event != nullptr);

            const auto at = std::max(blockTime(next_), event->span.startSeconds);
            const auto position =
                magda::engine::readingPositionAt(*clip, *event, at, beatAt(at), kSampleRate);
            const auto ahead = entry.stretcher != nullptr ? entry.stretcher->readAheadSamples() : 0;

            entry.stream->seek(static_cast<std::int64_t>(std::llround(position)) + ahead -
                               entry.preRollSamples);
        }

        advance(lead + 1);
    }

    /// Roll to @p targetBlock, starting from before the clip does.
    ///
    /// Where the pool would have cued it: a stream told where to go a second
    /// early has its material in memory by the time its clip starts, so the
    /// first block of that clip primes its stretcher with material rather than
    /// with the silence a reader that has not caught up gives. A stretcher
    /// primed on silence still plays, and fades in over its own window instead
    /// of arriving aligned, which is what a locate onto a clip sounds like and
    /// not what starting one does.
    void rollTo(int targetBlock) {
        start(targetBlock, targetBlock - kFirstBlock + 8);
    }

    void advance(int count = 1, bool continuous = true) {
        for (auto index = 0; index < count; ++index) {
            render(blockFrom(blockTime(next_), continuous || index > 0));
            ++next_;
        }
    }

    /// Jump the transport to @p targetBlock, the way a locate does: the block
    /// that lands there is not continuous with the one before it.
    void locate(int targetBlock) {
        next_ = targetBlock;
        advance(1, false);
    }

    void render(const BlockInfo& block) {
        fill();
        source.render(block, juce::dsp::AudioBlock<float>(output));
    }

    void fill() {
        auto worked = true;
        while (worked) {
            worked = false;
            for (const auto& entry : table.entries)
                worked = entry.stream->fill() || worked;
        }
    }

    float at(int sample, int channel = 0) const {
        return output.getSample(channel, sample);
    }

    double rms(int channel = 0) const {
        auto sum = 0.0;
        for (auto sample = 0; sample < kBlockSize; ++sample) {
            const auto value = output.getSample(channel, sample);
            sum += value * value;
        }
        return std::sqrt(sum / kBlockSize);
    }

    /// How often the block crosses zero, which is what a question about pitch
    /// reduces to for a single tone.
    int zeroCrossings(int channel = 0) const {
        auto crossings = 0;
        for (auto sample = 1; sample < kBlockSize; ++sample)
            if ((output.getSample(channel, sample - 1) < 0.0f) !=
                (output.getSample(channel, sample) < 0.0f))
                ++crossings;
        return crossings;
    }

    const AudioClipPlayback* clipOf(magda::ClipId clipId) const {
        for (const auto& clip : lane.audio)
            if (clip.clipId == clipId)
                return &clip;
        return nullptr;
    }

    const AudioEventPlayback* eventOf(magda::ClipId clipId, magda::EventId eventId) const {
        if (const auto* clip = clipOf(clipId))
            for (const auto& event : clip->events)
                if (event.eventId == eventId)
                    return &event;
        return nullptr;
    }

    AudioEventPlayback& event(magda::ClipId clipId) {
        return lane.audio.front().events.front();
    }

    TrackClipPlayback lane{kTrack, {}, {}};
    ClipSnapshotFeed clips;
    ClipStreamFeed streams;
    ClipAudioSource source{kTrack, clips, streams};
    ClipStreamTable table;
    juce::AudioBuffer<float> output;

    int next_ = 0;

    /// The block every clip in this file starts on, so that rolling in from
    /// before it is one number rather than one per test.
    static constexpr int kFirstBlock = 100;
};

}  // namespace

// ---------------------------------------------------------------------------
// Where in the reading a moment sits.
// ---------------------------------------------------------------------------

TEST_CASE("A clip at its file's own speed consumes one sample per sample",
          "[engine][clip][stretch]") {
    const auto clip = clipOver(1, seconds(10.0, 20.0), 500);
    const auto& event = clip.events.front();

    REQUIRE(magda::engine::readingRateOf(event) == approx(1.0));
    REQUIRE(magda::engine::readingPositionAt(clip, event, 10.0, beatAt(10.0), kSampleRate) ==
            approx(500.0));

    // One second later, one second of the file later.
    REQUIRE(magda::engine::readingPositionAt(clip, event, 11.0, beatAt(11.0), kSampleRate) ==
            approx(500.0 + kSampleRate));
}

TEST_CASE("A speed ratio is a constant factor on how fast the reading is consumed",
          "[engine][clip][stretch]") {
    auto clip = clipOver(1, seconds(10.0, 20.0), 500);
    auto& event = clip.events.front();

    SECTION("twice as fast") {
        event.speedRatio = 2.0;

        REQUIRE(magda::engine::readingRateOf(event) == approx(2.0));
        REQUIRE(magda::engine::readingPositionAt(clip, event, 11.0, beatAt(11.0), kSampleRate) ==
                approx(500.0 + 2.0 * kSampleRate));
    }

    SECTION("half as fast") {
        event.speedRatio = 0.5;

        REQUIRE(magda::engine::readingPositionAt(clip, event, 11.0, beatAt(11.0), kSampleRate) ==
                approx(500.0 + 0.5 * kSampleRate));
    }

    SECTION("a ratio no stretcher will run at is clamped rather than obeyed") {
        event.speedRatio = 1000.0;

        REQUIRE(magda::engine::readingRateOf(event) == approx(magda::engine::kMaxStretchRate));
        REQUIRE(magda::engine::readingPositionAt(clip, event, 11.0, beatAt(11.0), kSampleRate) ==
                approx(500.0 + magda::engine::kMaxStretchRate * kSampleRate));
    }
}

TEST_CASE("Auto tempo consumes the file against beats rather than against seconds",
          "[engine][clip][stretch]") {
    // A file analysed at 60 bpm, played on a 120 bpm timeline: twice as fast.
    auto clip = clipOver(1, seconds(10.0, 20.0), 0);
    auto& event = clip.events.front();
    event.autoTempo = true;
    event.interpBpm = 60.0;

    REQUIRE(magda::engine::readingRateOf(event) == approx(2.0));

    SECTION("at a constant tempo it is the equivalent constant ratio") {
        auto pinned = clip;
        pinned.events.front().autoTempo = false;
        pinned.events.front().speedRatio = 2.0;

        for (const auto at : {10.0, 12.5, 17.25}) {
            const auto following =
                magda::engine::readingPositionAt(clip, event, at, beatAt(at), kSampleRate);
            const auto fixed = magda::engine::readingPositionAt(pinned, pinned.events.front(), at,
                                                                beatAt(at), kSampleRate);
            REQUIRE(following == approx(fixed, 1e-6));
        }
    }

    SECTION("the beat face is what it reads, so a tempo that moved moves it") {
        // The same instant in seconds, but the timeline has run twice as many
        // beats to get there. A clip following the tempo has consumed twice the
        // material; one on a fixed ratio would not have noticed.
        const auto atDoubleTempo = magda::engine::readingPositionAt(
            clip, event, 11.0, beatAt(11.0) + beatAt(1.0), kSampleRate);

        REQUIRE(atDoubleTempo == approx(4.0 * kSampleRate));
    }
}

TEST_CASE("A speed ramp warps where the material is, and lands where the clip says",
          "[engine][clip][stretch]") {
    // A one second linear ramp into a clip that starts at ten seconds.
    auto clip = clipOver(1, seconds(10.0, 20.0), 0);
    clip.fadeInSeconds = 1.0;
    clip.fadeInBeats = 1.0 * kBeatsPerSecond;
    clip.fadeInBehaviour = 1;
    const auto& event = clip.events.front();

    // Not at the clip's first sample: a ramp that ran the material from the very
    // start of its region would arrive at the far end half a region behind. The
    // linear ramp's integral says half way in.
    REQUIRE(magda::engine::readingPositionAt(clip, event, 10.0, beatAt(10.0), kSampleRate) ==
            approx(0.5 * kSampleRate, 1.0));

    // And by the end of the ramp it has caught up with where an unramped clip
    // would be, which is what makes the rest of the clip play in time.
    REQUIRE(magda::engine::readingPositionAt(clip, event, 11.0, beatAt(11.0), kSampleRate) ==
            approx(1.0 * kSampleRate, 1.0));

    // Past the ramp nothing is warped at all.
    REQUIRE(magda::engine::readingPositionAt(clip, event, 12.0, beatAt(12.0), kSampleRate) ==
            approx(2.0 * kSampleRate, 1.0));
}

TEST_CASE("A reversed clip mirrors its anchor and still runs forwards through the reading",
          "[engine][clip][stretch]") {
    auto clip = clipOver(1, seconds(10.0, 11.0), 0);
    auto& event = clip.events.front();
    event.reversed = true;
    event.speedRatio = 2.0;

    const auto opens =
        magda::engine::readingPositionAt(clip, event, 10.0, beatAt(10.0), kSampleRate);
    const auto later =
        magda::engine::readingPositionAt(clip, event, 10.5, beatAt(10.5), kSampleRate);

    // Forwards through the mirrored file, at the ratio, whatever the flip did to
    // where it starts.
    REQUIRE(later - opens == approx(2.0 * 0.5 * kSampleRate));
}

// ---------------------------------------------------------------------------
// Which stretcher an event gets.
// ---------------------------------------------------------------------------

TEST_CASE("A clip asks for a stretcher only when it needs one", "[engine][clip][stretch]") {
    auto clip = clipOver(1, seconds(0.0, 10.0), 0);
    auto& event = clip.events.front();

    SECTION("its file's own speed, nothing asked: no layer at all") {
        REQUIRE(magda::engine::makeStretcher(
                    magda::engine::stretchSetupFor(clip, event, context())) == nullptr);
    }

    SECTION("analog pitch resamples, and the model already folded the pitch into the ratio") {
        event.analogPitch = true;
        event.speedRatio = 2.0;

        const auto setup = magda::engine::stretchSetupFor(clip, event, context());
        const auto stretcher = magda::engine::makeStretcher(setup);

        REQUIRE(stretcher != nullptr);

        // The curve reaches past the sample it lands on, so the reading runs a
        // little ahead of the position wanted.
        REQUIRE(stretcher->readAheadSamples() > 0);
        REQUIRE(stretcher->preRollSamples(setup.nominalRate) > 0);
    }

    SECTION("a speed ramp needs one even at the file's own speed") {
        clip.fadeInSeconds = 0.5;
        clip.fadeInBehaviour = 1;

        REQUIRE(magda::engine::makeStretcher(
                    magda::engine::stretchSetupFor(clip, event, context())) != nullptr);
    }

    SECTION("a mode set once on a clip that asks for nothing gets no engine") {
        // The mode is a preference for how to stretch, not an instruction to
        // stretch. The incumbent engages on auto tempo, auto pitch, a pitch
        // change or a ratio off unity and never on the mode alone
        // (AudioClipBase::usesTimeStretchedProxy), and a phase vocoder run at
        // one-to-one is not transparent: it re-synthesises what it was given.
        for (const auto which :
             {mode::kSignalsmith, mode::kSoundTouchNormal, mode::kSoundTouchBetter}) {
            event.timeStretchMode = which;

            INFO("mode " << which);
            REQUIRE(magda::engine::makeStretcher(
                        magda::engine::stretchSetupFor(clip, event, context())) == nullptr);
        }
    }

    SECTION("auto tempo keeps its engine even where its average is unity") {
        // The average is not what it plays at: an auto tempo ratio is the
        // project's tempo over the file's own and moves with the tempo curve, so
        // a clip averaging unity is still stretching in both directions around
        // it.
        event.timeStretchMode = mode::kSignalsmith;
        event.autoTempo = true;
        event.interpBpm = 120.0;

        const auto setup = magda::engine::stretchSetupFor(clip, event, context());

        REQUIRE(setup.nominalRate == approx(1.0));
        REQUIRE(magda::engine::makeStretcher(setup) != nullptr);
    }

    SECTION("the pinned modes each resolve to an engine that primes") {
        for (const auto which :
             {mode::kSignalsmith, mode::kSoundTouchNormal, mode::kSoundTouchBetter}) {
            event.timeStretchMode = which;
            event.speedRatio = 2.0;

            const auto setup = magda::engine::stretchSetupFor(clip, event, context());
            const auto stretcher = magda::engine::makeStretcher(setup);

            REQUIRE(stretcher != nullptr);

            // Every one of them holds material back, and says how much, which is
            // what the pool cues behind the clip's own start.
            REQUIRE(stretcher->preRollSamples(setup.nominalRate) > 0);
            REQUIRE(stretcher->readAheadSamples() == 0);
        }
    }

    SECTION("a mode this build has no engine for falls back rather than skipping material") {
        event.timeStretchMode = 6;  // elastiquePro, which is not vendored here
        event.speedRatio = 2.0;

        // The default engine, as the incumbent answers the same question. Not
        // null: the position map has already decided this block consumes twice
        // its length, so a clip with nothing to consume it through would read
        // that material at unity and skip the rest at every block boundary.
        REQUIRE(magda::engine::makeStretcher(
                    magda::engine::stretchSetupFor(clip, event, context())) != nullptr);
    }

    SECTION("a transpose with nothing else asked for still gets one") {
        // The model's rule upgrades a mode left at Off for auto tempo, warp, a
        // ratio and a pitch change, and not for the transpose an auto pitch clip
        // carries. A clip that only transposes would otherwise be handed
        // semitones with nothing to apply them with, and would play untransposed.
        event.autoPitch = true;
        event.transpose = -4;

        const auto setup = magda::engine::stretchSetupFor(clip, event, context());

        CHECK(setup.semitones == approx(-4.0));
        CHECK(setup.mode == mode::kSignalsmith);
        REQUIRE(magda::engine::makeStretcher(setup) != nullptr);
    }

    SECTION("and analog pitch still does not, because its pitch is not a stretch") {
        event.analogPitch = true;
        event.pitchChange = 7.0f;
        event.speedRatio = std::pow(2.0, 7.0 / 12.0);

        const auto setup = magda::engine::stretchSetupFor(clip, event, context());

        CHECK(setup.mode == mode::kDisabled);
        CHECK(magda::engine::makeStretcher(setup)->readAheadSamples() > 0);
    }
}

TEST_CASE("Pitch is the transpose a following clip has and the change a fixed one has",
          "[engine][clip][stretch]") {
    auto clip = clipOver(1, seconds(0.0, 10.0), 0);
    auto& event = clip.events.front();
    event.pitchChange = 3.0f;
    event.transpose = -5;

    REQUIRE(magda::engine::stretchSetupFor(clip, event, context()).semitones == approx(3.0));

    event.autoPitch = true;
    REQUIRE(magda::engine::stretchSetupFor(clip, event, context()).semitones == approx(-5.0));
}

// ---------------------------------------------------------------------------
// Playing it.
// ---------------------------------------------------------------------------

TEST_CASE("A resampled clip plays the samples its position map named", "[engine][clip][stretch]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 300), 0));

    auto& event = rig.event(1);
    event.analogPitch = true;
    event.speedRatio = 2.0;

    rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();
    rig.rollTo(120);

    // Twice as fast: the block that begins twenty blocks into the clip is the
    // fortieth block of the file, and it advances two samples per sample.
    const auto opens = 2.0 * (blockTime(120) - blockTime(100)) * kSampleRate;

    REQUIRE(rig.at(0) == approx(opens, 0.05));
    REQUIRE(rig.at(1) == approx(opens + 2.0, 0.05));
    REQUIRE(rig.at(kBlockSize - 1) == approx(opens + 2.0 * (kBlockSize - 1), 0.05));

    // And the next block continues it exactly, with no sample lost or repeated
    // at the join.
    rig.advance();
    REQUIRE(rig.at(0) == approx(opens + 2.0 * kBlockSize, 0.05));
}

TEST_CASE("A resampled clip lands between samples rather than on the nearest one",
          "[engine][clip][stretch]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 300), 0));

    auto& event = rig.event(1);
    event.analogPitch = true;
    event.speedRatio = 0.5;

    rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();
    rig.rollTo(120);

    const auto opens = 0.5 * (blockTime(120) - blockTime(100)) * kSampleRate;

    // Half speed is a sample every other output sample, so the ones in between
    // are the halves the curve found.
    REQUIRE(rig.at(0) == approx(opens, 0.05));
    REQUIRE(rig.at(1) == approx(opens + 0.5, 0.05));
    REQUIRE(rig.at(2) == approx(opens + 1.0, 0.05));
}

TEST_CASE("A locate into a stretched clip resumes at the material the timeline names",
          "[engine][clip][stretch]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

    auto& event = rig.event(1);
    event.analogPitch = true;
    event.speedRatio = 2.0;

    rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();
    rig.rollTo(120);

    // Somewhere else entirely, with nothing said about it: the reader seeks, the
    // stretcher resets and primes from the material in front of where it landed.
    rig.locate(300);
    rig.advance(2);

    const auto opens = 2.0 * (blockTime(302) - blockTime(100)) * kSampleRate;

    // Aligned, not a pre-roll late. What was primed with was the material
    // before this position, not the material at it.
    REQUIRE(rig.at(0) == approx(opens, 0.05));
}

TEST_CASE("A stretched clip keeps its pitch and a resampled one does not",
          "[engine][clip][stretch]") {
    constexpr double kTone = 1000.0;

    // A tone at 1 kHz through a 512 sample block: about twelve cycles, so about
    // twenty four zero crossings, and twice that if the pitch went up an octave.
    const auto crossingsAtUnity = 2 * static_cast<int>(kTone * kBlockSize / kSampleRate);

    SECTION("stretched") {
        Rig rig;
        rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

        auto& event = rig.event(1);
        event.timeStretchMode = mode::kSignalsmith;
        event.speedRatio = 2.0;

        rig.give(1, 1, std::make_unique<SineReader>(kTone));
        rig.publish();
        rig.rollTo(140);

        // Twice the material in the same time, and the same note.
        REQUIRE(rig.rms() > 0.3);
        REQUIRE(rig.zeroCrossings() ==
                Catch::Approx(crossingsAtUnity).margin(crossingsAtUnity / 4));
    }

    SECTION("resampled") {
        Rig rig;
        rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

        auto& event = rig.event(1);
        event.analogPitch = true;
        event.speedRatio = 2.0;

        rig.give(1, 1, std::make_unique<SineReader>(kTone));
        rig.publish();
        rig.rollTo(140);

        // Twice the material in the same time, an octave up: tape, not a
        // stretcher.
        REQUIRE(rig.rms() > 0.3);
        REQUIRE(rig.zeroCrossings() ==
                Catch::Approx(2 * crossingsAtUnity).margin(crossingsAtUnity / 4));
    }
}

TEST_CASE("Both SoundTouch modes play, because projects were saved naming them",
          "[engine][clip][stretch]") {
    for (const auto which : {mode::kSoundTouchNormal, mode::kSoundTouchBetter}) {
        Rig rig;
        rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

        auto& event = rig.event(1);
        event.timeStretchMode = which;
        event.speedRatio = 2.0;

        rig.give(1, 1, std::make_unique<SineReader>(1000.0));
        rig.publish();
        rig.rollTo(140);

        // A full block of material rather than the silence a pipe that had not
        // caught up would give.
        INFO("mode " << which);
        REQUIRE(rig.rms() > 0.2);
    }
}

TEST_CASE("Auto tempo plays through a stretcher without being told a ratio",
          "[engine][clip][stretch]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

    auto& event = rig.event(1);
    event.autoTempo = true;
    event.interpBpm = kBpm / 2.0;  // half the timeline's, so twice as fast
    event.timeStretchMode = mode::kSignalsmith;

    rig.give(1, 1, std::make_unique<SineReader>(1000.0));
    rig.publish();
    rig.rollTo(140);

    REQUIRE(rig.rms() > 0.3);
}

TEST_CASE("A speed ramp changes the speed rather than the gain", "[engine][clip][stretch]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

    auto& clip = rig.lane.audio.front();
    clip.fadeInSeconds = blockTime(110) - blockTime(100);
    clip.fadeInBeats = clip.fadeInSeconds * kBeatsPerSecond;
    clip.fadeInBehaviour = 1;

    rig.give(1, 1, std::make_unique<ConstantReader>());
    rig.publish();
    rig.rollTo(102);

    // Two blocks into a ten block ramp. A gain fade would be a fifth of the way
    // up and audibly quiet; a speed ramp leaves the level alone and moves the
    // material instead.
    REQUIRE(rig.at(0) == approx(1.0, 1e-3));
    REQUIRE(rig.at(kBlockSize - 1) == approx(1.0, 1e-3));
}

TEST_CASE("A speed ramp reads the file at a rate that climbs to unity", "[engine][clip][stretch]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

    auto& clip = rig.lane.audio.front();
    clip.fadeInSeconds = blockTime(110) - blockTime(100);
    clip.fadeInBeats = clip.fadeInSeconds * kBeatsPerSecond;
    clip.fadeInBehaviour = 1;

    rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();
    rig.rollTo(101);

    // Inside the ramp the material is ahead of where an unramped clip would be,
    // and moving slower than one sample per sample.
    const auto opens = rig.at(0);
    const auto step = rig.at(kBlockSize - 1) - rig.at(0);

    REQUIRE(opens > (blockTime(101) - blockTime(100)) * kSampleRate);
    REQUIRE(step < kBlockSize - 1);

    // By the far end of the ramp it is running at the file's own speed again,
    // and where the clip says it should be.
    rig.advance(10);
    REQUIRE(rig.at(kBlockSize - 1) - rig.at(0) == approx(kBlockSize - 1, 0.5));
    REQUIRE(rig.at(0) == approx((blockTime(111) - blockTime(100)) * kSampleRate, 2.0));
}

TEST_CASE("A voice primes the stretcher it is handed, and the one that replaces it",
          "[engine][clip][stretch]") {
    // A rate, pitch or mode edit on a sounding clip swaps in a fresh stretcher
    // and leaves the stream alone (ClipVoicePool::service). The entry a voice is
    // playing does not change when it does, so a voice that remembered only
    // *that* it had primed would run the new engine cold, and cold for a phase
    // vocoder is its whole latency out of step with every other voice on the
    // track, for the rest of the take.
    //
    // Driven directly rather than through a track, because what is being
    // asserted is which calls a voice makes rather than what they sound like.
    magda::engine::ClipVoice voice;
    voice.prepare(context());

    const auto clip = clipOver(1, blocks(0, 400), 0);
    const auto& event = clip.events.front();

    PrefetchStream stream(std::make_unique<CountingReader>(), context(), {8192, 8});

    juce::AudioBuffer<float> scratch(2, magda::engine::stretchScratchSamples(kBlockSize));
    juce::AudioBuffer<float> out(2, kBlockSize);
    scratch.clear();
    out.clear();

    CountingStretcher first;
    CountingStretcher second;

    const auto render = [&](magda::engine::ClipStretcher& stretcher, int block, bool continuous) {
        voice.render(clip, event, blockFrom(blockTime(block), continuous), stream, &stretcher, 0,
                     juce::dsp::AudioBlock<float>(scratch), juce::dsp::AudioBlock<float>(out));
    };

    render(first, 10, false);
    CHECK(first.primes == 1);

    // Carrying on, so nothing is primed again: priming reads behind where the
    // block is, and doing that every block would seek every block.
    render(first, 11, true);
    render(first, 12, true);
    CHECK(first.primes == 1);

    // The edit. Same clip, same event, same stream, continuous block, different
    // engine, and it has to be primed.
    render(second, 13, true);
    CHECK(second.primes == 1);
    CHECK(first.primes == 1);

    // And a jump primes whatever is in place, as before.
    render(second, 200, false);
    CHECK(second.primes == 2);
}

TEST_CASE("An auto tempo ratio past what a stretcher will run at stays inside its buffers",
          "[engine][clip][stretch]") {
    // A constant ratio is clamped where it is read, and auto tempo's is not: it
    // is the project's tempo over a file's own analysed bpm, and nothing bounds
    // their quotient. A file analysed at fifteen bpm under this rig is past the
    // ceiling by a long way. It plays wrongly, which is what a clamped ratio
    // does too; what it must not do is read more than the block was sized for.
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

    auto& event = rig.event(1);
    event.autoTempo = true;
    event.interpBpm = 5.0;
    event.timeStretchMode = mode::kSoundTouchNormal;

    rig.give(1, 1, std::make_unique<SineReader>(1000.0));
    rig.publish();
    rig.rollTo(140);

    // Nothing to assert about the sound of it. That it got here at all is the
    // assertion: the reading a block asks for is bounded by the same function
    // every buffer behind it was sized against.
    CHECK(magda::engine::readingRateOf(event) == approx(magda::engine::kMaxStretchRate));
    CHECK(std::isfinite(rig.rms()));
}

TEST_CASE("SoundTouch runs the whole rate range without growing a buffer under a callback",
          "[engine][clip][stretch]") {
    // SoundTouch grows its own pipes on demand, and both the calls that feed it
    // are on the audio thread, so its buffers are pushed to their worst case on
    // the thread that builds it (SoundTouchClipStretcher). The worst case is the
    // sequence it keeps back plus the largest single push a callback can hand
    // it, and it is settled in one call because putSamples grows to hold what a
    // single call hands it: the same total in block-sized pieces only grows it
    // to whatever residue that rate happened to leave.
    //
    // What this covers is the range that has to have been reached: both modes,
    // rates from a sixth to the ceiling, a pitch shift so the rate transposer is
    // in the chain too, auto tempo so the rate moves block to block, and locates
    // in both directions so priming runs again over material the reader has to
    // seek for.
    //
    // Verified against a counter on the vendored ensureCapacity, over eighty two
    // rate and mode combinations rather than the seven kept here, where it stays
    // at zero throughout. That counter is what caught the two versions of this
    // that did not work: one where a clip playing at nine allocated after a
    // warm-up that had run at ten, and one where the buffers behind the front of
    // the pipe grew at a tempo the warm-up had stepped over.
    for (const auto which : {mode::kSoundTouchNormal, mode::kSoundTouchBetter}) {
        for (const auto ratio : {0.15, 0.5, 1.7, 2.0, 4.3, 9.0, 9.97}) {
            Rig rig;
            rig.lane.audio.push_back(clipOver(1, blocks(100, 400), 0));

            auto& event = rig.event(1);
            event.timeStretchMode = which;
            event.autoTempo = true;
            event.interpBpm = kBpm / ratio;
            event.pitchChange = 4.0f;

            rig.give(1, 1, std::make_unique<SineReader>(1000.0));
            rig.publish();

            rig.rollTo(140);
            rig.advance(20);
            rig.locate(300);
            rig.advance(20);
            rig.locate(150);
            rig.advance(30);

            INFO("mode " << which << " ratio " << ratio);
            CHECK(std::isfinite(rig.rms()));
        }
    }
}

TEST_CASE("A stretcher is the same DSP whatever block size it was built for",
          "[engine][clip][stretch]") {
    // What the block-size gate asserts over the corpus (#2078), at the level the
    // state that broke it actually lives.
    //
    // A stretcher is handed the block size so it can size its buffers, and a
    // buffer size is the one thing it is entitled to take from there. What it
    // must not take is any part of what it does: a voice drives every mode in
    // fixed 128-sample cells however the host batches its callbacks
    // (ClipVoice::renderThroughCells), so two stretchers built for different
    // block sizes and then fed the same cells are being asked the same question
    // and owe the same samples.
    //
    // SoundTouch did not, and the corpus caught it at 4096 while 64, 96 and 512
    // agreed. Two block-derived figures reached the library rather than staying
    // in the buffer sizing: the pre-roll cushion, and the size of the single
    // push the warm-up settles capacity with. The second is the one that hides,
    // because the warm-up ends in touch_.clear() and looks like it left nothing
    // behind. It does not. TDStretch::skipFract is the fraction of a sample the
    // last splice did not use, it is carried into the next one, and nothing
    // resets it: not clear(), which clears the FIFOs and the overlap buffer, and
    // not setTempo(), which recalculates the lengths around it. Only
    // setParameters does, and the warm-up never calls it. So the block size
    // decided how much silence went through, that decided the fraction left
    // over, and playback started from it.
    //
    // Which is why this is asserted bit for bit rather than within a floor. The
    // divergence it produces is not proportional to its cause: in Better mode
    // the seek is a full correlation search, so a fraction of a sample moves
    // which offset wins and the two renders come apart by tens of decibels or by
    // nothing at all, depending on how near the tie was. A floor would have
    // called the corpus green on one machine and red on another, which is
    // exactly what happened before this was understood.
    for (const auto which :
         {mode::kSoundTouchNormal, mode::kSoundTouchBetter, mode::kSignalsmith}) {
        constexpr auto kCell = magda::engine::kStretchCellSamples;
        constexpr auto kCells = 160;
        constexpr auto kRate = 1.37;

        const auto readPerCell = static_cast<int>(std::llround(kCell * kRate));

        // A tone rather than noise, and two channels that differ, so a stretcher
        // that crossed them or lost one is visible here too.
        juce::AudioBuffer<float> material(2, kCells * readPerCell + 64);
        for (auto channel = 0; channel < 2; ++channel)
            for (auto sample = 0; sample < material.getNumSamples(); ++sample)
                material.setSample(
                    channel, sample,
                    static_cast<float>(0.4 *
                                       std::sin(2.0 * juce::MathConstants<double>::pi *
                                                (220.0 + 55.0 * channel) * sample / 44100.0)));

        const auto renderAt = [&](int maxBlockSamples) {
            StretchSetup setup;
            setup.mode = which;
            setup.numChannels = 2;
            setup.sampleRate = 44100.0;
            setup.maxBlockSamples = maxBlockSamples;
            setup.nominalRate = kRate;

            auto stretcher = magda::engine::makeStretcher(setup);

            juce::AudioBuffer<float> rendered(2, kCells * kCell);
            rendered.clear();

            if (stretcher == nullptr)
                return rendered;

            for (auto cell = 0; cell < kCells; ++cell) {
                juce::dsp::AudioBlock<const float> input(material);
                juce::dsp::AudioBlock<float> output(rendered);

                stretcher->process(input.getSubBlock(static_cast<std::size_t>(cell * readPerCell),
                                                     static_cast<std::size_t>(readPerCell)),
                                   0.0, kRate,
                                   output.getSubBlock(static_cast<std::size_t>(cell * kCell),
                                                      static_cast<std::size_t>(kCell)));
            }

            return rendered;
        };

        // 512 is what the corpus renders at; the other three are the gate's
        // rungs. Each is compared against 512 rather than against its
        // neighbour, so a failure names the size that drifted.
        const auto reference = renderAt(512);
        REQUIRE(reference.getMagnitude(0, reference.getNumSamples()) > 0.001f);

        for (const auto blockSize : {64, 96, 4096}) {
            const auto other = renderAt(blockSize);

            INFO("mode " << which << " at block size " << blockSize);
            REQUIRE(other.getNumSamples() == reference.getNumSamples());

            auto worst = 0.0f;
            auto worstAt = -1;
            for (auto channel = 0; channel < 2; ++channel)
                for (auto sample = 0; sample < reference.getNumSamples(); ++sample) {
                    const auto difference = std::abs(other.getSample(channel, sample) -
                                                     reference.getSample(channel, sample));
                    if (difference > worst) {
                        worst = difference;
                        worstAt = sample;
                    }
                }

            INFO("worst difference " << worst << " at sample " << worstAt);
            CHECK(worst == 0.0f);
        }
    }
}

TEST_CASE("Nothing a stretcher pushes is derived from the block size", "[engine][clip][stretch]") {
    // The invariant behind the test above, asserted as arithmetic rather than
    // as audio (#2078).
    //
    // It is worth having both, because the audio one can only fail where the
    // divergence happens to be audible. What SoundTouch does with a difference
    // in its input is not proportional to the difference: in Better mode a
    // fraction of a sample moves which offset a correlation search picks, so the
    // same wrong state came apart by 36 dB on x86 and by nothing at all on ARM.
    // A test that only compares renders is therefore a test that passes on the
    // machine you happen to be holding. These two numbers are the cause, they
    // are the same on every machine, and a block size must not appear in either.
    //
    // stretchPushSamples takes no block size, so it cannot vary; it is asserted
    // anyway against the figure it has to agree with, since the whole point of
    // it is being the same number the buffer sizing uses.
    CHECK(magda::engine::stretchPushSamples() ==
          magda::engine::maxReadingSamples(magda::engine::kStretchCellSamples));

    for (const auto which :
         {mode::kSoundTouchNormal, mode::kSoundTouchBetter, mode::kSignalsmith, mode::kDisabled}) {
        for (const auto rate : {0.15, 0.5, 1.0, 1.37, 2.0, 9.97}) {
            const auto preRollAt = [&](int maxBlockSamples) {
                StretchSetup setup;
                setup.mode = which;
                setup.numChannels = 2;
                setup.sampleRate = 44100.0;
                setup.maxBlockSamples = maxBlockSamples;
                setup.nominalRate = rate;

                auto stretcher = magda::engine::makeStretcher(setup);
                return stretcher != nullptr ? stretcher->preRollSamples(rate) : 0;
            };

            const auto reference = preRollAt(512);

            for (const auto blockSize : {64, 96, 4096}) {
                INFO("mode " << which << " rate " << rate << " at block size " << blockSize);
                CHECK(preRollAt(blockSize) == reference);
            }
        }
    }
}
