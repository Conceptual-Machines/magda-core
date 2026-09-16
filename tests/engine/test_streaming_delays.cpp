#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ClipCallback.hpp"
#include "ReaderGate.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipStretcher.hpp"
#include "clip/ClipVoicePool.hpp"
#include "clip/EventPlacement.hpp"
#include "core/TimeStretchModes.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * Clip voices whose prefetch reader is delayed on purpose (#2700). Each case
 * renders beside a control whose reader keeps up, and measures the difference
 * in output samples. docs/issues/2699-streaming-seek-audit.md has the findings.
 */

using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::AudioFileReader;
using magda::engine::BlockInfo;
using magda::engine::ClipAudioSource;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSnapshotFeed;
using magda::engine::ClipStreamFeed;
using magda::engine::ClipStreamTable;
using magda::engine::ClipVoicePool;
using magda::engine::kStretchCellSamples;
using magda::engine::LaunchHandle;
using magda::engine::LaunchHandleFeed;
using magda::engine::LaunchHandleTable;
using magda::engine::PrefetchSettings;
using magda::engine::PrefetchThread;
using magda::engine::ReadPurpose;
using magda::engine::RenderContext;
using magda::engine::SamplePosition;
using magda::engine::Section;
using magda::engine::SessionSlotPlayback;
using magda::engine::SlotKey;
using magda::engine::SnapshotSpan;
using magda::engine::TrackClipPlayback;
using magda::test::ReaderGate;

namespace mode = magda::time_stretch_mode;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kSecondsPerBeat = 0.5;
constexpr magda::TrackId kTrack = 9;
constexpr int kScene = 0;

/// Room for a Signalsmith priming window with reading to spare.
constexpr PrefetchSettings kPool{2048, 8};

enum class Material {
    /// Frame n reads back as n + 1, so plain playback shows its position exactly.
    counting,
    /// 882 Hz at a level that steps every kLevelStep frames, never the same way twice.
    /// Not DC: after any gap Signalsmith settles DC at another level. Not a steady
    /// tone either: a render that came back late would match one.
    tone,
};

constexpr int kLevelStep = 4096;

/// The level of the step @p frame falls in, between 0.2 and 0.5.
float levelAt(std::int64_t frame) {
    const auto step = static_cast<std::uint64_t>(frame / kLevelStep);
    const auto scrambled = (step * 6364136223846793005ULL + 1442695040888963407ULL) >> 33;
    return 0.2f + 0.3f * static_cast<float>(scrambled % 4096) / 4096.0f;
}

class SourceFile final : public AudioFileReader {
  public:
    SourceFile(Material material, std::int64_t length, ReaderGate* gate,
               std::atomic<std::int64_t>& furthest)
        : material_(material), length_(length), gate_(gate), furthest_(furthest) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t start,
             int numSamples) override {
        if (gate_ != nullptr)
            gate_->pass(start);

        const auto available =
            static_cast<int>(std::clamp<std::int64_t>(length_ - start, 0, numSamples));
        for (auto sample = 0; sample < available; ++sample) {
            const auto value = valueAt(start + sample);
            for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
                destination.setSample(channel, destinationOffset + sample, value);
        }

        if (start + available > furthest_.load())
            furthest_.store(start + available);
        return available;
    }

  private:
    float valueAt(std::int64_t frame) const {
        if (frame < 0)
            return 0.0f;
        if (material_ == Material::counting)
            return static_cast<float>(frame + 1);
        return levelAt(frame) *
               static_cast<float>(std::cos(2.0 * juce::MathConstants<double>::pi * 882.0 *
                                           static_cast<double>(frame) / kSampleRate));
    }

    Material material_;
    std::int64_t length_;
    ReaderGate* gate_;
    std::atomic<std::int64_t>& furthest_;
};

class Files final : public magda::engine::AudioFileReaderFactory {
  public:
    std::unique_ptr<AudioFileReader> open(const std::string&) override {
        return std::make_unique<SourceFile>(material, length, gate, furthest);
    }

    Material material = Material::tone;
    std::int64_t length = 100000000;
    ReaderGate* gate = nullptr;

    /// The end of the furthest read the worker has made.
    std::atomic<std::int64_t> furthest{0};
};

struct Playback {
    int mode = mode::kDisabled;
    double speed = 1.0;
    int blockSize = 128;
    /// A two second speed ramp into the clip, so the rate changes under the stall.
    bool speedRamp = false;

    bool stretched() const {
        return speed != 1.0;
    }

    Material material() const {
        return stretched() ? Material::tone : Material::counting;
    }
};

std::string describe(const Playback& playback) {
    const auto* name = playback.mode == mode::kSoundTouchNormal   ? "SoundTouch normal"
                       : playback.mode == mode::kSoundTouchBetter ? "SoundTouch better"
                       : playback.mode == mode::kSignalsmith      ? "Signalsmith"
                                                                  : "plain";
    return std::string(name) + " x" + juce::String(playback.speed, 2).toStdString() + " at " +
           std::to_string(playback.blockSize);
}

/// Plain, both SoundTouch modes and Signalsmith, at 0.8 and 1.2, in 64/128/512 blocks.
std::vector<Playback> playbackMatrix() {
    std::vector<Playback> all;
    for (const auto blockSize : {64, 128, 512}) {
        all.push_back({mode::kDisabled, 1.0, blockSize});
        for (const auto stretch :
             {mode::kSoundTouchNormal, mode::kSoundTouchBetter, mode::kSignalsmith})
            for (const auto speed : {0.8, 1.2})
                all.push_back({stretch, speed, blockSize});
    }
    return all;
}

SnapshotSpan spanOf(double startSeconds, double endSeconds) {
    SnapshotSpan span;
    span.seconds = {startSeconds, endSeconds};
    span.beats = {startSeconds / kSecondsPerBeat, endSeconds / kSecondsPerBeat};
    return span;
}

AudioClipPlayback clipFor(const Playback& playback, double lengthSeconds = 30.0) {
    AudioClipPlayback clip;
    clip.clipId = 1;
    clip.span = spanOf(0.0, lengthSeconds);
    clip.launchFadeSamples = 0;
    if (playback.speedRamp) {
        clip.fadeInSeconds = 2.0;
        clip.fadeInBeats = 2.0 / kSecondsPerBeat;
        clip.fadeInBehaviour = 1;
    }

    AudioEventPlayback event;
    event.eventId = 1;
    event.sourceId = 1;
    event.filePath = "take.wav";
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 1000.0;
    event.span = clip.span;
    event.speedRatio = playback.speed;
    event.timeStretchMode = playback.mode;
    clip.events.push_back(std::move(event));
    return clip;
}

