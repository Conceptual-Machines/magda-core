#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

#define private public
#include "AudioClipTestHelpers.hpp"
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
    clip.trackId = INVALID_TRACK_ID;
    clip.setAudioContent();
    clip.view = ClipView::Session;
    clip.name = "InspectorTest";
    magda::test::audioEvent(clip).autoTempo = true;
    clip.loopEnabled = true;
    magda::test::audioEvent(clip).speedRatio = 1.0;
    magda::test::setSourceDuration(clip, sourceDuration);
    magda::test::audioEvent(clip).interpBpm = sourceBPM;
    magda::test::audioEvent(clip).interpTotalBeats = sourceBeats;
    clip.setPlacementBeats(0.0, sourceBeats);
    clip.length = sourceBeats * 60.0 / projectBPM;
    magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
    magda::test::audioEvent(clip).setLoopStartBeats(0.0);
    magda::test::audioEvent(clip).setLoopLengthSeconds(sourceDuration);
    magda::test::audioEvent(clip).setLoopLengthBeats(sourceBeats);
    magda::test::audioEvent(clip).setAnchorSeconds(0.0);
    magda::test::audioEvent(clip).setAnchorBeats(0.0);
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

void expectBpmDisplay(juce::UnitTest& test, const ClipInspector& inspector, double expected) {
    test.expectWithinAbsoluteError(inspector.clipBpmValue_.getText().getDoubleValue(), expected,
                                   0.01, "Inspector BPM should match source interpretation BPM");
}

void expectSourceBeatsDisplay(juce::UnitTest& test, const ClipInspector& inspector,
                              double expected) {
    test.expect(inspector.clipBeatsLengthValue_ != nullptr, "Beats widget should exist");
    if (inspector.clipBeatsLengthValue_ != nullptr) {
        test.expectWithinAbsoluteError(inspector.clipBeatsLengthValue_->getValue(), expected, 0.001,
                                       "Inspector Beats should match source interpretation beats");
    }
}
}  // namespace

class ClipInspectorJuceTest final : public juce::UnitTest {
  public:
    ClipInspectorJuceTest() : juce::UnitTest("ClipInspector JUCE Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testFullRefreshTracksSourceBeatEdits(); });
        magda::test::runWithCleanJuceState([this] { testMidDragPropertyChangeRefreshesLoopEnd(); });
        magda::test::runWithCleanJuceState([this] { testBpmAndBeatsDisplaysRefreshTogether(); });
        magda::test::runWithCleanJuceState(
            [this] { testLoopEndUsesLoopLengthNotPlacementLength(); });
    }

  private:
    void testFullRefreshTracksSourceBeatEdits() {
        beginTest("Loop end follows source Beats after full inspector refresh");

        auto seed = makeInspectorAudioClip();
        ClipManager::getInstance().restoreClip(seed);

        ClipInspector inspector;
        inspector.setBounds(0, 0, 360, 640);
        inspector.setSelectedClip(seed.id);
        expectLoopEnd(*this, inspector, 16.0);
        expectSourceBeatsDisplay(*this, inspector, 16.0);

        applySourceBeats(seed.id, 12.0);
        inspector.clipPropertyChanged(seed.id);
        expectLoopEnd(*this, inspector, 12.0);
        expectSourceBeatsDisplay(*this, inspector, 12.0);

        applySourceBeats(seed.id, 8.0);
        inspector.clipPropertyChanged(seed.id);
        expectLoopEnd(*this, inspector, 8.0);
        expectSourceBeatsDisplay(*this, inspector, 8.0);
    }

    void testMidDragPropertyChangeRefreshesLoopEnd() {
        beginTest("Loop end follows source Beats during Beats drag");

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
    }

    void testBpmAndBeatsDisplaysRefreshTogether() {
        beginTest("BPM and Beats displays refresh from source interpretation");

        auto seed = makeInspectorAudioClip();
        ClipManager::getInstance().restoreClip(seed);

        ClipInspector inspector;
        inspector.setBounds(0, 0, 360, 640);
        inspector.setSelectedClip(seed.id);
        expectBpmDisplay(*this, inspector, 172.0);
        expectSourceBeatsDisplay(*this, inspector, 16.0);

        applySourceBeats(seed.id, 12.0);
        inspector.clipPropertyChanged(seed.id);
        expectBpmDisplay(*this, inspector, 129.0);
        expectSourceBeatsDisplay(*this, inspector, 12.0);

        applySourceBeats(seed.id, 8.0);
        inspector.clipPropertyChanged(seed.id);
        expectBpmDisplay(*this, inspector, 86.0);
        expectSourceBeatsDisplay(*this, inspector, 8.0);
    }

    void testLoopEndUsesLoopLengthNotPlacementLength() {
        beginTest("Inspector loop end follows source loop length, not placement length");

        auto seed = makeInspectorAudioClip();
        seed.setPlacementBeats(0.0, 96.0);
        seed.length = 96.0 * 60.0 / projectBPM;
        magda::test::audioEvent(seed).setLoopLengthBeats(12.0);
        magda::test::audioEvent(seed).setLoopLengthSeconds(12.0 * 60.0 / sourceBPM);
        ClipManager::getInstance().restoreClip(seed);

        ClipInspector inspector;
        inspector.setBounds(0, 0, 360, 640);
        inspector.setSelectedClip(seed.id);
        expectSourceBeatsDisplay(*this, inspector, 16.0);
        expectLoopEnd(*this, inspector, 12.0);
    }
};

static ClipInspectorJuceTest clipInspectorJuceTest;
