#include "TransportTextWidths.hpp"

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

TextWidths measureTextWidths() {
    auto& fonts = FontManager::getInstance();
    const auto widthOf = [](const juce::Font& font, juce::StringRef text) {
        return juce::GlyphArrangement::getStringWidthInt(font, text);
    };

    // Each width is measured in the font that widget draws with, so a change of
    // font size or family moves the layout with it instead of overflowing it.
    // The sizes come from the widgets themselves wherever they own one.
    const auto headlineFont = fonts.getUIFont(kHeadlineFontSize);
    const auto readoutFont = fonts.getUIFont(kReadoutFontSize);
    const auto keyRootFont = fonts.getUIFontBold(kReadoutFontSize);
    const auto keyQualityFont = fonts.getUIFont(kKeyQualityFontSize);
    const auto memoryCaptionFont = fonts.getUIFont(kMemoryCaptionFontSize);
    const auto memoryTimeFont = fonts.getUIFont(kMemoryTimeFontSize);
    const auto bannerFont = fonts.getUIFont(kBannerFontSize).boldened();
    const auto divisionFont = fonts.getUIFontBold(GridDivisionButton::kFontSize);
    const auto toggleFont =
        fonts.getUIFontBold(SmallButtonLookAndFeel::getInstance().getFontSize());
    const auto cpuTitleFont = fonts.getUIFont(kCpuTitleFontSize);
    const auto cpuValueFont = fonts.getMonoFont(kCpuValueFontSize);
    const auto captionFont = fonts.getUIFont(kTimecodeCaptionFontSize);

    TextWidths text;
    // The readout sizes itself: it knows its own segment shares and how large a
    // bar number the range can reach, which a box drawn around it does not.
    const auto timecodeWidth = [](float fontSize) {
        return BarsBeatsTicksLabel::preferredWidthForRange(
            kTimecodeMaxBeats, MIN_TIME_SIGNATURE_VALUE, MAX_TIME_SIGNATURE_VALUE, true, fontSize);
    };
    text.timecodeBox = timecodeWidth(kRowTimecodeFontSize);
    text.stackTimecodeBox = timecodeWidth(kStackTimecodeFontSize);
    text.headlineTimecodeBox = timecodeWidth(kHeadlineTimecodeFontSize);
    // Packed readouts draw their S / E / I / O letter before the digits.
    const auto overlayFont = fonts.getUIFont(BarsBeatsTicksLabel::kPackedOverlayFontSize);
    for (const char* letter : {"S", "E", "I", "O"})
        text.timecodeOverlay = juce::jmax(text.timecodeOverlay, widthOf(overlayFont, letter));
    text.timecodeOverlay += BarsBeatsTicksLabel::kPackedOverlayGap;
    // The caption TransportPanel draws over the box's top-right corner. The
    // layout reserves this much at the end of every readout, and the readout
    // keeps its digits out of it.
    for (const char* caption : {kSelectionCaption, kLoopCaption, kCursorCaption})
        text.timecodeCaption = juce::jmax(text.timecodeCaption, widthOf(captionFont, caption));
    // The readout's glyphs stop this far inside its edge on their own (the strip
    // inset plus half a segment's air), so the box only has to grow by what the
    // caption needs beyond that.
    text.timecodeGlyphInset =
        BarsBeatsTicksLabel::kEdgeInset + (BarsBeatsTicksLabel::kSegmentPad / 2);
    text.tempo = widthOf(headlineFont, juce::String(MAX_VALID_BPM, 2));
    text.timeSigNumerator = widthOf(readoutFont, juce::String(MAX_TIME_SIGNATURE_VALUE) + "/");
    text.timeSigDenominator = widthOf(readoutFont, juce::String(MAX_TIME_SIGNATURE_VALUE));
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
    text.rangeChip =
        juce::jmax(widthOf(toggleFont, kSelectionCaption), widthOf(toggleFont, kLoopCaption));
    text.keep = widthOf(toggleFont, kKeepCaption);

    text.keyRoot = widthOf(keyRootFont, kNoKeyText);
    for (const char* root : kKeyRootNames)
        text.keyRoot = juce::jmax(text.keyRoot, widthOf(keyRootFont, root));
    for (const char* quality : kKeyQualityNames)
        text.keyQuality = juce::jmax(text.keyQuality, widthOf(keyQualityFont, quality));

    text.memoryCaption = widthOf(memoryCaptionFont, kMemoryCaption);
    text.memoryTime = widthOf(memoryTimeFont, kMemoryTimeWidest);
    for (const char* banner : kBannerTexts)
        text.banner = juce::jmax(text.banner, widthOf(bannerFont, banner));

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