BlockInfo blockAt(std::int64_t startSample, int count, bool continuous, bool playing,
                  std::int64_t monotonicStart) {
    BlockInfo block;
    block.numSamples = count;
    block.sampleRate = kSampleRate;
    block.playing = playing;
    block.continuous = continuous;
    block.monotonicSamples = {SamplePosition{monotonicStart},
                              SamplePosition{monotonicStart + count}};
    block.seconds.start = static_cast<double>(startSample) / kSampleRate;
    block.seconds.end = static_cast<double>(startSample + count) / kSampleRate;
    block.beats.start = block.seconds.start / kSecondsPerBeat;
    block.beats.end = block.seconds.end / kSecondsPerBeat;
    block.monotonicSeconds.start = static_cast<double>(monotonicStart) / kSampleRate;
    block.monotonicSeconds.end = static_cast<double>(monotonicStart + count) / kSampleRate;
    block.monotonicBeats.start = block.monotonicSeconds.start / kSecondsPerBeat;
    block.monotonicBeats.end = block.monotonicSeconds.end / kSecondsPerBeat;
    return block;
}

/// One track, its voice pool, and a worker that reads only when told to.
struct Rig {
    struct Segment {
        std::int64_t timeline = 0;
        int count = 0;
        bool continuous = true;
    };

    explicit Rig(const Playback& playbackIn, Section sectionIn = Section::Arrangement,
                 PrefetchSettings settings = kPool, ReaderGate* gate = nullptr, int slotCount = 1)
        : playback(playbackIn), section(sectionIn), pool(files, reader, context(), settings) {
        files.material = playback.material();
        files.gate = gate;

        launches.resize(static_cast<std::size_t>(slotCount));
        for (auto scene = 0; scene < slotCount; ++scene)
            table.entries.push_back(
                LaunchHandleTable::Entry{.key = SlotKey{kTrack, scene},
                                         .handle = &launches[static_cast<std::size_t>(scene)]});
        handles.publish(std::make_shared<const LaunchHandleTable>(table));

        source = section == Section::Session
                     ? std::make_unique<ClipAudioSource>(kTrack, clips, pool.feed(), handles,
                                                         Section::Session)
                     : std::make_unique<ClipAudioSource>(kTrack, clips, pool.feed());
        source->prepare(context());
        output.setSize(2, playback.blockSize);
        heard.reserve(std::size_t{1} << 20);
    }

    RenderContext context() const {
        return RenderContext{kSampleRate, playback.blockSize, 2};
    }

    void arrange(const AudioClipPlayback& clip) {
        TrackClipPlayback track;
        track.trackId = kTrack;
        track.audio.push_back(clip);
        publish(std::move(track));
    }

