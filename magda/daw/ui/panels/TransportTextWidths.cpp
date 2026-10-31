// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TransportTextWidths.hpp"

#include <cmath>

#include "../components/common/BarsBeatsTicksLabel.hpp"
#include "../components/common/GridDivisionMenu.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "core/StringTable.hpp"
#include "core/TempoUtils.hpp"

namespace magda::daw::ui::transport {

juce::String cpuReadoutText(int averagePercent, int peakPercent) {
    if (peakPercent > averagePercent + kCpuPeakMargin)
        return juce::String(averagePercent) + "/" + juce::String(peakPercent) + "%";
    return juce::String(averagePercent) + "%";
}

juce::String clockText(double seconds) {
    const auto totalMs = static_cast<juce::int64>(std::llround(juce::jmax(0.0, seconds) * 1000.0));
    const auto ms = static_cast<int>(totalMs % 1000);
    const auto totalSeconds = totalMs / 1000;
    const auto secs = static_cast<int>(totalSeconds % 60);
    const auto mins = static_cast<int>((totalSeconds / 60) % 60);
    const auto hours = static_cast<int>(totalSeconds / 3600);
    const auto pad = [](int value, int width) {
        return juce::String(value).paddedLeft('0', width);
    };
    if (hours > 0)
        return juce::String(hours) + ":" + pad(mins, 2) + ":" + pad(secs, 2) + "." + pad(ms, 3);
    return juce::String(mins) + ":" + pad(secs, 2) + "." + pad(ms, 3);
}

int timecodeBarDigits(std::initializer_list<double> beats, int numerator, int denominator) {
    const double barLength = beatsPerBar(numerator, denominator);
    int digits = kMinTimecodeBarDigits;
    for (double value : beats) {
        const auto bar =
            static_cast<juce::int64>(std::floor(juce::jmax(0.0, value) / barLength)) + 1;
        digits = juce::jmax(digits, juce::String(bar).length());
    }
    return digits;
}

TextWidths measureTextWidths(int barDigits, int numerator) {
    auto& fonts = FontManager::getInstance();
    const auto widthOf = [](const juce::Font& font, juce::StringRef text) {
        return juce::GlyphArrangement::getStringWidthInt(font, text);
    };

    // Each width is measured in the font that widget draws with, so a change of
    // font size or family moves the layout with it instead of overflowing it.
    // The sizes come from the widgets themselves wherever they own one.
    const auto headlineFont = fonts.getUIFont(kHeadlineFontSize);
    const auto memoryCaptionFont = fonts.getUIFont(kMemoryCaptionFontSize);
    const auto memoryTimeFont = fonts.getUIFont(kMemoryTimeFontSize);
    const auto bannerFont = fonts.getUIFont(kBannerFontSize).boldened();
    const auto divisionFont = fonts.getUIFontBold(GridDivisionButton::kFontSize);
    const auto toggleFont =
        fonts.getUIFontBold(SmallButtonLookAndFeel::getInstance().getFontSize());
    const auto cpuTitleFont = fonts.getUIFont(kCpuTitleFontSize);
    const auto cpuValueFont = fonts.getMonoFont(kCpuValueFontSize);

    TextWidths text;
    // The readout sizes itself from its own segment shares, here for a value
    // whose bar number has barDigits digits.
    const auto lastBarStart =
        (std::pow(10.0, barDigits) - 2.0) * beatsPerBar(numerator, MAX_TIME_SIGNATURE_VALUE);
    const auto timecodeWidth = [&](float fontSize) {
        return BarsBeatsTicksLabel::preferredWidthForRange(lastBarStart, numerator, numerator, true,
                                                           fontSize);
    };
    text.timecodeBox = timecodeWidth(kRowTimecodeFontSize);
    text.headlineTimecodeBox = timecodeWidth(kHeadlineFontSize);
    text.tempo = widthOf(headlineFont, juce::String(MAX_VALID_BPM, 2));
    text.timeSigNumerator = widthOf(headlineFont, juce::String(MAX_TIME_SIGNATURE_VALUE) + "/");
    text.timeSigDenominator = widthOf(headlineFont, juce::String(MAX_TIME_SIGNATURE_VALUE));
    text.cpuTitle = widthOf(cpuTitleFont, tr("transport.cpu.cpu"));
    // The readout gains a second number once the peak runs ahead of the average,
    // so measure every digit-count combination it can reach rather than just
    // "100%". A run of the widest digit stands in for each length.
    for (int average : {8, 88, 100})
        for (int peak : {8, 88, 100})
            text.cpuValue =
                juce::jmax(text.cpuValue, widthOf(cpuValueFont, cpuReadoutText(average, peak)));
    text.gridToggle =
        juce::jmax(widthOf(toggleFont, kAutoGridCaption), widthOf(toggleFont, kSnapCaption));
    const auto rangeCaptionFont = fonts.getUIFontBold(kRangeCaptionFontSize);
    text.rangeCaption = juce::jmax(widthOf(rangeCaptionFont, kSelectionCaption),
                                   widthOf(rangeCaptionFont, kLoopCaption));

    text.clock = widthOf(fonts.getMonoFont(kClockFontSize), kClockWidest);
    text.memoryCaption = widthOf(memoryCaptionFont, kMemoryCaption);
    text.memoryTime = widthOf(memoryTimeFont, kMemoryTimeWidest);
    for (const char* banner : kBannerTexts)
        text.banner = juce::jmax(text.banner, widthOf(bannerFont, banner));

    for (const auto& division : kStandardGridDivisions)
        text.gridDivision = juce::jmax(text.gridDivision, widthOf(divisionFont, division.label));
    return text;
}

}  // namespace magda::daw::ui::transport
