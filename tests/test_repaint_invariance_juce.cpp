#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/clips/ClipComponent.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"

/**
 * Repaint invariance in the arrangement (#2026).
 *
 * A clip dragged or nudged across the timeline invalidates only the rectangle
 * it left behind. Painting that reads its geometry from g.getClipBounds() draws
 * its outlines around that damage instead of around the thing being drawn, so
 * every position the clip passed through kept a thin box — the lane border, the
 * clip outline, the rounded corners — and nothing ever repainted it away.
 *
 * The invariant: the damaged region decides WHETHER to paint, never WHAT. These
 * tests render each component whole, then again through a series of narrow
 * partial repaints, and require the two to be pixel-identical.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;
constexpr TrackId testTrackId = 1;
constexpr int stripWidth = 37;  // deliberately not a divisor of any component width

/// One repaint covering the whole component.
juce::Image renderWhole(juce::Component& component) {
    juce::Image image(juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g(image);
    component.paintEntireComponent(g, false);
    return image;
}

/// The same pixels, reached through a series of narrow partial repaints — what
/// a clip moving across the arrangement leaves behind as damage.
juce::Image renderInStrips(juce::Component& component, int width) {
    juce::Image image(juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    for (int x = 0; x < component.getWidth(); x += width) {
        juce::Graphics g(image);
        g.reduceClipRegion(juce::Rectangle<int>(x, 0, width, component.getHeight()));
        component.paintEntireComponent(g, false);
    }
    return image;
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

/// A clip whose source was never on disk — the broken-file placeholder is one
/// of the things a partial repaint used to drop.
ClipId createAudioClip(double startBeats, double lengthBeats) {
    return ClipManager::getInstance().createAudioClipBeats(testTrackId, startBeats, lengthBeats,
                                                           "/tmp/magda_repaint_test.wav",
                                                           ClipView::Arrangement, testTempoBPM);
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

class RepaintInvarianceTests : public juce::UnitTest {
  public:
    RepaintInvarianceTests() : juce::UnitTest("Repaint invariance", "magda") {}

    void runTest() override {
        beginTest("A MIDI clip painted in strips matches a whole paint");
        {
            PaintFixture fixture;
            auto& cm = ClipManager::getInstance();
            const auto clipId =
                cm.createMidiClipBeats(testTrackId, 0.0, 4.0, ClipView::Arrangement);
            auto* clip = cm.getClip(clipId);
            for (double beat : {0.0, 0.5, 1.0, 1.5, 2.0})
                clip->midiNotes.push_back(MidiNote{60, 100, beat, 0.25});

            ClipComponent component(clipId, &fixture.panel);
            component.setBounds(0, 0, 400, 80);

            expect(pixelsDiffering(renderWhole(component), renderInStrips(component, stripWidth)) ==
                       0,
                   "the clip outline and corners must not follow the damaged region");
        }

        beginTest("An audio clip painted in strips matches a whole paint");
        {
            // Covers the broken-file placeholder too: it decides what it can
            // show from the width it is given, and a narrow strip drops its
            // label entirely.
            PaintFixture fixture;
            const auto clipId = createAudioClip(8.0, 4.0);

            ClipComponent component(clipId, &fixture.panel);
            component.setBounds(0, 0, 400, 80);

            expect(pixelsDiffering(renderWhole(component), renderInStrips(component, stripWidth)) ==
                       0,
                   "waveform-area content must not depend on the damaged region");
        }

        beginTest("A track lane painted in strips matches a whole paint");
        {
            // The ghost outlines in the report were the lane border: drawn
            // around the damaged rectangle, it boxed in the clip's old bounds.
            PaintFixture fixture;
            TrackManager::getInstance().createTrack("Lane", TrackType::Audio);
            fixture.panel.tracksChanged();
            fixture.panel.setSize(400, 200);

            expect(pixelsDiffering(renderWhole(fixture.panel),
                                   renderInStrips(fixture.panel, stripWidth)) == 0,
                   "the lane border must be drawn on the lane, not on the damage");
        }
    }
};

RepaintInvarianceTests repaintInvarianceTests;

}  // namespace
