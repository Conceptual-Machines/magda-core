#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioClipTestHelpers.hpp"
#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipDisplayInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/ui/components/waveform/WaveformGridComponent.hpp"

using namespace magda;

namespace {

/// The audio editor's grid with the gain envelope shown, rendered offscreen.
juce::Image renderEditor(ClipId clipId) {
    daw::ui::WaveformGridComponent grid;
    grid.setParentWidth(800);
    grid.setMinimumHeight(300);
    grid.setHorizontalZoom(150.0);
    grid.setRelativeMode(true);
    grid.setClip(clipId);
    grid.setDisplayInfo(
        ClipDisplayInfo::from(*ClipManager::getInstance().getClip(clipId), 120.0, 4.0));
    grid.setGainEnvelopeShown(true);

    juce::Image image(juce::Image::ARGB, grid.getWidth(), grid.getHeight(), true);
    juce::Graphics g(image);
    grid.paintEntireComponent(g, false);
    return image;
}

}  // namespace

class GainEnvelopeEditorTests : public juce::UnitTest {
  public:
    GainEnvelopeEditorTests() : juce::UnitTest("Gain envelope editor", "magda") {}

    void runTest() override {
        beginTest("Warp markers paint in the sample view");
        {
            magda::test::resetJuceProjectState();
            const auto clipId = ClipManager::getInstance().createAudioClipBeats(
                1, 0.0, 8.0, "/tmp/magda_warp_marker_paint.wav", ClipView::Arrangement);
            test::setSourceDuration(*ClipManager::getInstance().getClip(clipId), 4.0);

            daw::ui::WaveformGridComponent grid;
            grid.setParentWidth(800);
            grid.setMinimumHeight(300);
            grid.setHorizontalZoom(150.0);
            grid.setRelativeMode(true);
            grid.setClip(clipId);
            grid.setDisplayInfo(
                ClipDisplayInfo::from(*ClipManager::getInstance().getClip(clipId), 120.0, 4.0));
            grid.setWarpMode(true);
            grid.setWarpMarkers({{0.0, 0.0}, {1.0, 1.2}, {2.5, 2.5}, {4.0, 4.0}});

            juce::Image image(juce::Image::ARGB, grid.getWidth(), grid.getHeight(), true);
            {
                juce::Graphics g(image);
                grid.paintEntireComponent(g, false);
            }
            const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("warp-marker-paint.png");
            out.deleteFile();
            juce::FileOutputStream stream(out);
            juce::PNGImageFormat().writeImageToStream(image, stream);
            expect(image.isValid());
            magda::test::resetJuceProjectState();
        }

        beginTest("The audio editor draws the gain envelope over the waveform");
        {
            magda::test::resetJuceProjectState();
            const auto clipId = ClipManager::getInstance().createAudioClipBeats(
                1, 0.0, 8.0, "/tmp/magda_gain_envelope_paint.wav", ClipView::Arrangement);
            test::setSourceDuration(*ClipManager::getInstance().getClip(clipId), 4.0);

            const auto image = renderEditor(clipId);
            const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("gain-envelope-paint.png");
            out.deleteFile();
            juce::FileOutputStream stream(out);
            juce::PNGImageFormat().writeImageToStream(image, stream);
            expect(image.isValid());
            magda::test::resetJuceProjectState();
        }
    }
};

static GainEnvelopeEditorTests gainEnvelopeEditorTests;
