// The half of the transport layout that needs a graphics context: the widths
// the shipped fonts actually produce. test_transport_layout.cpp asserts the
// arithmetic; this one asserts that the strings really do fit inside it at the
// size MAGDA opens itself at, which is the regression in #2071.

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/layout/LayoutConfig.hpp"
#include "magda/daw/ui/panels/TransportLayout.hpp"
#include "magda/daw/ui/panels/TransportTextWidths.hpp"

class TransportLayoutFontsTest final : public juce::UnitTest {
  public:
    TransportLayoutFontsTest() : juce::UnitTest("Transport Layout Fonts", "magda") {}

    void runTest() override {
        using namespace magda::daw::ui::transport;
        const auto& config = magda::LayoutConfig::getInstance();

        beginTest("Measured widths are real numbers");
        const auto text = measureTextWidths();
        logMessage("bars=" + juce::String(text.barNumber) + " tempo=" + juce::String(text.tempo) +
                   " sigNum=" + juce::String(text.timeSigNumerator) +
                   " sigDen=" + juce::String(text.timeSigDenominator) + " cpuTitle=" +
                   juce::String(text.cpuTitle) + " cpuValue=" + juce::String(text.cpuValue) +
                   " gridDivision=" + juce::String(text.gridDivision) +
                   " gridToggle=" + juce::String(text.gridToggle));
        expect(text.barNumber > 0);
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

        beginTest("Each readout is wide enough for the string it has to draw");
        // The bars segment gets a quarter of the strip between the two dots.
        expect((l.playhead.getWidth() - 16) / 4 >= text.barNumber);
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
