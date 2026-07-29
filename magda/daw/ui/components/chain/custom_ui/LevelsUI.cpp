#include "LevelsUI.hpp"

#include <cmath>

#include "audio/analysis/TrackMeasurer.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

using magda::daw::audio::kSilenceDb;
using magda::daw::audio::kSilenceLufs;

namespace {

juce::String fmtLufs(float v) {
    return v <= kSilenceLufs + 1.0f ? juce::String("-inf") : juce::String(v, 1);
}
juce::String fmtDb(float v) {
    return v <= kSilenceDb + 1.0f ? juce::String("-inf") : juce::String(v, 1);
}
juce::String fmtLu(float v) {
    return juce::String(v, 1);
}

// Colour a true-peak value: hot near/over 0 dBTP, warm approaching it.
juce::Colour peakColour(float dbtp) {
    if (dbtp >= -0.1f)
        return magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_RED);
    if (dbtp >= -3.0f)
        return magda::DarkTheme::getColour(magda::DarkTheme::STATUS_WARNING);
    return magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_POSITIVE);
}

}  // namespace

LevelsUI::LevelsUI() {
    setOpaque(false);
}

LevelsUI::~LevelsUI() {
    stopTimer();
    if (telemetry_ != nullptr)
        telemetry_->setActive(false);
}

void LevelsUI::setTelemetrySource(std::shared_ptr<LevelsTelemetrySource> telemetry) {
    if (telemetry_ == telemetry)
        return;

    if (telemetry_ != nullptr)
        telemetry_->setActive(false);
    telemetry_ = std::move(telemetry);
    updateActiveState();
    repaint();
}

void LevelsUI::visibilityChanged() {
    updateActiveState();
}

void LevelsUI::parentHierarchyChanged() {
    updateActiveState();
}

void LevelsUI::updateActiveState() {
    const bool live = isVisible() && getParentComponent() != nullptr;
    if (telemetry_ != nullptr)
        telemetry_->setActive(live);
    if (live && !isTimerRunning())
        startTimerHz(kTimerHz);
    else if (!live && isTimerRunning())
        stopTimer();
}

void LevelsUI::timerCallback() {
    if (!isShowing() || telemetry_ == nullptr)
        return;
    snapshot_ = telemetry_->snapshot();
    repaint();
}

