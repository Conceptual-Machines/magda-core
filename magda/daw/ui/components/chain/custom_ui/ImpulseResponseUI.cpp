// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/ImpulseResponseUI.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

#include "BinaryData.h"
#include "audio/plugins/MagdaConvolutionPlugin.hpp"
#include "custom_ui/SamplerTiles.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/utils/AudioFileTypes.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kPanelGap = 8;
constexpr int kPanelPadX = 12;
constexpr int kPanelPadY = 8;
/// Less under the last row than over the title: the slot's edge frames it (#3144).
constexpr int kPanelPadBottom = 4;
constexpr int kTitleH = 18;
constexpr int kTileGap = 6;
constexpr int kTileH = 42;
constexpr int kCurveH = 44;
constexpr int kPeakColumns = 400;
/// The waveform's floor: an IR decays exponentially, so it is drawn in dB to show the tail.
constexpr float kFloorDb = -60.0f;
/// The response curve's range; Q up to 14 peaks near +23 dB.
constexpr float kCurveMinDb = -36.0f;
constexpr float kCurveMaxDb = 24.0f;
constexpr double kCurveRate = 48000.0;

juce::Colour colour(ColourRole role) {
    return ActiveTheme::getColour(role);
}

juce::String formatHz(double value) {
    if (value >= 1000.0)
        return juce::String(value / 1000.0, 1) + " kHz";
    return juce::String(juce::roundToInt(value)) + " Hz";
}

double parseHz(const juce::String& text) {
    auto t = text.trim().toLowerCase();
    if (t.contains("khz"))
        return t.replace("khz", "").trim().getDoubleValue() * 1000.0;
    return t.replace("hz", "").trim().getDoubleValue();
}

void paintPanel(juce::Graphics& g, juce::Rectangle<int> panel) {
    g.setColour(colour(ActiveTheme::DEVICE_PANEL));
    g.fillRoundedRectangle(panel.toFloat(), 6.0f);
    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void paintTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                const juce::String& detail = {}) {
    g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(FontManager::getInstance().getUIFontBold(10.5f));
    g.drawText(title, area, juce::Justification::topLeft, true);
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    g.setFont(FontManager::getInstance().getUIFont(9.5f));
    g.drawText(detail, area, juce::Justification::topRight, true);
}

/// A panel's content area under its title, padded less at the slot's edge below.
juce::Rectangle<int> panelBody(juce::Rectangle<int> panel, juce::Rectangle<int>& title,
                               bool atBottom) {
    auto a = panel.reduced(kPanelPadX, kPanelPadY);
    if (atBottom)
        a = a.withTrimmedBottom(kPanelPadBottom - kPanelPadY);
    title = a.removeFromTop(kTitleH);
    return a;
}
}  // namespace

ImpulseResponseUI::ImpulseResponseUI() {
    loadButton_ = std::make_unique<magda::SvgButton>("Load IR", BinaryData::folderopen_svg,
                                                     BinaryData::folderopen_svgSize);
    loadButton_->onClick = [this]() {
        if (onLoadIRRequested)
            onLoadIRRequested();
    };
    addAndMakeVisible(*loadButton_);

    setupSlider(gain_, "Gain");
    setupSlider(lowCut_, "Low Cut");
    setupSlider(highCut_, "High Cut");
    setupSlider(mix_, "Mix");
    setupSlider(filterQ_, "Q");

    gain_.slider.setRange(-12.0, 6.0, 0.1);
    gain_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(value));
    };

    // The cutoffs drag on a log scale.
    for (auto* cut : {&lowCut_, &highCut_}) {
        cut->slider.setRange(10.0, 20000.0, 1.0);
        cut->slider.setSkewForCentre(1000.0);
        cut->slider.setValueFormatter(formatHz);
        cut->slider.setValueParser(parseHz);
    }
    lowCut_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(value));
        repaint(curveArea_);
    };
    highCut_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
        repaint(curveArea_);
    };

    mix_.slider.setRange(0.0, 1.0, 0.001);
    mix_.slider.setValueFormatter(
        [](double value) -> juce::String { return juce::String(value * 100.0, 1) + "%"; });
    mix_.slider.setValueParser([](const juce::String& text) -> double {
        return text.trim().replace("%", "").trim().getDoubleValue() / 100.0;
    });
    mix_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(3, static_cast<float>(value));
    };

    filterQ_.slider.setRange(0.1, 14.0, 0.01);
    filterQ_.slider.setValueFormatter([](double value) -> juce::String { return {value, 2}; });
    filterQ_.slider.setValueParser(
        [](const juce::String& text) -> double { return text.trim().getDoubleValue(); });
    filterQ_.slider.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(4, static_cast<float>(value));
        repaint(curveArea_);
    };
}

