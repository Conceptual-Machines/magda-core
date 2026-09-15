#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

#include "JuceTestStateGuard.hpp"
#include "MediaDbTestHelpers.hpp"
#include "magda/daw/audio/AudioThumbnailManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"

/**
 * BEAT works out what a file is (#2674).
 *
 * The detection pass runs off the message thread, so this needs a real message
 * loop to pump — the model-level rules are covered headless in
 * test_clip_event_model.cpp.
 */

using namespace magda;

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 10000) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (done())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    return done();
}

const magda::AudioEvent* eventOf(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    return clip != nullptr ? clip->primaryEvent() : nullptr;
}

class ClipBeatDetectionJuceTest : public juce::UnitTest {
  public:
    ClipBeatDetectionJuceTest() : juce::UnitTest("Clip beat detection", "clips") {}

    void runTest() override {
        testAnUnscannedFileAnswersOnThePress();
        testAFileNothingCanTellFallsBackToTheProjectTempo();
    }

  private:
    // Nothing is scanned on drop, so a loop dragged in from outside any scanned
    // library has no row and no tier ever ran for it. The press is the signal
    // that this file's tempo is the one the user cares about.
    void testAnUnscannedFileAnswersOnThePress() {
        beginTest("BEAT works out an unscanned file's tempo and enters beat mode");

        magda::test::runWithCleanJuceState([this] {
            magda::test::TempMediaDb mediaDb;
            AudioThumbnailManager::getInstance().clearCache();

            const auto file = magda::test::writeTestWav(mediaDb.dir(), "riff_140bpm.wav");
            const auto trackId = TrackManager::getInstance().createTrack("Audio", TrackType::Media);
            const auto clipId = ClipManager::getInstance().createAudioClipBeats(
                trackId, 0.0, 8.0, file.getFullPathName(), ClipView::Session, 100.0);

            expect(eventOf(clipId) != nullptr, "clip has no audio event");
            expect(!eventOf(clipId)->hasInterpretedBpm(), "an unscanned file told nobody a tempo");
            expect(!eventOf(clipId)->autoTempo, "so the slot should come up in time mode");

            ClipManager::getInstance().setAutoTempo(clipId, true, 100.0);

            expect(pumpUntil([clipId] { return eventOf(clipId)->autoTempo; }),
                   "the detection pass never granted beat mode");
            expect(std::abs(eventOf(clipId)->interpBpm - 140.0) < 0.01,
                   "expected the filename's 140, got " +
                       juce::String(eventOf(clipId)->interpBpm, 2));
        });
    }

    // A pad, a vocal take or a one-shot has no tempo to find. The press still
    // means what pressing it means: play this at my tempo.
    void testAFileNothingCanTellFallsBackToTheProjectTempo() {
        beginTest("BEAT on a file nothing can tell falls back to the project tempo");

        magda::test::runWithCleanJuceState([this] {
            magda::test::TempMediaDb mediaDb;
            AudioThumbnailManager::getInstance().clearCache();

            const auto file = magda::test::writeTestWav(mediaDb.dir(), "vocal_take.wav");
            const auto trackId = TrackManager::getInstance().createTrack("Audio", TrackType::Media);
            const auto clipId = ClipManager::getInstance().createAudioClipBeats(
                trackId, 0.0, 8.0, file.getFullPathName(), ClipView::Session, 100.0);

            expect(!eventOf(clipId)->hasInterpretedBpm(), "nothing should have told a tempo");

            ClipManager::getInstance().setAutoTempo(clipId, true, 100.0);

            expect(pumpUntil([clipId] { return eventOf(clipId)->autoTempo; }),
                   "the press left the clip in time mode with no way forward");
            expect(std::abs(eventOf(clipId)->interpBpm - 100.0) < 0.01,
                   "expected the project's 100, got " +
                       juce::String(eventOf(clipId)->interpBpm, 2));
        });
    }
};

ClipBeatDetectionJuceTest clipBeatDetectionJuceTest;

}  // namespace
