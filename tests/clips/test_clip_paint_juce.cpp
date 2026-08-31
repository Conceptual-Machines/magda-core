#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioClipTestHelpers.hpp"
#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/SourcePool.hpp"
#include "magda/daw/ui/components/clips/ClipComponent.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"

/**
 * Clip painting across every content shape (#1901).
 *
 * ClipComponent::paint reads the audio event for the loop-cut markers, the
 * fade handles and the waveform. A MIDI clip has no audio event, so a paint
 * that reaches for one without checking dereferences null and takes the
 * message thread down. That is exactly what shipped: the model refactor moved
 * those reads onto the event and the suite never noticed, because nothing in
 * it painted a clip.
 *
 * These tests render each shape into an offscreen image. They assert almost
 * nothing about the pixels: surviving the paint IS the assertion, and it is
 * the one the crash would have failed.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;
constexpr TrackId testTrackId = 1;

/// Render a clip the way the arrangement does, through the component tree.
juce::Image renderClip(ClipId clipId, TrackContentPanel& panel) {
    ClipComponent component(clipId, &panel);
    component.setBounds(0, 0, 400, 80);

    juce::Image image(juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g(image);
    component.paintEntireComponent(g, false);
    return image;
}

void paintClip(ClipId clipId, TrackContentPanel& panel) {
    renderClip(clipId, panel);
}

/// How many pixels differ between two renders of the same size.
int pixelsDiffering(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return -1;

    int differing = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++differing;
    return differing;
}

/// A clip whose event points at a source that was never on disk. Painting has
/// to cope: the file can go missing between sessions.
ClipId createAudioClip(double startBeats, double lengthBeats,
                       ClipView view = ClipView::Arrangement) {
    return ClipManager::getInstance().createAudioClipBeats(
        testTrackId, startBeats, lengthBeats, "/tmp/magda_paint_test.wav", view, testTempoBPM);
}

struct PaintFixture {
    TrackContentPanel panel;

    PaintFixture() {
        magda::test::resetJuceProjectState();
        panel.setSize(2000, 400);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);
    }

    ~PaintFixture() {
        magda::test::resetJuceProjectState();
    }
};

class ClipPaintTests : public juce::UnitTest {
  public:
    ClipPaintTests() : juce::UnitTest("Clip painting", "magda") {}

    void runTest() override {
        beginTest("A plain MIDI clip paints");
        {
            PaintFixture fixture;
            const auto clipId = ClipManager::getInstance().createMidiClipBeats(
                testTrackId, 0.0, 4.0, ClipView::Arrangement);
            paintClip(clipId, fixture.panel);
            expect(true, "painting a MIDI clip must not dereference an audio event");
        }

        beginTest("A looped MIDI clip paints");
        {
            // The crash: the loop-cut markers read loopLengthSeconds off the
            // clip's audio event, which a MIDI clip does not have.
            PaintFixture fixture;
            const auto clipId = ClipManager::getInstance().createMidiClipBeats(
                testTrackId, 0.0, 16.0, ClipView::Arrangement);
            auto* clip = ClipManager::getInstance().getClip(clipId);
            clip->loopEnabled = true;
            clip->loopLengthBeats = 4.0;
            paintClip(clipId, fixture.panel);
            expect(clip->loopEnabled, "the MIDI loop survives a paint");
        }

        beginTest("A looped MIDI clip draws loop boundary cuts");
        {
            // Surviving the paint is not enough: before the event split the
            // loop length lived on ClipInfo and MIDI clips set it, so looped
            // MIDI clips drew boundary cuts. Reading the loop off the audio
            // event silently dropped them, because a MIDI clip has none.
            PaintFixture fixture;
            auto& cm = ClipManager::getInstance();

            const auto plainId =
                cm.createMidiClipBeats(testTrackId, 0.0, 16.0, ClipView::Arrangement);
            const auto plain = renderClip(plainId, fixture.panel);

            const auto loopedId =
                cm.createMidiClipBeats(testTrackId, 32.0, 16.0, ClipView::Arrangement);
            auto* looped = cm.getClip(loopedId);
            looped->colour = cm.getClip(plainId)->colour;
            looped->loopEnabled = true;
            looped->loopLengthBeats = 4.0;
            const auto withCuts = renderClip(loopedId, fixture.panel);

            expect(pixelsDiffering(plain, withCuts) > 0,
                   "a looped MIDI clip must render differently from an unlooped one");
        }

        beginTest("A looped audio clip draws loop boundary cuts");
        {
            PaintFixture fixture;
            auto& cm = ClipManager::getInstance();

            const auto plainId = createAudioClip(0.0, 16.0);
            const auto plain = renderClip(plainId, fixture.panel);

            const auto loopedId = createAudioClip(32.0, 16.0);
            auto* looped = cm.getClip(loopedId);
            looped->colour = cm.getClip(plainId)->colour;
            auto* event = looped->primaryEvent();
            event->interpBpm = 120.0;
            looped->loopEnabled = true;
            event->setLoopLengthSeconds(2.0);
            const auto withCuts = renderClip(loopedId, fixture.panel);

            expect(pixelsDiffering(plain, withCuts) > 0,
                   "a looped audio clip must render differently from an unlooped one");
        }

        beginTest("A MIDI clip with notes and a phase offset paints");
        {
            PaintFixture fixture;
            const auto clipId = ClipManager::getInstance().createMidiClipBeats(
                testTrackId, 0.0, 8.0, ClipView::Arrangement);
            auto* clip = ClipManager::getInstance().getClip(clipId);
            clip->loopEnabled = true;
            clip->loopLengthBeats = 2.0;
            clip->midiOffset = 0.5;
            clip->midiTrimOffset = 0.25;
            for (double beat : {0.0, 0.5, 1.0, 1.5})
                clip->midiNotes.push_back(MidiNote{60, 100, beat, 0.25});
            paintClip(clipId, fixture.panel);
            expect(clip->midiNotes.size() == 4, "notes survive a paint");
        }

        beginTest("An audio clip paints with a missing source file");
        {
            // The waveform, fades and loop cuts all read the event; none of
            // them may assume the file resolved.
            PaintFixture fixture;
            const auto clipId = createAudioClip(0.0, 8.0);
            paintClip(clipId, fixture.panel);

            const auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
            expect(event != nullptr, "an audio clip keeps its event across a paint");
        }

        beginTest("A looped audio clip paints its loop cuts");
        {
            PaintFixture fixture;
            const auto clipId = createAudioClip(0.0, 16.0);
            auto* clip = ClipManager::getInstance().getClip(clipId);
            auto* event = clip->primaryEvent();
            event->interpBpm = 120.0;
            event->interpTotalBeats = 8.0;
            clip->loopEnabled = true;
            event->setLoopLengthSeconds(2.0);
            paintClip(clipId, fixture.panel);
            expect(clip->loopEnabled, "the source loop survives a paint");
        }

        beginTest("A beat-mode audio clip paints");
        {
            PaintFixture fixture;
            const auto clipId = createAudioClip(0.0, 8.0);
            auto* clip = ClipManager::getInstance().getClip(clipId);
            auto* event = clip->primaryEvent();
            event->autoTempo = true;
            event->interpBpm = 174.0;
            event->interpTotalBeats = 8.0;
            clip->loopEnabled = true;
            event->setLoopLengthBeats(8.0);
            paintClip(clipId, fixture.panel);
            expect(event->autoTempo, "beat mode survives a paint");
        }

        beginTest("An audio clip with fades and a trim paints");
        {
            PaintFixture fixture;
            const auto clipId = createAudioClip(0.0, 8.0);
            auto* event = primaryEventOf(ClipManager::getInstance().getClip(clipId));
            event->fadeInSeconds = 0.5;
            event->fadeOutSeconds = 0.75;
            event->setAnchorSeconds(1.0);
            event->reversed = true;
            paintClip(clipId, fixture.panel);
            expect(event->fadeInSeconds > 0.0, "fades survive a paint");
        }

        beginTest("An audio clip with no event at all paints");
        {
            // Nothing builds this today, but a hand-edited or truncated
            // project can load one, and paint is the first thing to touch it.
            PaintFixture fixture;
            const auto clipId = createAudioClip(0.0, 4.0);
            auto* clip = ClipManager::getInstance().getClip(clipId);
            clip->audio().events.clear();
            paintClip(clipId, fixture.panel);
            expect(clip->primaryEvent() == nullptr, "an eventless audio clip still paints");
        }

        beginTest("Session clips paint");
        {
            PaintFixture fixture;
            const auto audioId = createAudioClip(0.0, 4.0, ClipView::Session);
            const auto midiId = ClipManager::getInstance().createMidiClipBeats(
                testTrackId, 0.0, 4.0, ClipView::Session);
            paintClip(audioId, fixture.panel);
            paintClip(midiId, fixture.panel);
            expect(true, "session clips of both content types paint");
        }

        beginTest("A selected clip paints");
        {
            PaintFixture fixture;
            const auto midiId = ClipManager::getInstance().createMidiClipBeats(
                testTrackId, 0.0, 4.0, ClipView::Arrangement);
            const auto audioId = createAudioClip(8.0, 4.0);
            SelectionManager::getInstance().selectClips({midiId, audioId});
            paintClip(midiId, fixture.panel);
            paintClip(audioId, fixture.panel);
            expect(true, "selection overlays paint for both content types");
        }

        beginTest("A disabled clip paints");
        {
            PaintFixture fixture;
            const auto clipId = ClipManager::getInstance().createMidiClipBeats(
                testTrackId, 0.0, 4.0, ClipView::Arrangement);
            ClipManager::getInstance().setClipEnabled(clipId, false);
            paintClip(clipId, fixture.panel);
            expect(!ClipManager::getInstance().getClip(clipId)->enabled,
                   "a disabled clip still paints");
        }

        beginTest("Ghost siblings paint");
        {
            PaintFixture fixture;
            auto& cm = ClipManager::getInstance();
            const auto original =
                cm.createMidiClipBeats(testTrackId, 0.0, 4.0, ClipView::Arrangement);
            const auto ghost = cm.duplicateClipAsGhostAtBeats(original, 8.0);
            paintClip(original, fixture.panel);
            paintClip(ghost, fixture.panel);
            expect(cm.isGhostClip(original), "ghost clips paint their index badge");
        }
    }
};

ClipPaintTests clipPaintTests;

}  // namespace