void ImpulseResponseUI::setupSlider(SliderWithLabel& s, const juce::String& labelText) {
    s.label.setText(labelText, juce::dontSendNotification);
    s.label.setColour(juce::Label::textColourId, colour(ActiveTheme::DEVICE_DIM));
    s.label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(s.label);
    sampler_tiles::styleValue(s.slider);
    addAndMakeVisible(s.slider);
}

void ImpulseResponseUI::lookAndFeelChanged() {
    for (auto* s : {&gain_, &lowCut_, &highCut_, &mix_, &filterQ_}) {
        s->label.setColour(juce::Label::textColourId, colour(ActiveTheme::DEVICE_DIM));
        s->slider.setTextColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    }
    repaint();
}

void ImpulseResponseUI::setIRName(const juce::String& name) {
    irName_ = name;
    repaint(irPanel_);
}

void ImpulseResponseUI::setImpulseResponse(const juce::MemoryBlock& encoded) {
    if (encoded == encodedIR_)
        return;
    encodedIR_ = encoded;
    peaks_.clear();
    irSeconds_ = 0.0;
    irChannels_ = 0;

    juce::AudioBuffer<float> ir;
    double rate = 0.0;
    if (daw::audio::MagdaConvolutionPlugin::decodeImpulseResponse(encoded, ir, rate)) {
        irSeconds_ = rate > 0.0 ? ir.getNumSamples() / rate : 0.0;
        irChannels_ = ir.getNumChannels();
        const int n = ir.getNumSamples();
        const int columns = juce::jmin(kPeakColumns, n);
        peaks_.assign(static_cast<size_t>(columns), 0.0f);
        for (int col = 0; col < columns; ++col) {
            const int start = static_cast<int>(static_cast<juce::int64>(col) * n / columns);
            const int end = static_cast<int>(static_cast<juce::int64>(col + 1) * n / columns);
            for (int ch = 0; ch < irChannels_; ++ch)
                peaks_[static_cast<size_t>(col)] =
                    juce::jmax(peaks_[static_cast<size_t>(col)],
                               ir.getMagnitude(ch, start, juce::jmax(1, end - start)));
        }
        if (const float loudest = *std::ranges::max_element(peaks_); loudest > 0.0f)
            for (auto& p : peaks_)
                p /= loudest;
    }
    repaint(irPanel_);
}

void ImpulseResponseUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (params.size() > 0)
        gain_.slider.setValue(params[0].currentValue, juce::dontSendNotification);
    if (params.size() > 1)
        lowCut_.slider.setValue(params[1].currentValue, juce::dontSendNotification);
    if (params.size() > 2)
        highCut_.slider.setValue(params[2].currentValue, juce::dontSendNotification);
    if (params.size() > 3)
        mix_.slider.setValue(params[3].currentValue, juce::dontSendNotification);
    if (params.size() > 4)
        filterQ_.slider.setValue(params[4].currentValue, juce::dontSendNotification);
    repaint(curveArea_);
}

void ImpulseResponseUI::paint(juce::Graphics& g) {
    paintPanel(g, irPanel_);
    paintPanel(g, filterPanel_);
    paintPanel(g, outputPanel_);

    const auto detail = irChannels_ == 0 ? juce::String()
                                         : juce::String(irSeconds_, 2) + " s" +
                                               juce::String::fromUTF8("  \xc2\xb7  ") +
                                               (irChannels_ > 1 ? "stereo" : "mono");
    paintTitle(g, irTitle_.withTrimmedRight(loadButton_->getWidth() + 6),
               irName_.isEmpty() ? juce::String("IMPULSE RESPONSE") : irName_, detail);
    paintTitle(g, filterTitle_, "FILTER");
    paintTitle(g, outputTitle_, "OUTPUT");

    paintWaveform(g);
    paintResponse(g);

    for (const auto* s : {&lowCut_, &highCut_, &filterQ_, &gain_, &mix_})
        if (!s->tile.isEmpty())
            sampler_tiles::paint(g, s->tile, 4.0f);
}

