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

// Fails today: the callback keeps a typed value only when it differs from the
// project tempo, so 120 in a 120 project looks defaulted. Passes after phase 2.
TEST_CASE("A detection that lands on a clip the user already set does not replace it",
          "[clip][tempo][sequence][detection][!mayfail]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();

    ClipManager::AudioClipBeatsUpdate typed;
    typed.interpretationBpm = kProjectBpm;
    ClipManager::getInstance().applyAudioClipBeats(clipId, typed, kProjectBpm);
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

    ClipManager::AudioClipBeatsUpdate typed;
    typed.interpretationBpm = 100.0;
    ClipManager::getInstance().applyAudioClipBeats(clipId, typed, kProjectBpm);

    REQUIRE(fx.pumpUntilAnswered());
    REQUIRE(AudioThumbnailManager::getInstance().getCachedBPM(fx.path) == Approx(kFileBpm));

    REQUIRE(eventOf(clipId)->interpBpm == Approx(100.0));
}

// Fails today: the callback grants beat mode whenever it applies a tempo.
// Passes after phase 2.
TEST_CASE("A detection that lands after the user chose time mode does not switch it on",
          "[clip][tempo][sequence][detection][!mayfail]") {
    DetectionFixture fx;
    const auto clipId = fx.createSessionClip();

    // The clip is already in time mode for want of a tempo; this is the
    // user's gesture saying it should stay there.
    ClipManager::getInstance().setAutoTempo(clipId, false, kProjectBpm);
    REQUIRE_FALSE(eventOf(clipId)->autoTempo);

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
