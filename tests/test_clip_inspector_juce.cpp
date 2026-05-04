#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

#define private public
#include "magda/daw/ui/panels/content/inspector/ClipInspector.hpp"
#undef private

using namespace magda;
using magda::daw::ui::ClipInspector;

namespace {
constexpr double projectBPM = 120.0;
constexpr double sourceBPM = 172.0;
constexpr double sourceBeats = 16.0;
constexpr double sourceDuration = sourceBeats * 60.0 / sourceBPM;

ClipInfo makeInspectorAudioClip(ClipId id = 9001) {
    ClipInfo clip;
    clip.id = id;
    clip.trackId = 1;
    clip.setAudioContent();
    clip.view = ClipView::Session;
    clip.name = "InspectorTest";
    clip.autoTempo = true;
    clip.loopEnabled = true;
    clip.speedRatio = 1.0;
    clip.audio().source.durationSeconds = sourceDuration;
    clip.audio().interpretation.bpm = sourceBPM;
    clip.audio().interpretation.totalBeats = sourceBeats;
    clip.setPlacementBeats(0.0, sourceBeats);
    clip.length = sourceBeats * 60.0 / projectBPM;
    clip.loopStart = 0.0;
    clip.loopStartBeats = 0.0;
    clip.loopLength = sourceDuration;
    clip.loopLengthBeats = sourceBeats;
    clip.offset = 0.0;
    clip.offsetBeats = 0.0;
    return clip;
}

void applySourceBeats(ClipId clipId, double beats) {
    ClipManager::AudioClipBeatsUpdate update;
    update.interpretationTotalBeats = beats;
    update.interpretationBpm = beats * 60.0 / sourceDuration;
    update.lockInterpretationTotalBeats = true;
    ClipManager::getInstance().applyAudioClipBeats(clipId, update, projectBPM);
}

void expectLoopEnd(juce::UnitTest& test, const ClipInspector& inspector, double expected) {
    test.expect(inspector.clipLoopEndValue_ != nullptr, "Loop end widget should exist");
    if (inspector.clipLoopEndValue_ != nullptr) {
        test.expectWithinAbsoluteError(inspector.clipLoopEndValue_->getValue(), expected, 0.001,
                                       "Inspector loop end should match source beats");
    }
}
}  // namespace

class ClipInspectorJuceTest final : public juce::UnitTest {
  public:
    ClipInspectorJuceTest() : juce::UnitTest("ClipInspector JUCE Tests", "magda") {}

    void runTest() override {
        testFullRefreshTracksSourceBeatEdits();
        testMidDragPropertyChangeRefreshesLoopEnd();
    }

  private:
    void testFullRefreshTracksSourceBeatEdits() {
        beginTest("Loop end follows source Beats after full inspector refresh");
        ClipManager::getInstance().clearAllClips();

        auto seed = makeInspectorAudioClip();
        ClipManager::getInstance().restoreClip(seed);

        ClipInspector inspector;
        inspector.setBounds(0, 0, 360, 640);
        inspector.setSelectedClip(seed.id);
        expectLoopEnd(*this, inspector, 16.0);

        applySourceBeats(seed.id, 12.0);
        inspector.clipPropertyChanged(seed.id);
        expectLoopEnd(*this, inspector, 12.0);

        applySourceBeats(seed.id, 8.0);
        inspector.clipPropertyChanged(seed.id);
        expectLoopEnd(*this, inspector, 8.0);

        ClipManager::getInstance().clearAllClips();
    }

    void testMidDragPropertyChangeRefreshesLoopEnd() {
        beginTest("Loop end follows source Beats during Beats drag");
        ClipManager::getInstance().clearAllClips();

        auto seed = makeInspectorAudioClip();
        ClipManager::getInstance().restoreClip(seed);

        ClipInspector inspector;
        inspector.setBounds(0, 0, 360, 640);
        inspector.setSelectedClip(seed.id);
        expectLoopEnd(*this, inspector, 16.0);

        inspector.clipBeatsLengthValue_->isDragging_ = true;

        applySourceBeats(seed.id, 12.0);
        inspector.clipPropertyChanged(seed.id);
        expectLoopEnd(*this, inspector, 12.0);

        applySourceBeats(seed.id, 8.0);
        inspector.clipPropertyChanged(seed.id);
        expectLoopEnd(*this, inspector, 8.0);

        inspector.clipBeatsLengthValue_->isDragging_ = false;
        ClipManager::getInstance().clearAllClips();
    }
};

static ClipInspectorJuceTest clipInspectorJuceTest;