    void prepareLoop(std::int64_t start, std::int64_t end) {
        const magda::engine::TempoMap tempo({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
        pool.setTransport({.enabled = true,
                           .startBeat = static_cast<double>(start) / kSampleRate / kSecondsPerBeat,
                           .endBeat = static_cast<double>(end) / kSampleRate / kSecondsPerBeat},
                          tempo);
    }

    /// One slot per handle, scene by scene, each holding its own clip.
    void slot(const AudioClipPlayback& clip, double lengthBeats) {
        TrackClipPlayback track;
        track.trackId = kTrack;

        for (std::size_t scene = 0; scene < launches.size(); ++scene) {
            auto own = clip;
            own.clipId = static_cast<magda::ClipId>(scene + 1);
            own.events.front().eventId = own.clipId;

            SessionSlotPlayback slot;
            slot.sceneIndex = static_cast<int>(scene);
            slot.lengthBeats = lengthBeats;
            slot.audio.push_back(std::move(own));
            track.session.push_back(std::move(slot));
        }

        publish(std::move(track));
    }

    void publish(TrackClipPlayback track) {
        auto snapshot = std::make_shared<ClipSnapshot>();
        snapshot->tracks.push_back(std::move(track));
        pool.setSnapshot(snapshot);
        pool.service();
        clips.publish(std::move(snapshot));
    }

    /// One device callback: when @p fill, every worker round that has work, then the segments.
    void callback(const std::vector<Segment>& segments, bool fill, bool playing = true) {
        if (fill)
            pool.fillNow();

        output.clear();
        auto offset = 0;
        for (const auto& segment : segments) {
            if (segment.count <= 0)
                continue;
            const auto block =
                blockAt(segment.timeline, segment.count, segment.continuous, playing, monotonic);
            if (section == Section::Session)
                magda::engine::advanceLaunchHandles(handles, requests, block);

            auto part = juce::dsp::AudioBlock<float>(output).getSubBlock(
                static_cast<std::size_t>(offset), static_cast<std::size_t>(segment.count));
            magda::test::renderBlock(*source, clips, block, part,
                                     section == Section::Session ? &handles : nullptr);
            offset += segment.count;
            monotonic += segment.count;
        }

        if (playing)
            for (auto sample = 0; sample < offset; ++sample)
                heard.push_back(output.getSample(0, sample));
    }

    void play(std::int64_t timeline, bool continuous, bool fill) {
        callback({{timeline, playback.blockSize, continuous}}, fill);
    }

    void stopped(std::int64_t timeline, bool fill) {
        callback({{timeline, playback.blockSize, false}}, fill, false);
    }

    /// Callbacks from @p timeline covering at least @p samples, the worker keeping up.
    std::int64_t playOn(std::int64_t timeline, std::int64_t samples) {
        for (std::int64_t done = 0; done < samples; done += playback.blockSize) {
            play(timeline, true, true);
            timeline += playback.blockSize;
        }
        return timeline;
    }

    /// A locate the worker has had a full round for while stopped.
    void preparedLocate(std::int64_t timeline) {
        stopped(timeline, true);
        pool.fillNow();
        play(timeline, false, false);
    }

    ClipStreamTable::Entry entry(int index = 0) {
        const ClipStreamFeed::Reader published(pool.feed());
        REQUIRE(published);
        REQUIRE(std::ssize(published->entries) > index);
        return published->entries[static_cast<std::size_t>(index)];
    }

    std::int64_t missing(ReadPurpose purpose, int index = 0) {
        return entry(index).stream->missingFrames(purpose);
    }

    LaunchHandle& handle(int scene = 0) {
        return launches[static_cast<std::size_t>(scene)];
    }

    Playback playback;
    Section section;
    Files files;
    PrefetchThread reader{false};
    ClipVoicePool pool;
    ClipSnapshotFeed clips;
    std::deque<LaunchHandle> launches;
    LaunchHandleTable table;
    LaunchHandleFeed handles;
    magda::engine::LaunchRequestQueue requests;
    std::unique_ptr<ClipAudioSource> source;
    juce::AudioBuffer<float> output;
    std::vector<float> heard;
    std::int64_t monotonic = 0;
};

constexpr int kSilenceWindow = 64;
constexpr int kEnvelopeWindow = 512;

double rms(const std::vector<float>& samples, std::int64_t from, int count) {
    double sum = 0.0;
    for (auto i = 0; i < count; ++i) {
        const auto value = static_cast<double>(samples[static_cast<std::size_t>(from + i)]);
        sum += value * value;
    }
    return std::sqrt(sum / count);
}

/// How a render differs from its control over a stretch of output, in offsets from its start.
struct Damage {
    std::int64_t firstAudible = -1;
    /// Samples at exactly zero where the control is not. Exact for counting material.
    std::int64_t zeroWhereAudible = 0;
    /// Windows of kSilenceWindow at RMS < 0.01 where the control's is >= 0.1.
    std::int64_t silentWhereAudible = 0;
    std::int64_t lastDifference = -1;
    /// End of the last kEnvelopeWindow whose RMS differs from the control's by > 0.02.
    std::int64_t lastEnvelopeDifference = -1;
    float worstDifference = 0.0f;
};

Damage compare(const std::vector<float>& heard, std::int64_t heardFrom,
               const std::vector<float>& control, std::int64_t controlFrom, std::int64_t length) {
    REQUIRE(heardFrom + length <= std::ssize(heard));
    REQUIRE(controlFrom + length <= std::ssize(control));

    Damage damage;
    for (std::int64_t sample = 0; sample < length; ++sample) {
        const auto h = heard[static_cast<std::size_t>(heardFrom + sample)];
        const auto c = control[static_cast<std::size_t>(controlFrom + sample)];
        if (damage.firstAudible < 0 && std::abs(h) >= 1.0e-4f)
            damage.firstAudible = sample;
        if (h == 0.0f && c != 0.0f)
            ++damage.zeroWhereAudible;
        const auto difference = std::abs(h - c);
        damage.worstDifference = std::max(damage.worstDifference, difference);
        if (difference > 1.0e-3f)
            damage.lastDifference = sample;
    }

    for (std::int64_t at = 0; at + kSilenceWindow <= length; at += kSilenceWindow)
        if (rms(heard, heardFrom + at, kSilenceWindow) < 0.01 &&
            rms(control, controlFrom + at, kSilenceWindow) >= 0.1)
            damage.silentWhereAudible += kSilenceWindow;

    for (std::int64_t at = 0; at + kEnvelopeWindow <= length; at += kEnvelopeWindow)
        if (std::abs(rms(heard, heardFrom + at, kEnvelopeWindow) -
                     rms(control, controlFrom + at, kEnvelopeWindow)) > 0.02)
            damage.lastEnvelopeDifference = at + kEnvelopeWindow;

    return damage;
}

/// The most reading @p outputSamples of playback can take, cell by cell.
std::int64_t mostReading(const Playback& playback, std::int64_t outputSamples) {
    if (!playback.stretched())
        return outputSamples;
    const auto cells = (outputSamples + kStretchCellSamples - 1) / kStretchCellSamples + 1;
    return cells * (static_cast<std::int64_t>(std::ceil(kStretchCellSamples * playback.speed)) + 1);
}

/// The least reading @p outputSamples of playback can take.
std::int64_t leastReading(const Playback& playback, std::int64_t outputSamples) {
    if (!playback.stretched())
        return outputSamples;
    const auto cells = outputSamples / kStretchCellSamples - 1;
    return std::max<std::int64_t>(
        0,
        cells * (static_cast<std::int64_t>(std::floor(kStretchCellSamples * playback.speed)) - 1));
}

std::int64_t coverageOf(const Playback& playback, PrefetchSettings settings) {
    return static_cast<std::int64_t>(std::max(settings.chunkSamples, playback.blockSize)) *
           settings.chunkCount;
}

std::int64_t roundUp(std::int64_t samples, int blockSize) {
    return (samples + blockSize - 1) / blockSize * blockSize;
}

/// Priming frames at or after sample zero for a voice starting at @p timeline.
std::int64_t primingFramesAt(Rig& rig, std::int64_t timeline) {
    const auto entry = rig.entry();
    if (entry.stretcher == nullptr)
        return 0;
    const auto cell = timeline / kStretchCellSamples * kStretchCellSamples;
    const auto readFrom = std::llround(static_cast<double>(cell) * rig.playback.speed) +
                          entry.stretcher->readAheadSamples();
    return std::clamp<std::int64_t>(readFrom, 0, entry.preRollSamples);
}

std::string summary(Rig& rig, const Damage& damage) {
    const auto entry = rig.entry();
    return "lost " + std::to_string(entry.stream->missingFrames(ReadPurpose::playback)) +
           ", priming lost " + std::to_string(entry.stream->missingFrames(ReadPurpose::priming)) +
           ", short reads " + std::to_string(entry.stream->underruns()) + ", preRoll " +
           std::to_string(entry.preRollSamples) + ", first audible " +
           std::to_string(damage.firstAudible) + ", silent " +
           std::to_string(damage.silentWhereAudible) + ", zeros " +
           std::to_string(damage.zeroWhereAudible) + ", last difference " +
           std::to_string(damage.lastDifference) + ", last envelope difference " +
           std::to_string(damage.lastEnvelopeDifference);
}

/// Output samples a stretcher takes to flush a gap once input is back: its priming
/// window, and four envelope windows for the measure itself. Measured at 6.1k to
/// 9.2k output samples at 0.8x and 1.2x.
std::int64_t flushSamples(const Playback& playback, int preRoll) {
    return static_cast<std::int64_t>(std::ceil(preRoll / playback.speed)) + 4 * kEnvelopeWindow;
}

/**
 * @brief Every silent output sample is explained by counted missing frames, and
 *        the render returns to its control once input is back at @p inputReturns.
 *
 * Plain playback shows its position, so it must match exactly from there. The
 * material's level steps never repeat, so a stretched render that matched the
 * control's envelope late would fail this too.
 */
void checkAccounted(Rig& rig, const Damage& damage, std::int64_t inputReturns) {
    INFO(summary(rig, damage));
    const auto& playback = rig.playback;
    const auto entry = rig.entry();
    const auto lost = entry.stream->missingFrames(ReadPurpose::playback);
    const auto primingLost = entry.stream->missingFrames(ReadPurpose::priming);

    CHECK(lost + primingLost > 0);
    if (!playback.stretched()) {
        CHECK(primingLost == 0);
        CHECK(damage.zeroWhereAudible == lost);
        CHECK(damage.lastDifference < inputReturns);
        return;
    }

    const auto lostOutput = static_cast<std::int64_t>(
        std::ceil(static_cast<double>(lost + primingLost) / playback.speed));
    CHECK(damage.silentWhereAudible <= lostOutput + kStretchCellSamples + 2 * kSilenceWindow);

    CHECK(damage.lastEnvelopeDifference <=
          inputReturns + flushSamples(playback, entry.preRollSamples));
}

/// Renders @p heard and @p control from zero, withholding @p heard's worker for @p stall samples.
void renderStall(Rig& heard, Rig& control, std::int64_t warm, std::int64_t stall,
                 std::int64_t after) {
    const auto block = heard.playback.blockSize;
    for (std::int64_t position = 0; position < warm + stall + after; position += block) {
        const auto withheld = position >= warm && position < warm + stall;
        heard.play(position, position > 0, !withheld);
        control.play(position, position > 0, true);
    }
}

/// A pool that holds the priming window and a warm-up's reading at @p playback's rate.
PrefetchSettings poolFor(const Playback& playback) {
    const auto clip = clipFor(playback);
    const auto stretcher = magda::engine::makeStretcher(magda::engine::stretchSetupFor(
        clip, clip.events.front(), RenderContext{kSampleRate, playback.blockSize, 2}));
    REQUIRE(stretcher != nullptr);
    const auto needed = stretcher->preRollSamples(playback.speed) + stretcher->readAheadSamples() +
                        mostReading(playback, 4096);
    return PrefetchSettings{2048, static_cast<int>(needed / 2048) + 3};
}

void checkCoveredStall(const Playback& playback, PrefetchSettings settings) {
    INFO(describe(playback));
    const auto block = playback.blockSize;

    // A round refills every chunk, and the one being read may be nearly spent.
    const auto resident = coverageOf(playback, settings) - std::max(settings.chunkSamples, block) -
                          mostReading(playback, block);
    std::int64_t stall = 0;
    while (mostReading(playback, stall + block) <= resident)
        stall += block;
    REQUIRE(stall > 0);

    Rig heard(playback, Section::Arrangement, settings);
    Rig control(playback, Section::Arrangement, settings);
    for (auto* rig : {&heard, &control}) {
        rig->arrange(clipFor(playback));
        rig->pool.fillNow();
    }

    const auto warm = roundUp(8192, block);
    const auto after = roundUp(16384, block);
    renderStall(heard, control, warm, stall, after);

    CHECK(heard.missing(ReadPurpose::playback) == 0);
    CHECK(heard.missing(ReadPurpose::priming) == 0);
    CHECK(heard.entry().stream->underruns() == 0);
    const auto damage = compare(heard.heard, 0, control.heard, 0, warm + stall + after);
    if (playback.mode == mode::kSignalsmith && (playback.speed < 0.5 || playback.speedRamp)) {
        // Below half speed (also crossed by the ramp), upstream Signalsmith's
        // random phases affect both samples and short-window envelopes. Coverage
        // is established by the zero missing-frame counts above. Check actual
        // output separately without treating another random render as ground truth.
        CHECK(std::all_of(heard.heard.begin(), heard.heard.end(),
                          [](float sample) { return std::isfinite(sample); }));
        CHECK(rms(heard.heard, warm + stall, static_cast<int>(after)) > 0.01);
    } else {
        CHECK(damage.worstDifference == 0.0f);
    }
}

void checkExhaustedStall(const Playback& playback, PrefetchSettings settings) {
    INFO(describe(playback));
    const auto block = playback.blockSize;

    // At least 4096 frames past everything the pool can hold.
    std::int64_t stall = 0;
    while (leastReading(playback, stall) < coverageOf(playback, settings) + 4096)
        stall += block;

    Rig heard(playback, Section::Arrangement, settings);
    Rig control(playback, Section::Arrangement, settings);
    for (auto* rig : {&heard, &control}) {
        rig->arrange(clipFor(playback));
        rig->pool.fillNow();
    }

    const auto warm = roundUp(8192, block);
    // At 0.1x the stretcher's recovery window is longer than 16384 output
    // samples. Observe a full window plus a tail before asking whether it resumed.
    const auto after = roundUp(
        std::max<std::int64_t>(16384, flushSamples(playback, heard.entry().preRollSamples) + 8192),
        block);
    renderStall(heard, control, warm, stall, after);

    const auto lost = heard.missing(ReadPurpose::playback);
    CHECK(lost >= leastReading(playback, stall) - coverageOf(playback, settings));
    CHECK(lost <= mostReading(playback, stall));
    CHECK(heard.missing(ReadPurpose::priming) == 0);
    if (playback.mode == mode::kSignalsmith && playback.speed < 0.5) {
        // The missing-frame bounds still apply to randomized stretching. Require
        // resumed, finite output after the gap; exact recovery against a separate
        // randomized instance is not a defined property of the stock engine.
        CHECK(std::all_of(heard.heard.begin(), heard.heard.end(),
                          [](float sample) { return std::isfinite(sample); }));
        CHECK(rms(heard.heard, warm + stall + after - 8192, 8192) > 0.01);
    } else {
        checkAccounted(heard, compare(heard.heard, warm, control.heard, warm, stall + after),
                       stall);
    }
}

}  // namespace

TEST_CASE("A render that is merely late fails the recovery measure",
          "[engine][clip][streaming][2700]") {
    // The bound checkAccounted applies means nothing unless a shifted render breaks it.
    for (const auto& playback :
         {Playback{mode::kSignalsmith, 1.2, 128}, Playback{mode::kSoundTouchNormal, 0.8, 128}}) {
        Rig rig(playback);
        rig.arrange(clipFor(playback));
        rig.pool.fillNow();
        rig.playOn(0, 49152);

        for (const std::int64_t late : {441, 4410}) {
            INFO(describe(playback) << ", late by " << late);
            const auto damage = compare(rig.heard, late, rig.heard, 0, 32768);
            CHECK(damage.lastEnvelopeDifference > 16384);
        }
    }
}

TEST_CASE("A stall the resident reading covers leaves playback untouched",
          "[engine][clip][streaming][2700]") {
    for (const auto& playback : playbackMatrix())
        checkCoveredStall(playback, kPool);
}

TEST_CASE("A stall past the resident reading is counted, and playback returns to the timeline",
          "[engine][clip][streaming][2700]") {
    for (const auto& playback : playbackMatrix())
        checkExhaustedStall(playback, kPool);
}

TEST_CASE("Stalls at the stretch rate limits follow the same coverage",
          "[engine][clip][streaming][2700]") {
    for (const auto stretch : {mode::kSoundTouchNormal, mode::kSignalsmith})
        for (const auto speed : {magda::engine::kMinStretchRate, magda::engine::kMaxStretchRate}) {
            const Playback playback{stretch, speed, 128};
            const auto settings = poolFor(playback);
            checkCoveredStall(playback, settings);
            checkExhaustedStall(playback, settings);
        }
}

TEST_CASE("A stall during a speed ramp that resident reading covers leaves playback untouched",
          "[engine][clip][streaming][2700]") {
    for (const auto& playback : {Playback{mode::kDisabled, 1.0, 128, true},
                                 Playback{mode::kSoundTouchNormal, 1.2, 128, true},
                                 Playback{mode::kSignalsmith, 0.8, 128, true}})
        checkCoveredStall(playback, kPool);
}

TEST_CASE("Catching up after a long stall does not re-read the stall",
          "[engine][clip][streaming][2704]") {
    // The reader fills forward from where it last read, and after a stall the
    // callback is a long way past that. Every chunk it then comes back with is
    // behind the cursor and thrown away, so a gap used to cost as much reading
    // again before anything audible arrived -- the stall twice over. The stream
    // now tells the reader where the callback actually got to.
    for (const auto& playback :
         {Playback{mode::kDisabled, 1.0, 128}, Playback{mode::kSoundTouchNormal, 1.2, 128},
          Playback{mode::kSignalsmith, 0.8, 128}}) {
        INFO(describe(playback));
        const auto block = playback.blockSize;
        const auto coverage = coverageOf(playback, kPool);

        // Four pool windows, so catch-up proportional to the stall is four times
        // anything that can be in flight when the callback says where it is.
        std::int64_t stall = 0;
        while (leastReading(playback, stall) < 4 * coverage)
            stall += block;

        Rig heard(playback, Section::Arrangement, kPool);
        Rig control(playback, Section::Arrangement, kPool);
        for (auto* rig : {&heard, &control}) {
            rig->arrange(clipFor(playback));
            rig->pool.fillNow();
        }

        const auto warm = roundUp(8192, block);
        const auto after =
            roundUp(std::max<std::int64_t>(
                        16384, flushSamples(playback, heard.entry().preRollSamples) + 8192),
                    block);
        renderStall(heard, control, warm, stall, after);

        const auto obsolete = heard.entry().stream->obsoleteFrames();
        INFO("obsolete " << obsolete << ", stall reading " << leastReading(playback, stall));
        CHECK(obsolete <= coverage);
        CHECK(obsolete < leastReading(playback, stall));

        // And it comes back where it left off rather than where it ran dry.
        checkAccounted(heard, compare(heard.heard, warm, control.heard, warm, stall + after),
                       stall);
    }
}

TEST_CASE("A stall across the end of the file counts only frames the file has",
          "[engine][clip][streaming][2700]") {
    constexpr std::int64_t kLength = 60000;

    for (const auto& playback :
         {Playback{mode::kDisabled, 1.0, 128}, Playback{mode::kSoundTouchNormal, 1.2, 128}}) {
        INFO(describe(playback));
        Rig heard(playback);
        heard.files.length = kLength;
        heard.arrange(clipFor(playback));
        heard.pool.fillNow();

        const auto warm = heard.playOn(0, 8192);
        const auto resident = heard.files.furthest.load();
        REQUIRE(resident < kLength);

        auto position = warm;
        for (; position < warm + 65536; position += playback.blockSize)
            heard.play(position, true, false);
        heard.playOn(position, 8192);

        CHECK(heard.missing(ReadPurpose::playback) == kLength - resident);
        CHECK(heard.missing(ReadPurpose::priming) == 0);
    }
}

TEST_CASE("Play after a stopped locate is complete once the worker has had a round",
          "[engine][clip][streaming][2697][2700]") {
    for (const auto& playback : playbackMatrix()) {
        // On a stretch cell boundary, and inside one.
        for (const std::int64_t target : {44160, 44160 + 65}) {
            INFO(describe(playback) << ", target " << target);
            const auto block = playback.blockSize;
            const auto warm = roundUp(8192, block);
            const auto after = roundUp(16384, block);

            Rig prepared(playback);
            Rig unprepared(playback);
            for (auto* rig : {&prepared, &unprepared}) {
                rig->arrange(clipFor(playback));
                rig->pool.fillNow();
                rig->playOn(0, warm);
            }

            prepared.preparedLocate(target);
            unprepared.stopped(target, true);
            unprepared.play(target, false, false);
            for (auto* rig : {&prepared, &unprepared})
                rig->playOn(target + block, after);

            CHECK(prepared.missing(ReadPurpose::playback) == 0);
            CHECK(prepared.missing(ReadPurpose::priming) == 0);
            if (!playback.stretched())
                CHECK(prepared.heard[static_cast<std::size_t>(warm)] ==
                      static_cast<float>(target + 1));

            // No round before Play: every priming frame and the first callback's reading.
            CHECK(unprepared.missing(ReadPurpose::priming) == primingFramesAt(unprepared, target));
            CHECK(unprepared.missing(ReadPurpose::playback) <= mostReading(playback, block));
            checkAccounted(unprepared,
                           compare(unprepared.heard, warm, prepared.heard, warm, block + after),
                           block);
        }
    }
}

TEST_CASE("A playing locate is lost until the reader reaches it, then recovers",
          "[engine][clip][streaming][2700]") {
    // No preparation policy exists for an arbitrary locate, so none is promised.
    for (const auto& playback : playbackMatrix()) {
        for (const std::int64_t target : {88200 + 65, 4096 + 37}) {
            INFO(describe(playback) << ", target " << target);
            const auto block = playback.blockSize;
            const auto warm = roundUp(12288, block);
            const auto after = roundUp(16384, block);

            Rig heard(playback);
            Rig control(playback);
            for (auto* rig : {&heard, &control}) {
                rig->arrange(clipFor(playback));
                rig->pool.fillNow();
                rig->playOn(0, warm);
            }

            heard.play(target, false, true);
            control.preparedLocate(target);
            for (auto* rig : {&heard, &control})
                rig->playOn(target + block, after);

            CHECK(heard.missing(ReadPurpose::priming) == primingFramesAt(heard, target));
            CHECK(heard.missing(ReadPurpose::playback) <= mostReading(playback, block));
            checkAccounted(heard, compare(heard.heard, warm, control.heard, warm, block + after),
                           block);
        }
    }
}

/// The first locate at or after @p from, @p offset samples into a cell, whose
/// whole reading the pool already holds: priming window, lookahead and a callback.
std::int64_t residentLocate(Rig& rig, std::int64_t from, std::int64_t offset) {
    const auto& playback = rig.playback;
    const auto entry = rig.entry();
    const auto ahead = entry.stretcher != nullptr ? entry.stretcher->readAheadSamples() : 0;

    // The most of the file playback can have consumed, and so the earliest frame
    // the pool can still be holding.
    const auto consumed = mostReading(playback, from) + ahead;
    const auto resident = rig.files.furthest.load();

    const auto sourceAt = [&](std::int64_t timeline) {
        return std::llround(static_cast<double>(timeline) * playback.speed) + ahead;
    };

    for (auto target = from + playback.blockSize + offset;; target += playback.blockSize) {
        const auto cell = target / kStretchCellSamples * kStretchCellSamples;
        const auto primed = sourceAt(playback.stretched() ? cell : target) - entry.preRollSamples;
        REQUIRE(sourceAt(target) + mostReading(playback, playback.blockSize) <= resident);
        if (primed >= consumed)
            return target;
    }
}

TEST_CASE("A playing locate the pool already holds plays complete",
          "[engine][clip][streaming][2701]") {
    for (const auto& playback : playbackMatrix()) {
        const auto settings = playback.stretched() ? poolFor(playback) : kPool;

        // On a stretch cell boundary, and inside one.
        for (const std::int64_t offset : {0, 37}) {
            INFO(describe(playback) << ", offset " << offset);
            const auto block = playback.blockSize;
            const auto warm = roundUp(8192, block);
            const auto after = roundUp(16384, block);

            Rig heard(playback, Section::Arrangement, settings);
            Rig control(playback, Section::Arrangement, settings);
            for (auto* rig : {&heard, &control}) {
                rig->arrange(clipFor(playback));
                rig->pool.fillNow();
                rig->playOn(0, warm);
            }

            const auto target = residentLocate(heard, warm, offset);
            INFO("target " << target);

            // The locate itself gets no worker round; the control's has one and
            // is otherwise the same callback, so the two renders have to agree.
            heard.play(target, false, false);
            control.play(target, false, true);
            for (auto* rig : {&heard, &control})
                rig->playOn(target + block, after);

            CHECK(heard.missing(ReadPurpose::playback) == 0);
            CHECK(heard.missing(ReadPurpose::priming) == 0);
            CHECK(heard.entry().stream->underruns() == 0);
            CHECK(compare(heard.heard, warm, control.heard, warm, block + after).worstDifference ==
                  0.0f);
        }
    }
}

constexpr std::int64_t kLoopStart = 4410 + 37;
constexpr std::int64_t kLoopEnd = 22050 + 11;

/// Plays @p rig from zero through one arrangement loop wrap inside a callback; returns the head.
int playThroughWrap(Rig& rig, std::int64_t after) {
    const auto block = rig.playback.blockSize;
    std::int64_t position = 0;
    for (; position + block <= kLoopEnd; position += block)
        rig.play(position, position > 0, true);

    const auto head = static_cast<int>(kLoopEnd - position);
    rig.callback({{position, head, true}, {kLoopStart, block - head, false}}, true);
    rig.playOn(kLoopStart + block - head, after);
    return head;
}

TEST_CASE(
    "An unprepared arrangement loop wrap keeps its tail and loses the top until the reader returns",
    "[engine][clip][streaming][2700]") {
    for (const auto& playback : playbackMatrix()) {
        INFO(describe(playback));
        const auto block = playback.blockSize;
        const auto after = roundUp(16384, block);

        Rig heard(playback);
        Rig straight(playback);
        Rig incoming(playback);
        for (auto* rig : {&heard, &straight, &incoming}) {
            rig->arrange(clipFor(playback));
            rig->pool.fillNow();
        }

        const auto head = playThroughWrap(heard, after);
        straight.playOn(0, kLoopEnd + block);
        incoming.preparedLocate(kLoopStart);
        incoming.playOn(kLoopStart + block, after);

        CHECK(compare(heard.heard, 0, straight.heard, 0, kLoopEnd).worstDifference == 0.0f);

        CHECK(heard.missing(ReadPurpose::priming) == primingFramesAt(heard, kLoopStart));
        if (!playback.stretched())
            CHECK(heard.missing(ReadPurpose::playback) == block - head);
        checkAccounted(heard, compare(heard.heard, kLoopEnd, incoming.heard, 0, after),
                       block - head);
    }
}

TEST_CASE("An arrangement loop wrap plays its first block complete",
          "[engine][clip][streaming][2700]") {
    for (const auto blockSize : {64, 128, 512}) {
        const Playback playback{mode::kDisabled, 1.0, blockSize};
        Rig rig(playback);
        rig.prepareLoop(kLoopStart, kLoopEnd);
        rig.arrange(clipFor(playback));
        rig.pool.fillNow();
        playThroughWrap(rig, roundUp(4096, blockSize));
        CHECK(rig.missing(ReadPurpose::playback) == 0);

        // The retained destination remains reusable after it has handed off to
        // the ordinary stream. Exercise two more transport folds without a
        // preparation round at either boundary.
        auto position = kLoopStart + blockSize - (kLoopEnd % blockSize) + roundUp(4096, blockSize);
        for (auto repeat = 0; repeat < 2; ++repeat) {
            while (position + blockSize <= kLoopEnd) {
                rig.play(position, true, true);
                position += blockSize;
            }
            const auto tail = static_cast<int>(kLoopEnd - position);
            rig.callback({{position, tail, true}, {kLoopStart, blockSize - tail, false}}, true);
            position = kLoopStart + blockSize - tail;
        }
        CHECK(rig.missing(ReadPurpose::playback) == 0);
    }
}

TEST_CASE("A prepared arrangement loop retains its destination while the reader is elsewhere",
          "[engine][clip][streaming][loop][2700]") {
    std::vector<Playback> cases;
    for (const auto blockSize : {64, 128, 512}) {
        cases.push_back({mode::kDisabled, 1.0, blockSize});
        cases.push_back({mode::kSignalsmith, 1.0, blockSize});
        cases.push_back({mode::kSignalsmith, 120.0 / 175.0, blockSize});
    }
    cases.push_back({mode::kSoundTouchNormal, 0.8, 512});
    cases.push_back({mode::kSoundTouchBetter, 1.2, 512});

    for (const auto& playback : cases) {
        INFO(describe(playback));
        Rig rig(playback);
        Rig straight(playback);
        Rig incoming(playback);
        rig.prepareLoop(kLoopStart, kLoopEnd);
        for (auto* prepared : {&rig, &straight, &incoming}) {
            prepared->arrange(clipFor(playback));
            prepared->pool.fillNow();
        }

        const auto after = roundUp(4096, playback.blockSize);
        playThroughWrap(rig, after);
        straight.playOn(0, kLoopEnd + playback.blockSize);
        incoming.preparedLocate(kLoopStart);
        incoming.playOn(kLoopStart + playback.blockSize, after);

        CHECK(rig.missing(ReadPurpose::playback) == 0);
        CHECK(rig.missing(ReadPurpose::priming) == 0);
        CHECK(compare(rig.heard, 0, straight.heard, 0, kLoopEnd).worstDifference == 0.0f);
        const auto incomingDamage = compare(rig.heard, kLoopEnd, incoming.heard, 0, after);
        INFO(summary(rig, incomingDamage));
        CHECK(incomingDamage.worstDifference == 0.0f);
        CHECK(incomingDamage.zeroWhereAudible == 0);
        CHECK(incomingDamage.silentWhereAudible == 0);
    }
}

TEST_CASE("A short clip at the loop destination keeps its prepared reader",
          "[engine][clip][streaming][loop][2700]") {
    const Playback playback{mode::kDisabled, 1.0, 128};
    auto clip = clipFor(playback);
    const auto starts = static_cast<double>(kLoopStart) / kSampleRate;
    clip.span = spanOf(starts, starts + 0.25);
    clip.events.front().span = clip.span;

    Rig rig(playback);
    rig.prepareLoop(kLoopStart, kLoopEnd);
    rig.arrange(clip);
    rig.pool.fillNow();

    // The moving arrangement window is now at the far end, where this short
    // clip would ordinarily have handed its only reader back.
    rig.pool.setPosition(static_cast<double>(kLoopEnd) / kSampleRate);
    rig.pool.service();
    CHECK(rig.pool.streamCount() == 1);

    const auto position = kLoopEnd - 37;
    rig.callback({{position, 37, true}, {kLoopStart, playback.blockSize - 37, false}}, false);
    CHECK(rig.missing(ReadPurpose::playback) == 0);
}

constexpr std::int64_t kLaunch = 8192;
constexpr double kSlotBeats = 4.0;
constexpr double kWrapBeats = 1.0;
constexpr std::int64_t kWrapSamples = 22050;

/// Where the retained opening runs out, from what startAt read before any round.
struct Retained {
    /// Output samples the opening certainly covers, and where it certainly does not.
    std::int64_t within = 0;
    std::int64_t beyond = 0;
};

Retained retainedOutput(Rig& rig) {
    const auto entry = rig.entry();
    const auto ahead = entry.stretcher != nullptr ? entry.stretcher->readAheadSamples() : 0;
    const auto resident = rig.files.furthest.load() - ahead;
    const auto block = rig.playback.blockSize;

    Retained retained;
    while (mostReading(rig.playback, retained.within + block) <= resident)
        retained.within += block;
    for (retained.beyond = retained.within; leastReading(rig.playback, retained.beyond) <= resident;
         retained.beyond += block) {
    }
    return retained;
}

/// Launches @p rig's slot at kLaunch, @p inside the callback or on its first sample.
void launchAt(Rig& rig, bool inside, bool fill) {
    const auto block = rig.playback.blockSize;
    for (std::int64_t position = 0; position < kLaunch; position += block)
        rig.play(position, position > 0, fill);

    const auto beat = static_cast<double>(kLaunch + 37) / kSampleRate / kSecondsPerBeat;
    rig.handle().play(inside ? std::optional<double>(beat) : std::nullopt);
    rig.play(kLaunch, true, fill);
}

TEST_CASE("A session launch plays its opening whether the worker has filled or not",
          "[engine][clip][streaming][session][2698][2700]") {
    for (const auto& playback : playbackMatrix()) {
        for (const auto inside : {false, true}) {
            INFO(describe(playback) << (inside ? ", inside a callback" : ", on a callback"));
            const auto block = playback.blockSize;
            const auto launchSample = kLaunch + (inside ? 37 : 0);

            Rig filled(playback, Section::Session);
            Rig paused(playback, Section::Session);
            for (auto* rig : {&filled, &paused})
                rig->slot(clipFor(playback, 2.0), kSlotBeats);
            filled.pool.fillNow();

            // Half the retained opening with no round at all, then the worker back.
            const auto silentFor = retainedOutput(paused).within / 2 / block * block;
            launchAt(filled, inside, true);
            launchAt(paused, inside, false);
            for (std::int64_t done = block; done < silentFor; done += block)
                paused.play(kLaunch + done, true, false);
            filled.playOn(kLaunch + block, silentFor);
            for (auto* rig : {&filled, &paused})
                rig->playOn(kLaunch + silentFor, roundUp(8192, block));

            for (auto* rig : {&filled, &paused}) {
                CHECK(rig->missing(ReadPurpose::playback) == 0);
                CHECK(rig->missing(ReadPurpose::priming) == 0);
                CHECK(rig->entry().stream->underruns() == 0);
            }

            if (!playback.stretched()) {
                CHECK(filled.heard[static_cast<std::size_t>(launchSample - 1)] == 0.0f);
                CHECK(filled.heard[static_cast<std::size_t>(launchSample)] == 1.0f);
            }

            const auto length = std::ssize(paused.heard) - launchSample;
            CHECK(compare(paused.heard, launchSample, filled.heard, launchSample, length)
                      .worstDifference == 0.0f);
        }
    }
}

TEST_CASE("A session slot hands its retained opening over to the reader",
          "[engine][clip][streaming][session][2698][2700]") {
    for (const auto& playback : playbackMatrix()) {
        const auto block = playback.blockSize;
        const auto after = roundUp(16384, block);

        // What the opening covers, before any round has added to it.
        Rig probe(playback, Section::Session);
        probe.slot(clipFor(playback, 2.0), kSlotBeats);
        const auto covered = retainedOutput(probe);

        Rig control(playback, Section::Session);
        control.slot(clipFor(playback, 2.0), kSlotBeats);
        control.pool.fillNow();
        launchAt(control, false, true);
        control.playOn(kLaunch + block, covered.beyond + after + 8 * block);

        // The worker back before the retained opening runs out, at the last
        // callback it covers, and once playback is past it.
        for (const auto resume : {covered.within - block, covered.within, covered.beyond + block}) {
            INFO(describe(playback) << ", worker back at " << resume << " of " << covered.within
                                    << " to " << covered.beyond);
            Rig heard(playback, Section::Session);
            heard.slot(clipFor(playback, 2.0), kSlotBeats);
            launchAt(heard, false, false);

            for (std::int64_t done = block; done < resume; done += block)
                heard.play(kLaunch + done, true, false);
            heard.playOn(kLaunch + resume, after);

            const auto damage =
                compare(heard.heard, kLaunch, control.heard, kLaunch, resume + after);
            INFO(summary(heard, damage));

            if (resume <= covered.within) {
                CHECK(heard.missing(ReadPurpose::playback) == 0);
                CHECK(heard.missing(ReadPurpose::priming) == 0);
                CHECK(damage.worstDifference == 0.0f);
            } else {
                checkAccounted(heard, damage, resume);
            }
        }
    }
}

TEST_CASE("Several launched slots hand over from their own retained openings",
          "[engine][clip][streaming][session][2698][2700]") {
    constexpr int kSlots = 3;

    for (const auto& playback :
         {Playback{mode::kDisabled, 1.0, 128}, Playback{mode::kSoundTouchNormal, 1.2, 128},
          Playback{mode::kSignalsmith, 0.8, 128}}) {
        INFO(describe(playback));
        const auto block = playback.blockSize;

        Rig rig(playback, Section::Session, kPool, nullptr, kSlots);
        rig.slot(clipFor(playback, 2.0), kSlotBeats);

        const auto covered = retainedOutput(rig);
        for (auto scene = 0; scene < kSlots; ++scene)
            rig.handle(scene).play(std::nullopt);

        // Every slot plays its own opening while the worker is still paused.
        for (std::int64_t done = 0; done < covered.within; done += block)
            rig.play(kLaunch + done, done > 0, false);

        for (auto slot = 0; slot < kSlots; ++slot) {
            INFO("slot " << slot);
            CHECK(rig.missing(ReadPurpose::playback, slot) == 0);
            CHECK(rig.missing(ReadPurpose::priming, slot) == 0);
        }

        // And past it every slot is counted, then back together.
        rig.playOn(kLaunch + covered.beyond, roundUp(16384, block));
        for (auto slot = 0; slot < kSlots; ++slot) {
            INFO("slot " << slot);
            CHECK(rig.missing(ReadPurpose::playback, slot) ==
                  rig.missing(ReadPurpose::playback, 0));
            CHECK(rig.entry(slot).stream->underruns() == rig.entry(0).stream->underruns());
        }
    }
}

/// A slot launched at kLaunch with the worker keeping up, re-triggered every kWrapBeats.
struct LoopingSlot {
    explicit LoopingSlot(const Playback& playback, int wraps) : rig(playback, Section::Session) {
        rig.slot(clipFor(playback, 2.0), kSlotBeats);
        rig.pool.fillNow();
        rig.playOn(0, kLaunch);
        rig.handle().setLooping(kWrapBeats);
        rig.handle().play(std::nullopt);
        rig.playOn(kLaunch, kWrapSamples * wraps + roundUp(16384, playback.blockSize));
    }

