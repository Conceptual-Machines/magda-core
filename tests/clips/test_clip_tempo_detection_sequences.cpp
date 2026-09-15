// Phase 1 of the clip tempo ownership work (#2674): sequences that define done.
// Detection answers on the message thread, so each case runs a dispatch loop.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <numbers>

#include "magda/daw/audio/AudioThumbnailManager.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SourcePool.hpp"

using namespace magda;
using Catch::Approx;

namespace {

constexpr double kProjectBpm = 120.0;
constexpr double kFileBpm = 128.0;
constexpr int kSampleRate = 44100;
constexpr double kFileSeconds = 2.0;

/// Writes 2 s of a 440 Hz sine to @p file so every tier below the filename
/// has real audio to read.
void writeSineWav(const juce::File& file) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    juce::StringPairArray metadata;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    REQUIRE(stream != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), kSampleRate, 1, 16, metadata, 0));
    REQUIRE(writer != nullptr);
    stream.release();

    const int n = static_cast<int>(kFileSeconds * kSampleRate);
    juce::AudioBuffer<float> buf(1, n);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    for (int i = 0; i < n; ++i)
        buf.setSample(0, i, static_cast<float>(0.5 * std::sin(kTwoPi * 440.0 * i / kSampleRate)));
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, n));
}

/// A message loop, a clean model, and one real file whose name answers the
/// filename tier with 128 BPM. Declared first so the loop outlives the rest.
struct DetectionFixture {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir;
    juce::String path;

    DetectionFixture() {
        reset();
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("magda_tempo_" + juce::Uuid().toString());
        REQUIRE(dir.createDirectory());
        const auto file = dir.getChildFile("loop_128bpm.wav");
        writeSineWav(file);
        path = file.getFullPathName();
    }

    ~DetectionFixture() {
        reset();
        // The detection thread must not outlive the message loop this fixture
        // owns, or it dies in static destruction with a dead mutex.
        AudioThumbnailManager::getInstance().stopBackgroundWork();
        dir.deleteRecursively();
    }

    static void reset() {
        ClipManager::getInstance().clearAllClips();
        AudioThumbnailManager::getInstance().clearCache();
        SourcePool::getInstance().clear();
    }

    ClipId createSessionClip() const {
        return ClipManager::getInstance().createAudioClipBeats(1, 0.0, 4.0, path, ClipView::Session,
                                                               kProjectBpm);
    }

