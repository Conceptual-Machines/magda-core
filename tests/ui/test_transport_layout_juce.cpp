// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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
        logMessage("timecodeBox=" + juce::String(text.timecodeBox) +
                   " headline=" + juce::String(text.headlineTimecodeBox) +
                   " clock=" + juce::String(text.clock) + " tempo=" + juce::String(text.tempo) +
                   " sigNum=" + juce::String(text.timeSigNumerator) +
                   " sigDen=" + juce::String(text.timeSigDenominator) +
                   " caption=" + juce::String(text.rangeCaption) + " memCaption=" +
                   juce::String(text.memoryCaption) + " memTime=" + juce::String(text.memoryTime) +
                   " cpuTitle=" + juce::String(text.cpuTitle) +
                   " cpuValue=" + juce::String(text.cpuValue) +
                   " gridDivision=" + juce::String(text.gridDivision) + " gridToggle=" +
                   juce::String(text.gridToggle) + " banner=" + juce::String(text.banner));
        expect(text.timecodeBox > 0);
        expect(text.headlineTimecodeBox > text.timecodeBox);
        expect(text.clock > 0);
        expect(text.tempo > 0);
        expect(text.timeSigNumerator > 0);
        expect(text.timeSigDenominator > 0);
        expect(text.rangeCaption > 0);
        expect(text.memoryTime > 0);
        expect(text.cpuTitle > 0);
        expect(text.cpuValue > 0);
        expect(text.gridDivision > 0);
        expect(text.gridToggle > 0);
        expect(text.banner > 0);

        beginTest("Nothing drops into the overflow menu at the window MAGDA opens at");
        const auto l =
            compute(magda::LayoutConfig::defaultWindowWidth, config.transportHeight, text, 1.0f);
        juce::StringArray collapsed;
        for (auto section : kDropOrder)
            if (!l.isVisible(section))
                collapsed.add(juce::String(static_cast<int>(section)));
        logMessage("collapsed at the default width: " + collapsed.joinIntoString(", "));
        expect(l.navVisible, "navigation buttons collapsed");
        expect(l.loopBackVisible, "loop / back-to-arrangement collapsed");
        expect(l.punchVisible, "punch collapsed");
        expect(l.selLoopTimesVisible, "selection / loop readouts collapsed");
        expect(l.gridVisible, "grid cluster collapsed");
        expect(l.rightClusterVisible, "capture, CPU meter and QWERTY toggle collapsed");
        expect(!l.overflowVisible, "the overflow button is showing at the default size");

        // The rest of the checks read every readout, so they need a bar wide
        // enough to show them all.
        const auto wide = compute(2400, config.transportHeight, text, 1.0f);

        beginTest("...and at every spacing density the preference offers");
        for (float density : {0.6f, 1.0f, 1.4f}) {
            // The widest spacing needs a wider window than the one MAGDA opens at.
            const int width = density > 1.0f ? 1400 : magda::LayoutConfig::defaultWindowWidth;
            const auto dense = compute(width, config.transportHeight, text, density);
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

        beginTest("The timecode boxes grow with the bar numbers in view");
        {
            // Sized for three digits until a value needs more, then for every
            // digit of the largest position the readouts accept.
            expectEquals(timecodeBarDigits({0.0, 4.0 * 998.0}, 4, 4), kMinTimecodeBarDigits);
            expectEquals(timecodeBarDigits({4.0 * 999.0}, 4, 4), 4);
            const int maxDigits = timecodeBarDigits({kTimecodeMaxBeats}, 4, 4);
            expectEquals(maxDigits, 5);

            const auto fitted = measureTextWidths(maxDigits, 4);
            expect(fitted.timecodeBox > text.timecodeBox, "the box did not grow");
            const auto needed = magda::BarsBeatsTicksLabel::segmentWidthsFor(
                kTimecodeMaxBeats, 4, 4, true, kRowTimecodeFontSize);
            const auto grown = compute(2400, config.transportHeight, fitted, 1.0f);
            expect(grown.selectionStart.getWidth() >= needed[0] + needed[1] + needed[2],
                   "the box clips the largest position");
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
            label.setTimeSignature(4, 4);
            label.setSize(measureTextWidths(5, 4).timecodeBox, 20);
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

        beginTest("The CPU slot holds the peak form, not just the average");
        {
            // setCpuUsage shows the retained peak beside the average once it
            // runs ahead, so the widest readout is two numbers, not "100%".
            expect(cpuReadoutText(50, 51) == "50%", "a peak within the margin is not shown");
            expect(cpuReadoutText(88, 100) == "88/100%");

            const auto cpuFont = fonts.getMonoFont(kCpuValueFontSize);
            expect(wide.cpuValue.getWidth() >= widthOf(cpuFont, cpuReadoutText(88, 100)),
                   "the CPU readout clips once the peak runs ahead");
        }

        beginTest("The playhead clock reads minutes, seconds and milliseconds");
        expectEquals(clockText(0.0), juce::String("0:00.000"));
        expectEquals(clockText(61.5), juce::String("1:01.500"));
        expectEquals(clockText(3725.004), juce::String("1:02:05.004"));
        expectEquals(clockText(-1.0), juce::String("0:00.000"));

        beginTest("Each readout is wide enough for the string it has to draw");
        expect(wide.clock.getWidth() >= text.clock);
        expect(wide.playhead.getWidth() >= text.headlineTimecodeBox);
        expect(wide.selectionStart.getWidth() >= text.timecodeBox);
        expect(wide.loopEnd.getWidth() >= text.timecodeBox);
        expect(wide.selectionCaption.getWidth() >= text.rangeCaption);
        expect(wide.tempo.getWidth() >= text.tempo);
        expect(wide.timeSigNumerator.getWidth() >= text.timeSigNumerator);
        expect(wide.timeSigDenominator.getWidth() >= text.timeSigDenominator);
        expect(wide.autoGrid.getWidth() >= text.gridToggle);
        expect(wide.snap.getWidth() >= text.gridToggle);
        expect(wide.gridDivision.getWidth() >= text.gridDivision);
        expect(wide.cpuValue.getWidth() >= text.cpuValue);
        expect(wide.cpuTitle.getWidth() >= text.cpuTitle);
    }
};

static TransportLayoutFontsTest transportLayoutFontsTest;