void ImpulseResponseUI::paintWaveform(juce::Graphics& g) const {
    if (waveArea_.isEmpty())
        return;
    const auto field = waveArea_.toFloat();
    g.setColour(colour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(field, 4.0f);
    g.setColour(dragOver_ ? colour(ActiveTheme::DEVICE_BLUE)
                          : colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(field.reduced(0.5f), 4.0f, dragOver_ ? 1.5f : 1.0f);

    if (peaks_.empty()) {
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("Drop an IR or click to load", waveArea_, juce::Justification::centred);
        return;
    }

    // Mirrored about the centre line, each column's peak in dB above the floor.
    const auto plot = field.reduced(6.0f, 6.0f);
    const float mid = plot.getCentreY();
    const float half = plot.getHeight() * 0.5f;
    const int width = juce::jmax(1, juce::roundToInt(plot.getWidth()));
    juce::Path wave;
    wave.startNewSubPath(plot.getX(), mid);
    std::vector<float> heights(static_cast<size_t>(width));
    for (int x = 0; x < width; ++x) {
        const auto col = static_cast<size_t>(x * static_cast<int>(peaks_.size()) / width);
        const float db = juce::Decibels::gainToDecibels(peaks_[col], kFloorDb);
        heights[static_cast<size_t>(x)] = half * (db - kFloorDb) / -kFloorDb;
        wave.lineTo(plot.getX() + static_cast<float>(x), mid - heights[static_cast<size_t>(x)]);
    }
    for (int x = width - 1; x >= 0; --x)
        wave.lineTo(plot.getX() + static_cast<float>(x), mid + heights[static_cast<size_t>(x)]);
    wave.closeSubPath();

    const auto blue = colour(ActiveTheme::DEVICE_BLUE);
    g.setColour(blue.withAlpha(0.35f));
    g.fillPath(wave);
    g.setColour(blue);
    g.strokePath(wave, juce::PathStrokeType(1.0f));
}

void ImpulseResponseUI::paintResponse(juce::Graphics& g) const {
    if (curveArea_.isEmpty())
        return;
    const auto field = curveArea_.toFloat();
    g.setColour(colour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(field, 4.0f);
    g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(field.reduced(0.5f), 4.0f, 1.0f);

    const auto plot = field.reduced(4.0f, 3.0f);
    const auto xFor = [&plot](double hz) {
        return plot.getX() +
               plot.getWidth() * static_cast<float>(std::log(hz / 20.0) / std::log(1000.0));
    };
    const auto yFor = [&plot](float db) {
        const float t = (juce::jlimit(kCurveMinDb, kCurveMaxDb, db) - kCurveMinDb) /
                        (kCurveMaxDb - kCurveMinDb);
        return plot.getBottom() - t * plot.getHeight();
    };

    g.setColour(colour(ActiveTheme::DEVICE_LINE).withAlpha(0.5f));
    for (const double hz : {100.0, 1000.0, 10000.0})
        g.drawVerticalLine(juce::roundToInt(xFor(hz)), plot.getY(), plot.getBottom());
    g.drawHorizontalLine(juce::roundToInt(yFor(0.0f)), plot.getX(), plot.getRight());

    // The same pair of biquads the device runs: high pass at Low Cut, low pass at High Cut.
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    const auto q = static_cast<float>(filterQ_.slider.getValue());
    const auto highPass =
        Coefficients::makeHighPass(kCurveRate, static_cast<float>(lowCut_.slider.getValue()), q);
    const auto lowPass =
        Coefficients::makeLowPass(kCurveRate, static_cast<float>(highCut_.slider.getValue()), q);

    juce::Path curve;
    const int width = juce::jmax(2, juce::roundToInt(plot.getWidth()));
    for (int x = 0; x < width; ++x) {
        const double hz = 20.0 * std::pow(1000.0, static_cast<double>(x) / (width - 1));
        const double gain = highPass->getMagnitudeForFrequency(hz, kCurveRate) *
                            lowPass->getMagnitudeForFrequency(hz, kCurveRate);
        const float y = yFor(juce::Decibels::gainToDecibels(static_cast<float>(gain), -100.0f));
        if (x == 0)
            curve.startNewSubPath(plot.getX(), y);
        else
            curve.lineTo(plot.getX() + static_cast<float>(x), y);
    }
    auto fill = curve;
    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.lineTo(plot.getX(), plot.getBottom());
    fill.closeSubPath();

    const auto blue = colour(ActiveTheme::DEVICE_BLUE);
    g.setColour(blue.withAlpha(0.2f));
    g.fillPath(fill);
    g.setColour(blue);
    g.strokePath(curve, juce::PathStrokeType(1.5f));
}

void ImpulseResponseUI::resized() {
    // Padded at the sides only: the header above and the slot's edge below frame the panels.
    auto r = getLocalBounds().reduced(kPanelGap, 0);

    const int bottomH = 2 * kPanelPadY + kTitleH + kCurveH + kTileGap + kTileH;
    auto bottom = r.removeFromBottom(juce::jmin(bottomH, r.getHeight() / 2));
    r.removeFromBottom(kPanelGap);
    irPanel_ = r;
    filterPanel_ = bottom.removeFromLeft(bottom.getWidth() * 64 / 100);
    bottom.removeFromLeft(kPanelGap);
    outputPanel_ = bottom;

    waveArea_ = panelBody(irPanel_, irTitle_, false);
    loadButton_->setBounds(irTitle_.removeFromRight(16).withHeight(16).translated(0, -2));

    // FILTER: the response over its three tiles.
    {
        auto a = panelBody(filterPanel_, filterTitle_, true);
        auto row = a.removeFromBottom(juce::jmin(kTileH, a.getHeight()));
        a.removeFromBottom(kTileGap);
        curveArea_ = a;
        const int cellW = row.getWidth() / 3;
        for (auto* s : {&lowCut_, &highCut_, &filterQ_}) {
            s->tile = row.removeFromLeft(cellW).reduced(kTileGap / 2, 0);
            sampler_tiles::layoutTile(s->label, s->slider, s->tile);
        }
    }

    // OUTPUT: Gain over Mix.
    {
        auto a = panelBody(outputPanel_, outputTitle_, true);
        const int tileH = juce::jmin(kTileH, (a.getHeight() - kTileGap) / 2);
        a.removeFromTop(a.getHeight() - 2 * tileH - kTileGap);
        for (auto* s : {&gain_, &mix_}) {
            s->tile = a.removeFromTop(tileH);
            sampler_tiles::layoutTile(s->label, s->slider, s->tile);
            a.removeFromTop(kTileGap);
        }
    }
}

void ImpulseResponseUI::mouseDown(const juce::MouseEvent& e) {
    if (waveArea_.contains(e.getPosition()) && onLoadIRRequested)
        onLoadIRRequested();
}

void ImpulseResponseUI::mouseMove(const juce::MouseEvent& e) {
    setMouseCursor(waveArea_.contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                       : juce::MouseCursor::NormalCursor);
}

bool ImpulseResponseUI::isInterestedInFileDrag(const juce::StringArray& files) {
    return std::ranges::any_of(files, isAudioFile);
}

void ImpulseResponseUI::fileDragEnter(const juce::StringArray& /*files*/, int /*x*/, int /*y*/) {
    dragOver_ = true;
    repaint(waveArea_);
}

void ImpulseResponseUI::fileDragExit(const juce::StringArray& /*files*/) {
    dragOver_ = false;
    repaint(waveArea_);
}

void ImpulseResponseUI::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) {
    dragOver_ = false;
    repaint(waveArea_);
    for (const auto& f : files) {
        juce::File file(f);
        if (file.existsAsFile()) {
            if (onFileDropped)
                onFileDropped(file);
            return;
        }
    }
}

// IRs dragged from MAGDA's own browser come through as an internal
// {type:"files"} drag rather than an OS file drag, so route them back into the
// FileDragAndDropTarget handlers above. See InternalFileDrag.hpp.
bool ImpulseResponseUI::isInterestedInDragSource(const SourceDetails& details) {
    return magda::dnd::acceptsFilesDrag(*this, details);
}

void ImpulseResponseUI::itemDragEnter(const SourceDetails& details) {
    magda::dnd::forwardFilesDragEnter(*this, details);
}

void ImpulseResponseUI::itemDragMove(const SourceDetails& details) {
    magda::dnd::forwardFilesDragMove(*this, details);
}

void ImpulseResponseUI::itemDragExit(const SourceDetails& details) {
    magda::dnd::forwardFilesDragExit(*this, details);
}

void ImpulseResponseUI::itemDropped(const SourceDetails& details) {
    magda::dnd::forwardFilesDrop(*this, details);
}

std::vector<LinkableTextSlider*> ImpulseResponseUI::getLinkableSliders() {
    return {&gain_.slider, &lowCut_.slider, &highCut_.slider, &mix_.slider, &filterQ_.slider};
}

}  // namespace magda::daw::ui