void LevelsUI::paint(juce::Graphics& g) {
    using DT = magda::DarkTheme;
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(DT::getColour(DT::BACKGROUND));
    g.fillRoundedRectangle(bounds, 4.0f);

    const auto& s = snapshot_;
    const auto dim = DT::getColour(DT::TEXT_DIM);
    const auto primary = DT::getColour(DT::TEXT_PRIMARY);

    // Three columns: loudness | dynamics/peak | stereo.
    auto area = bounds.reduced(8.0f, 6.0f);
    const float colW = area.getWidth() / 3.0f;
    auto loudCol = area.removeFromLeft(colW).reduced(4.0f, 0.0f);
    auto dynCol = area.removeFromLeft(colW).reduced(4.0f, 0.0f);
    auto stereoCol = area.reduced(4.0f, 0.0f);

    // Helper: draw a labelled value row, returning the consumed band.
    auto valueRow = [&](juce::Rectangle<float>& col, const juce::String& label,
                        const juce::String& value, const juce::String& unit,
                        juce::Colour valueColour, float rowH) {
        auto row = col.removeFromTop(rowH);
        g.setColour(dim);
        g.setFont(10.0f);
        g.drawText(label, row.removeFromTop(12.0f), juce::Justification::topLeft);
        g.setColour(valueColour);
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
        auto valueArea = row.removeFromTop(20.0f);
        g.drawText(value, valueArea.withTrimmedRight(unit.isEmpty() ? 0.0f : 26.0f),
                   juce::Justification::bottomLeft);
        if (!unit.isEmpty()) {
            g.setColour(dim);
            g.setFont(9.0f);
            g.drawText(unit, valueArea.removeFromRight(26.0f), juce::Justification::bottomLeft);
        }
    };

    const float rowH = juce::jmax(28.0f, loudCol.getHeight() / 3.0f);

    // Column 1: loudness.
    g.setColour(DT::getColour(DT::ACCENT_INFO));
    g.setFont(9.0f);
    g.drawText("LOUDNESS", loudCol.removeFromTop(11.0f), juce::Justification::topLeft);
    valueRow(loudCol, "Integrated", fmtLufs(s.integratedLufs), "LUFS", primary, rowH);
    valueRow(loudCol, "Short-term", fmtLufs(s.shortTermLufs), "LUFS", primary, rowH * 0.8f);
    valueRow(loudCol, "Momentary", fmtLufs(s.momentaryLufs), "LUFS", dim.brighter(0.4f),
             rowH * 0.8f);

    // Column 2: peak + dynamics.
    g.setColour(DT::getColour(DT::ACCENT_INFO));
    g.setFont(9.0f);
    g.drawText("PEAK / DYNAMICS", dynCol.removeFromTop(11.0f), juce::Justification::topLeft);
    const float tp = s.truePeakValid ? s.truePeakDb : s.samplePeakDb;
    valueRow(dynCol, s.truePeakValid ? "True peak" : "Sample peak", fmtDb(tp), "dB",
             s.valid ? peakColour(tp) : primary, rowH);
    valueRow(dynCol, "PLR", fmtLu(s.plr), "LU", primary, rowH * 0.8f);
    valueRow(dynCol, "PSR", fmtLu(s.psr), "LU", dim.brighter(0.4f), rowH * 0.8f);

    // Column 3: stereo (correlation bar + width).
    g.setColour(DT::getColour(DT::ACCENT_INFO));
    g.setFont(9.0f);
    g.drawText("STEREO", stereoCol.removeFromTop(11.0f), juce::Justification::topLeft);

    // Correlation meter: -1 .. +1, centre line.
    g.setColour(dim);
    g.setFont(10.0f);
    g.drawText("Correlation", stereoCol.removeFromTop(12.0f), juce::Justification::topLeft);
    auto corrBar = stereoCol.removeFromTop(10.0f);
    g.setColour(DT::getColour(DT::SURFACE));
    g.fillRoundedRectangle(corrBar, 2.0f);
    const float corr = juce::jlimit(-1.0f, 1.0f, s.correlation);
    const float midX = corrBar.getCentreX();
    auto fill =
        corr >= 0.0f
            ? juce::Rectangle<float>(midX, corrBar.getY(), corr * corrBar.getWidth() * 0.5f,
                                     corrBar.getHeight())
            : juce::Rectangle<float>(midX + corr * corrBar.getWidth() * 0.5f, corrBar.getY(),
                                     -corr * corrBar.getWidth() * 0.5f, corrBar.getHeight());
    g.setColour(corr < 0.0f ? DT::getColour(DT::STATUS_WARNING)
                            : DT::getColour(DT::ACCENT_POSITIVE));
    g.fillRoundedRectangle(fill, 2.0f);
    g.setColour(dim.withAlpha(0.6f));
    g.drawVerticalLine(static_cast<int>(midX), corrBar.getY(), corrBar.getBottom());
    g.setColour(primary);
    g.setFont(11.0f);
    g.drawText(juce::String(corr, 2), stereoCol.removeFromTop(14.0f),
               juce::Justification::topRight);

    // Width 0..1.
    g.setColour(dim);
    g.setFont(10.0f);
    auto widthLabelRow = stereoCol.removeFromTop(12.0f);
    g.drawText("Width", widthLabelRow, juce::Justification::topLeft);
    g.setColour(primary);
    g.setFont(11.0f);
    g.drawText(juce::String(s.width, 2), widthLabelRow, juce::Justification::topRight);
    auto widthBar = stereoCol.removeFromTop(8.0f);
    g.setColour(DT::getColour(DT::SURFACE));
    g.fillRoundedRectangle(widthBar, 2.0f);
    g.setColour(DT::getColour(DT::ACCENT_PRIMARY_SOFT));
    g.fillRoundedRectangle(
        widthBar.withWidth(juce::jlimit(0.0f, 1.0f, s.width) * widthBar.getWidth()), 2.0f);

    if (!s.valid) {
        g.setColour(dim.withAlpha(0.7f));
        g.setFont(11.0f);
        g.drawText("no signal", bounds, juce::Justification::centredBottom);
    }
}

}  // namespace magda::daw::ui