    Rig rig;
};

TEST_CASE("A session wrap plays its top from the retained opening, every pass",
          "[engine][clip][streaming][session][2698][2700]") {
    constexpr int kWraps = 3;

    for (const auto& playback : playbackMatrix()) {
        INFO(describe(playback));
        LoopingSlot looping(playback, kWraps);
        auto& rig = looping.rig;

        CHECK(rig.missing(ReadPurpose::playback) == 0);
        CHECK(rig.missing(ReadPurpose::priming) == 0);

        for (auto wrap = 1; wrap <= kWraps; ++wrap) {
            INFO("wrap " << wrap);
            const auto at = kLaunch + wrap * kWrapSamples;

            // The first pass is the control: it ran from a filled pool.
            const auto damage = compare(rig.heard, at, rig.heard, kLaunch,
                                        std::min<std::int64_t>(kWrapSamples, 16384));
            INFO(summary(rig, damage));
            CHECK(damage.zeroWhereAudible == 0);
            CHECK(damage.silentWhereAudible == 0);
        }
    }
}

TEST_CASE("A session re-trigger plays its top from the retained opening",
          "[engine][clip][streaming][session][2698][2700]") {
    for (const auto& playback : playbackMatrix()) {
        INFO(describe(playback));
        const auto block = playback.blockSize;

        Rig rig(playback, Section::Session);
        rig.slot(clipFor(playback, 2.0), kSlotBeats);
        rig.pool.fillNow();
        rig.playOn(0, kLaunch);
        rig.handle().play(std::nullopt);
        const auto position = rig.playOn(kLaunch, roundUp(16384, block));

        rig.handle().play(std::nullopt);
        rig.playOn(position, roundUp(8192, block));

        CHECK(rig.missing(ReadPurpose::playback) == 0);
        CHECK(rig.missing(ReadPurpose::priming) == 0);
        const auto damage = compare(rig.heard, position - kLaunch, rig.heard, 0,
                                    std::ssize(rig.heard) - (position - kLaunch));
        INFO(summary(rig, damage));
        CHECK(damage.silentWhereAudible == 0);
    }
}

TEST_CASE("A prime the reader missed is not made good by priming again",
          "[engine][clip][streaming][2703]") {
    // Priming reads behind the position it aligns to, so a voice that primed
    // again whenever it came up short would send a reader that is already late
    // further back still, and the audio it recovered would arrive after the
    // moment it belonged to. The loss is reported and the position is kept.
    constexpr PrefetchSettings kSmallChunks{256, 64};
    constexpr std::int64_t kTarget = 44160;

    for (const auto stretch :
         {mode::kSoundTouchNormal, mode::kSoundTouchBetter, mode::kSignalsmith})
        for (const auto speed : {0.8, 1.2}) {
            const Playback playback{stretch, speed, 128};
            INFO(describe(playback));
            const auto after = roundUp(16384, playback.blockSize);

            ReaderGate gate;
            Rig heard(playback, Section::Arrangement, kSmallChunks, &gate);
            Rig control(playback, Section::Arrangement, kSmallChunks);
            for (auto* rig : {&heard, &control}) {
                rig->arrange(clipFor(playback));
                rig->pool.fillNow();
                rig->playOn(0, 8192);
            }

            // Held before it fills anything, so the prime aligns against silence.
            heard.stopped(kTarget, true);
            const auto preRoll = heard.entry().preRollSamples;
            gate.closeAfter(0);
            {
                magda::test::GatedWorker worker(gate, [&] { heard.reader.fillOnce(); });
                REQUIRE(gate.waitUntilHeld());
                heard.play(kTarget, false, false);
            }

            const auto primingLost = heard.missing(ReadPurpose::priming);
            CHECK(primingLost > 0);
            CHECK(primingLost <= preRoll);

            control.preparedLocate(kTarget);
            for (auto* rig : {&heard, &control})
                rig->playOn(kTarget + playback.blockSize, after);

            // The reader was back for the whole of that, and nothing went back
            // for what it had missed: one prime, and one window of loss.
            CHECK(heard.missing(ReadPurpose::priming) == primingLost);

            const auto from = roundUp(8192, playback.blockSize);
            checkAccounted(
                heard, compare(heard.heard, from, control.heard, from, playback.blockSize + after),
                playback.blockSize);
        }
}

TEST_CASE("A prime the reader half supplied is counted apart from playback, and recovers",
          "[engine][clip][streaming][2700]") {
    constexpr PrefetchSettings kSmallChunks{256, 64};
    constexpr std::int64_t kTarget = 44160;

    for (const auto stretch :
         {mode::kSoundTouchNormal, mode::kSoundTouchBetter, mode::kSignalsmith})
        for (const auto speed : {0.8, 1.2}) {
            const Playback playback{stretch, speed, 128};
            INFO(describe(playback));
            const auto after = roundUp(16384, playback.blockSize);

            ReaderGate gate;
            Rig heard(playback, Section::Arrangement, kSmallChunks, &gate);
            Rig control(playback, Section::Arrangement, kSmallChunks);
            for (auto* rig : {&heard, &control}) {
                rig->arrange(clipFor(playback));
                rig->pool.fillNow();
                rig->playOn(0, 8192);
            }

            heard.stopped(kTarget, true);
            const auto preRoll = heard.entry().preRollSamples;
            const auto chunks = std::max(1, preRoll / 2 / kSmallChunks.chunkSamples);
            gate.closeAfter(chunks);
            {
                magda::test::GatedWorker worker(gate, [&] { heard.reader.fillOnce(); });
                REQUIRE(gate.waitUntilHeld());
                heard.play(kTarget, false, false);
            }

            control.preparedLocate(kTarget);
            for (auto* rig : {&heard, &control})
                rig->playOn(kTarget + playback.blockSize, after);

            CHECK(heard.missing(ReadPurpose::priming) ==
                  preRoll - chunks * kSmallChunks.chunkSamples);
            CHECK(heard.missing(ReadPurpose::playback) > 0);
            CHECK(heard.missing(ReadPurpose::playback) <=
                  mostReading(playback, playback.blockSize));

            // Finding 4: the source reports no starved voice for any of it.
            CHECK(heard.source->starvedVoices() == 0);

            const auto from = roundUp(8192, playback.blockSize);
            checkAccounted(
                heard, compare(heard.heard, from, control.heard, from, playback.blockSize + after),
                playback.blockSize);
        }
}
