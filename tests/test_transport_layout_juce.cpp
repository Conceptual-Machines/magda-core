// The half of the transport layout that needs a graphics context: the widths
// the shipped fonts actually produce. test_transport_layout.cpp asserts the
// arithmetic; this one asserts that the strings really do fit inside it at the
// size MAGDA opens itself at, which is the regression in #2071.

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/TempoUtils.hpp"
#include "magda/daw/ui/components/common/BarsBeatsTicksLabel.hpp"
#include "magda/daw/ui/components/common/DraggableValueLabel.hpp"
#include "magda/daw/ui/layout/LayoutConfig.hpp"
#include "magda/daw/ui/panels/TransportLayout.hpp"
#include "magda/daw/ui/panels/TransportTextWidths.hpp"
#include "magda/daw/ui/themes/FontManager.hpp"

class TransportLayoutFontsTest final : public juce::UnitTest {
  public:
    TransportLayoutFontsTest() : juce::UnitTest("Transport Layout Fonts", "magda") {}

    void runTest() override {
        using namespace magda::daw::ui::transport;
        auto& fonts = magda::FontManager::getInstance();
        const auto widthOf = [](const juce::Font& font, juce::StringRef string) {
            return juce::GlyphArrangement::getStringWidthInt(font, string);
        };
        const auto& config = magda::LayoutConfig::getInstance();

        beginTest("Measured widths are real numbers");
        const auto text = measureTextWidths();
        logMessage("timecodeBox=" + juce::String(text.timecodeBox) + " timecodeCaption=" +
                   juce::String(text.timecodeCaption) + " tempo=" + juce::String(text.tempo) +
                   " sigNum=" + juce::String(text.timeSigNumerator) +
                   " sigDen=" + juce::String(text.timeSigDenominator) + " cpuTitle=" +
                   juce::String(text.cpuTitle) + " cpuValue=" + juce::String(text.cpuValue) +
                   " gridDivision=" + juce::String(text.gridDivision) +
                   " gridToggle=" + juce::String(text.gridToggle));
        expect(text.timecodeBox > 0);
        expect(text.timecodeCaption > 0);
        expect(text.tempo > 0);
        expect(text.timeSigNumerator > 0);
        expect(text.timeSigDenominator > 0);
        expect(text.cpuTitle > 0);
        expect(text.cpuValue > 0);
        expect(text.gridDivision > 0);
        expect(text.gridToggle > 0);

        beginTest("Nothing collapses at the window MAGDA opens at");
        const auto l = compute(magda::LayoutConfig::defaultWindowWidth,
                               config.defaultTransportHeight, text, 1.0f);
        expect(l.navVisible, "navigation buttons collapsed");
        expect(l.loopBackVisible, "loop / back-to-arrangement collapsed");
        expect(l.punchVisible, "punch box collapsed");
        expect(l.selLoopTimesVisible, "selection / loop readouts collapsed");
        expect(l.gridVisible, "grid cluster collapsed");
        expect(l.rightClusterVisible, "CPU meter and QWERTY toggle collapsed");
        expect(!l.overflowVisible, "the overflow button is showing at the default size");

        beginTest("...and at every spacing density the preference offers");
        for (float density : {0.6f, 1.0f, 1.4f}) {
            const auto dense = compute(magda::LayoutConfig::defaultWindowWidth,
                                       config.defaultTransportHeight, text, density);
            expect(!dense.overflowVisible,
                   "the overflow button is showing at density " + juce::String(density, 1));
        }

        beginTest("The font scale is applied once, however often fonts are re-applied");
        {
            // TransportPanel hands its cached-font children a font again on
            // every look-and-feel change, and re-measures for it. Both have to
            // be idempotent, or a run of font changes would walk the sizes.
            auto& config = magda::Config::getInstance();
            const double originalScale = config.getUIFontScale();
            const struct Restore {
                magda::Config& config;
                double scale;
                ~Restore() {
                    config.setUIFontScale(scale);
                }
            } restore{config, originalScale};

            juce::Label label;
            label.setFont(fonts.getUIFont(kCpuTitleFontSize));
            const float once = label.getFont().getHeight();
            label.setFont(fonts.getUIFont(kCpuTitleFontSize));
            expectEquals(label.getFont().getHeight(), once);

            config.setUIFontScale(originalScale * 1.25);
            label.setFont(fonts.getUIFont(kCpuTitleFontSize));
            const float scaled = label.getFont().getHeight();
            label.setFont(fonts.getUIFont(kCpuTitleFontSize));
            expectEquals(label.getFont().getHeight(), scaled);
            expectWithinAbsoluteError(scaled, once * 1.25f, 0.01f);

            // ...and the measurement follows it by the same one factor.
            const auto scaledText = measureTextWidths();
            expectWithinAbsoluteError(static_cast<float>(scaledText.tempo),
                                      static_cast<float>(text.tempo) * 1.25f, 1.5f);
        }

        beginTest("The timecode box holds the largest position its range can reach");
        {
            // Positions are one-indexed and the labels accept kTimecodeMaxBeats
            // beats, so at one beat per bar the bar number reaches six digits.
            // Sizing the box for four clipped it.
            const auto needed = magda::BarsBeatsTicksLabel::segmentWidthsFor(
                kTimecodeMaxBeats, magda::MIN_TIME_SIGNATURE_VALUE, magda::MAX_TIME_SIGNATURE_VALUE,
                true);
            const auto timecodeFont = fonts.getUIFont(magda::BarsBeatsTicksLabel::kTextFontSize);
            const juce::String widestBars(
                static_cast<int>(kTimecodeMaxBeats / magda::MIN_TIME_SIGNATURE_VALUE) + 1);

            expect(needed[0] >= widthOf(timecodeFont, widestBars),
                   "the bars segment cannot hold " + widestBars);
            expect(needed[1] >=
                   widthOf(timecodeFont, juce::String(magda::MAX_TIME_SIGNATURE_VALUE)));
            expect(needed[2] >= widthOf(timecodeFont, "888"));
            expect(l.playhead.getWidth() >= needed[0] + needed[1] + needed[2]);
        }

        beginTest("A bar number that gains a digit re-proportions the readout's segments");
        {
            // The strip is shared out by the digits on screen, so "1.1.000"
            // reads balanced instead of reserving the width of a five-digit
            // bar number, and a value that grows a digit relayouts rather than
            // clipping or just repainting.
            magda::BarsBeatsTicksLabel label;
            label.setRange(0.0, kTimecodeMaxBeats, 0.0);
            label.setBarsBeatsIsPosition(true);
            label.setBeatsPerBar(4);
            label.setSize(text.timecodeBox, 20);
            expect(label.getNumChildComponents() == 3, "expected three segments");

            const auto barsBefore = label.getChildComponent(0)->getBounds();
            const auto ticksBefore = label.getChildComponent(2)->getBounds();
            // A one-digit bar number gets no more of the strip than the
            // one-digit beat number beside it.
            expect(barsBefore.getWidth() == label.getChildComponent(1)->getWidth(),
                   "a one-digit bar number took more room than a one-digit beat number");

            label.setValue(9.0 * 4.0, juce::dontSendNotification);  // bar 10
            expect(label.getChildComponent(0)->getWidth() > barsBefore.getWidth(),
                   "the bar segment did not grow for its second digit");
            expect(label.getChildComponent(2)->getBounds() != ticksBefore,
                   "the segments kept their one-digit proportions");

            // The widest value the range allows still fits, segment by segment.
            label.setValue(kTimecodeMaxBeats, juce::dontSendNotification);
            const auto needed =
                magda::BarsBeatsTicksLabel::segmentWidthsFor(kTimecodeMaxBeats, 4, 4, true);
            for (int i = 0; i < 3; ++i)
                expect(label.getChildComponent(i)->getWidth() >= needed[i],
                       "segment " + juce::String(i) + " clips the largest position");
        }

        beginTest("The digits stay clear of what is drawn over the readout's end");
        {
            // TransportPanel draws the group caption over the box's top-right
            // corner and the punch box carries its icons there. The layout
            // adds that zone to every readout and hands it to the label, which
            // keeps its strip out of it.
            expect(l.timeBoxTrailingInset >= text.timecodeCaption,
                   "the inset does not cover the caption");

            magda::BarsBeatsTicksLabel label;
            label.setRange(0.0, kTimecodeMaxBeats, 0.0);
            label.setBarsBeatsIsPosition(true);
            label.setSize(l.playhead.getWidth(), l.playhead.getHeight());
            label.setTrailingInset(l.timeBoxTrailingInset);
            label.setValue(kTimecodeMaxBeats, juce::dontSendNotification);

            // The last glyph sits half a segment pad inside its segment.
            const int glyphRight = label.getChildComponent(2)->getRight() -
                                   (magda::BarsBeatsTicksLabel::kSegmentPad / 2);
            expect(glyphRight <= label.getWidth() - l.timeBoxTrailingInset,
                   "the ticks run under the caption");
            const auto needed = magda::BarsBeatsTicksLabel::segmentWidthsFor(
                kTimecodeMaxBeats, magda::DEFAULT_TIME_SIGNATURE_NUMERATOR,
                magda::DEFAULT_TIME_SIGNATURE_NUMERATOR, true);
            for (int i = 0; i < 3; ++i)
                expect(label.getChildComponent(i)->getWidth() >= needed[i],
                       "segment " + juce::String(i) + " lost room to the inset");
        }

        beginTest("The CPU slot holds the peak form, not just the average");
        {
            // setCpuUsage shows the retained peak beside the average once it
            // runs ahead, so the widest readout is two numbers, not "100%".
            expect(cpuReadoutText(50, 51) == "50%", "a peak within the margin is not shown");
            expect(cpuReadoutText(88, 100) == "88/100%");

            const auto cpuFont = fonts.getMonoFont(kCpuValueFontSize);
            expect(l.cpuValue.getWidth() >= widthOf(cpuFont, cpuReadoutText(88, 100)),
                   "the CPU readout clips once the peak runs ahead");
        }

        beginTest("Each readout is wide enough for the string it has to draw");
        expect(l.playhead.getWidth() >= text.timecodeBox);
        expect(l.tempo.getWidth() >= text.tempo);
        expect(l.timeSigNumerator.getWidth() >= text.timeSigNumerator);
        expect(l.timeSigDenominator.getWidth() >= text.timeSigDenominator);
        expect(l.autoGrid.getWidth() >= text.gridToggle);
        expect(l.snap.getWidth() >= text.gridToggle);
        expect(l.gridDivision.getWidth() >= text.gridDivision);
        expect(l.cpuValue.getWidth() >= text.cpuValue);
        expect(l.cpuTitle.getWidth() >= text.cpuTitle);
    }
};

static TransportLayoutFontsTest transportLayoutFontsTest;