    static void pump(int times = 3) {
        for (int i = 0; i < times; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    static bool pumpUntil(const std::function<bool()>& done) {
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (!done() && juce::Time::getMillisecondCounter() < deadline)
            pump(1);
        return done();
    }

    /// The cache is written in the same message before the callbacks run, so
    /// an entry means the answer has landed; the extra pumps are for anything
    /// the callbacks posted.
    bool pumpUntilAnswered() const {
        const bool answered = pumpUntil(
            [this] { return AudioThumbnailManager::getInstance().getCachedBPM(path) > 0.0; });
        pump();
        return answered;
    }
};

/// Collects the [tempo] log lines detection writes; the only view of a join.
struct LogCapture : juce::Logger {
    LogCapture() {
        juce::Logger::setCurrentLogger(this);
    }
    ~LogCapture() override {
        juce::Logger::setCurrentLogger(nullptr);
    }

    void logMessage(const juce::String& message) override {
        const juce::ScopedLock sl(lock);
        lines.add(message);
    }

    int count(const juce::String& needle) const {
        const juce::ScopedLock sl(lock);
        int n = 0;
        for (const auto& line : lines)
            if (line.contains(needle))
                ++n;
        return n;
    }

    juce::CriticalSection lock;
    juce::StringArray lines;
};

const AudioEvent* eventOf(ClipId id) {
    const auto* clip = ClipManager::getInstance().getClip(id);
    return clip != nullptr ? clip->primaryEvent() : nullptr;
}

}  // namespace

// =============================================================================
// A detection never overrides what the user set
// =============================================================================

// Analysis never overrides a user-owned tempo, even one equal to the project's.
TEST_CASE("A detection that lands on a clip the user already set does not replace it",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();

    ClipManager::getInstance().setSourceTempo(clipId, kProjectBpm, Provenance::User);
    REQUIRE(eventOf(clipId)->interpBpm == Approx(kProjectBpm));

    REQUIRE(fx.pumpUntilAnswered());
    REQUIRE(AudioThumbnailManager::getInstance().getCachedBPM(fx.path) == Approx(kFileBpm));

    REQUIRE(eventOf(clipId)->interpBpm == Approx(kProjectBpm));
}

// The boundary of the heuristic: a typed tempo off the project's survives.
TEST_CASE("A detection that lands on a clip the user set to another tempo does not replace it",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();

    ClipManager::getInstance().setSourceTempo(clipId, 100.0, Provenance::User);

    REQUIRE(fx.pumpUntilAnswered());
    REQUIRE(AudioThumbnailManager::getInstance().getCachedBPM(fx.path) == Approx(kFileBpm));

    REQUIRE(eventOf(clipId)->interpBpm == Approx(100.0));
}

// A detection supplies a tempo; only the user's intent switches beat mode on.
TEST_CASE("A detection that lands after the user chose time mode does not switch it on",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();

    // The clip is already in time mode for want of a tempo; this is the
    // user's gesture saying it should stay there.
    ClipManager::getInstance().setAutoTempo(clipId, false, kProjectBpm);
    REQUIRE_FALSE(eventOf(clipId)->autoTempo);
    REQUIRE(eventOf(clipId)->playbackIntent == PlaybackIntent::Free);

    REQUIRE(fx.pumpUntilAnswered());

    REQUIRE_FALSE(eventOf(clipId)->autoTempo);
}

// =============================================================================
// A detection lands on the clip that asked, or nowhere
// =============================================================================

TEST_CASE("A detection for a clip that is gone lands nowhere",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();
    REQUIRE(AudioThumbnailManager::getInstance().getCachedBPM(fx.path) == 0.0);

    ClipManager::getInstance().deleteClip(clipId);
    REQUIRE(ClipManager::getInstance().getClip(clipId) == nullptr);

    REQUIRE(fx.pumpUntilAnswered());
    REQUIRE(AudioThumbnailManager::getInstance().getCachedBPM(fx.path) == Approx(kFileBpm));

    REQUIRE(ClipManager::getInstance().getClip(clipId) == nullptr);
    REQUIRE(ClipManager::getInstance().getClips().empty());
}

TEST_CASE("A second request for a file in flight joins the first",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    LogCapture log;
    const auto first = fx.createSessionClip();
    const auto second = fx.createSessionClip();

    REQUIRE(fx.pumpUntilAnswered());

    REQUIRE(eventOf(first)->interpBpm == Approx(kFileBpm));
    REQUIRE(eventOf(second)->interpBpm == Approx(kFileBpm));
    REQUIRE(AudioThumbnailManager::getInstance().getCachedBPM(fx.path) == Approx(kFileBpm));

    // One read of the file, and the second clip waited on it.
    REQUIRE(log.count("[tempo] detecting loop_128bpm.wav") == 1);
    REQUIRE(log.count("[tempo] joined the request in flight for loop_128bpm.wav") == 1);
}

TEST_CASE("A detection answering for a file the clip no longer plays lands nowhere",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();
    REQUIRE(eventOf(clipId)->interpBpm == 0.0);

    const auto other = fx.dir.getChildFile("some_other_loop.wav").getFullPathName();
    ClipManager::getInstance().adoptAnalysis(clipId, other, 100.0);

    REQUIRE(eventOf(clipId)->interpBpm == 0.0);
}

// =============================================================================
// Ownership decides whether an answer may land
// =============================================================================

TEST_CASE("A detection is refused over the user's tempo and taken over an earlier analysis",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    auto& clips = ClipManager::getInstance();

    const auto owned = fx.createSessionClip();
    clips.setSourceTempo(owned, 100.0, Provenance::User);
    clips.adoptAnalysis(owned, fx.path, 90.0);
    REQUIRE(eventOf(owned)->interpBpm == Approx(100.0));
    REQUIRE(eventOf(owned)->bpmFrom == Provenance::User);

    const auto analysed = fx.createSessionClip();
    clips.setSourceTempo(analysed, 100.0, Provenance::Analysis);
    clips.adoptAnalysis(analysed, fx.path, 90.0);
    REQUIRE(eventOf(analysed)->interpBpm == Approx(90.0));
    REQUIRE(eventOf(analysed)->bpmFrom == Provenance::Analysis);
}

// =============================================================================
// Beat mode asked for before there is a tempo to grant it
// =============================================================================

TEST_CASE("Beat mode asked for without a tempo waits for the detection",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    auto& clips = ClipManager::getInstance();
    const auto clipId = fx.createSessionClip();

    clips.setPlaybackIntent(clipId, PlaybackIntent::BeatWhenKnown, kProjectBpm);
    REQUIRE(eventOf(clipId)->playbackIntent == PlaybackIntent::BeatWhenKnown);
    REQUIRE_FALSE(eventOf(clipId)->autoTempo);

    clips.adoptAnalysis(clipId, fx.path, kFileBpm);

    REQUIRE(eventOf(clipId)->autoTempo);
    REQUIRE(eventOf(clipId)->loopExtent == RegionExtent::Interpretation);
    REQUIRE(clips.getClip(clipId)->loopEnabled);
}

// =============================================================================
// A beat count is refused with its implied tempo
// =============================================================================

TEST_CASE("A beat count that implies a tempo no file has is refused whole",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    auto& clips = ClipManager::getInstance();
    const auto clipId = fx.createSessionClip();

    clips.setSourceTempo(clipId, kFileBpm, Provenance::Analysis);
    const double beats = eventOf(clipId)->interpTotalBeats;
    REQUIRE(beats > 0.0);

    // 1434 beats over a 2 s file is 43,000 BPM — the typo this guards.
    clips.setSourceBeatCount(clipId, 1434.0);

    REQUIRE(eventOf(clipId)->interpTotalBeats == Approx(beats));
    REQUIRE(eventOf(clipId)->interpBpm == Approx(kFileBpm));
}

// =============================================================================
// Speed moves an explicit range, not one the interpretation sizes
// =============================================================================

TEST_CASE("A speed change leaves a region that follows the interpretation alone",
          "[clip][tempo][sequence][detection]") {
    DetectionFixture fx;
    auto& clips = ClipManager::getInstance();
    const auto clipId = fx.createSessionClip();

    clips.adoptAnalysis(clipId, fx.path, kFileBpm);
    REQUIRE(eventOf(clipId)->loopExtent == RegionExtent::Interpretation);
    const auto regionSamples = eventOf(clipId)->loopLengthSamples;
    REQUIRE(regionSamples > 0);

    clips.setSpeedRatio(clipId, 2.0);

    REQUIRE(eventOf(clipId)->loopExtent == RegionExtent::Interpretation);
    REQUIRE(eventOf(clipId)->loopLengthSamples == regionSamples);
}
