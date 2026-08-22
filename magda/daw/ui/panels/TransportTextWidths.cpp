#include "TransportTextWidths.hpp"

#include "../components/common/BarsBeatsTicksLabel.hpp"
#include "../components/common/GridDivisionMenu.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "core/StringTable.hpp"
#include "core/TempoUtils.hpp"

namespace magda::daw::ui::transport {

TextWidths measureTextWidths() {
    auto& fonts = FontManager::getInstance();
    const auto widthOf = [](const juce::Font& font, juce::StringRef text) {
        return juce::GlyphArrangement::getStringWidthInt(font, text);
    };

    // Each width is measured in the font that widget draws with, so a change of
    // font size or family moves the layout with it instead of overflowing it.
    // The sizes come from the widgets themselves wherever they own one.
    const auto timecodeFont = fonts.getUIFont(BarsBeatsTicksLabel::kTextFontSize);
    const auto readoutFont = fonts.getUIFont(kReadoutFontSize);
    const auto divisionFont = fonts.getUIFontBold(GridDivisionButton::kFontSize);
    const auto toggleFont =
        fonts.getUIFontBold(SmallButtonLookAndFeel::getInstance().getFontSize());
    const auto cpuTitleFont = fonts.getUIFont(kCpuTitleFontSize);
    const auto cpuValueFont = fonts.getMonoFont(kCpuValueFontSize);

    TextWidths text;
    // The bars segment is the widest of the three: beats stop at the time
    // signature and ticks at three digits.
    text.barNumber = widthOf(timecodeFont, "0000");
    text.tempo = widthOf(readoutFont, juce::String(MAX_VALID_BPM, 2));
    text.timeSigNumerator = widthOf(readoutFont, juce::String(MAX_TIME_SIGNATURE_VALUE) + "/");
    text.timeSigDenominator = widthOf(readoutFont, juce::String(MAX_TIME_SIGNATURE_VALUE));
    text.cpuTitle = widthOf(cpuTitleFont, tr("transport.cpu.cpu"));
    text.cpuValue = widthOf(cpuValueFont, "100%");
    text.gridToggle =
        juce::jmax(widthOf(toggleFont, kAutoGridCaption), widthOf(toggleFont, kSnapCaption));

    // The division button stacks the numerator over the denominator, so what it
    // has to hold is the widest single line anywhere in the division table.
    for (const auto& division : kStandardGridDivisions) {
        const juce::String label(division.label);
        const int slash = label.indexOfChar('/');
        text.gridDivision =
            juce::jmax(text.gridDivision, widthOf(divisionFont, label.substring(0, slash).trim()),
                       widthOf(divisionFont, label.substring(slash + 1).trim()));
    }
    return text;
}

}  // namespace magda::daw::ui::transport
