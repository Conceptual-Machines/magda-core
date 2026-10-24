#include "custom_ui/SpectralFxUI.hpp"

#include <cmath>

#include "audio/SpectrogramCache.hpp"
#include "devices/spectral/SpectralFx.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

using devices::spectral::SpectralFx;
using devices::spectral::SpectrogramTelemetry;

const ThemedColour kPanel{ActiveTheme::INSTRUMENT_PANEL};
const ThemedColour kBorder{ActiveTheme::INSTRUMENT_BORDER};
const ThemedColour kText{ActiveTheme::INSTRUMENT_TEXT};
const ThemedColour kDim{ActiveTheme::INSTRUMENT_TEXT_DIM};
const juce::Colour kAccent{0xff45c8d0};

constexpr int kBands = SpectrogramTelemetry::kColumnBands;

struct ControlSpec {
    int param;
    const char* name;
};
constexpr std::array<ControlSpec, 5> kControls{{{SpectralFx::kGateParamIndex, "Gate"},
                                                {SpectralFx::kContrastParamIndex, "Contrast"},
                                                {SpectralFx::kShiftParamIndex, "Shift"},
                                                {SpectralFx::kScrambleParamIndex, "Scramble"},
                                                {SpectralFx::kOutputParamIndex, "Output"}}};

// paint() reads these two by position.
static_assert(kControls[SpectralFx::kGateParamIndex].param == SpectralFx::kGateParamIndex);
static_assert(kControls[SpectralFx::kShiftParamIndex].param == SpectralFx::kShiftParamIndex);

bool isEffect(int param) {
    return param <= SpectralFx::kScrambleParamIndex;
}

/// Magma, as the audio editor's spectrogram draws it, indexed by dB above the floor.
const std::array<juce::PixelARGB, 256>& colourTable() {
    static const auto table = [] {
        std::array<juce::PixelARGB, 256> t;
        for (std::size_t i = 0; i < t.size(); ++i)
            t[i] = magda::SpectrogramCache::colourAt(static_cast<float>(i) / 255.0f).getPixelARGB();
        return t;
    }();
    return table;
}

juce::PixelARGB colourForDb(float db) {
    const float v = 1.0f - db / SpectrogramTelemetry::kFloorDb;
    return colourTable()[static_cast<std::size_t>(
        juce::jlimit(0, 255, juce::roundToInt(v * 255.0f)))];
}

/// Height above the bottom of a frequency on the device's log band axis, 0..1.
float frequencyPosition(float hz) {
    return std::log(hz / SpectrogramTelemetry::kMinHz) /
           std::log(SpectrogramTelemetry::kMaxHz / SpectrogramTelemetry::kMinHz);
}

}  // namespace

SpectralFxUI::SpectralFxUI()
    : history_(juce::Image::RGB, kHistoryColumns, kBands, true),
      columns_(static_cast<size_t>(kHistoryColumns * kBands)) {
    history_.clear(history_.getBounds(), juce::Colour(colourForDb(SpectrogramTelemetry::kFloorDb)));

    static_assert(kControls.size() == kControlCount);
    for (size_t i = 0; i < kControls.size(); ++i) {
        const int index = kControls[i].param;
        auto& c = controls_[i];
        c.label = std::make_unique<juce::Label>();
        c.label->setText(kControls[i].name, juce::dontSendNotification);
        c.label->setFont(FontManager::getInstance().getUIFont(11.0f));
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>(TextSlider::Format::Decimal);
        c.slider->setParamIndex(index);
        // The shared dB format reads -60 and below as -inf, which suits a fader, not a threshold.
        if (index == SpectralFx::kGateParamIndex)
            c.slider->setValueFormatter([](double v) {
                return v <= SpectralFx::kGateOffDb + 0.05 ? juce::String("Off")
                                                          : juce::String(v, 1) + " dB";
            });
        c.slider->onValueChanged = [this, index](double v) {
            if (onParameterChanged)
                onParameterChanged(index, static_cast<float>(v));
            repaint(stripArea_);
        };
        addAndMakeVisible(*c.slider);
    }
    applyColours();
    startTimerHz(30);
}

SpectralFxUI::~SpectralFxUI() {
    stopTimer();
}

void SpectralFxUI::applyColours() {
    for (size_t i = 0; i < kControls.size(); ++i) {
        auto& c = controls_[i];
        const bool effect = isEffect(kControls[i].param);
        const auto colour = effect ? kAccent : juce::Colour(kText);
        c.label->setColour(juce::Label::textColourId, effect ? kAccent : juce::Colour(kDim));
        c.slider->setTextColour(colour);
    }
}

void SpectralFxUI::lookAndFeelChanged() {
    applyColours();
    repaint();
}

void SpectralFxUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (static_cast<int>(params.size()) < SpectralFx::kParamCount)
        return;
    for (size_t i = 0; i < kControls.size(); ++i) {
        auto& slider = *controls_[i].slider;
        const auto& info = params[static_cast<size_t>(kControls[i].param)];
        if (!configured_)
            slider.setParameterInfo(info);
        slider.setValue(info.currentValue, juce::dontSendNotification);
    }
    configured_ = true;
    repaint();
}

std::vector<LinkableTextSlider*> SpectralFxUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> out;
    out.reserve(controls_.size());
    for (auto& c : controls_)
        out.push_back(c.slider.get());
    return out;
}

void SpectralFxUI::setTelemetrySource(std::shared_ptr<SpectralFxTelemetrySource> telemetry) {
    telemetry_ = std::move(telemetry);
    readPosition_ = telemetry_ != nullptr ? telemetry_->writePosition() : 0;
}

void SpectralFxUI::timerCallback() {
    pullColumns();
}

void SpectralFxUI::pullColumns() {
    if (telemetry_ == nullptr || !isShowing())
        return;

    const auto position = telemetry_->writePosition();
    // A rebuilt device restarts its ring.
    if (position < readPosition_)
        readPosition_ = 0;
    const auto fresh = static_cast<int>(
        std::min<std::size_t>((position - readPosition_) / kBands, kHistoryColumns));
    if (fresh <= 0)
        return;

    readPosition_ = telemetry_->readLatest(columns_.data(), fresh * kBands);
    history_.moveImageSection(0, 0, fresh, 0, kHistoryColumns - fresh, kBands);

    juce::Image::BitmapData pixels(history_, juce::Image::BitmapData::writeOnly);
    for (int c = 0; c < fresh; ++c) {
        const float* column = columns_.data() + static_cast<size_t>(c) * kBands;
        const int x = kHistoryColumns - fresh + c;
        for (int band = 0; band < kBands; ++band)
            pixels.setPixelColour(x, kBands - 1 - band, juce::Colour(colourForDb(column[band])));
    }
    repaint(plotArea_);
}

void SpectralFxUI::paint(juce::Graphics& g) {
    g.setColour(kPanel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    paintSpectrogram(g);
    paintLevelStrip(g);
}

void SpectralFxUI::paintSpectrogram(juce::Graphics& g) {
    const auto plot = plotArea_.toFloat();
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImage(history_, plot);

    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    for (const auto& [hz, label] : {std::pair{100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}}) {
        const float y = plot.getBottom() - frequencyPosition(hz) * plot.getHeight();
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawText(label, juce::Rectangle<float>(plot.getX() + 4.0f, y - 12.0f, 40.0f, 11.0f),
                   juce::Justification::bottomLeft);
    }

    const double shift = controls_[SpectralFx::kShiftParamIndex].slider->getValue();
    if (std::abs(shift) >= 0.5) {
        g.setColour(kAccent);
        g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
        g.drawText((shift > 0 ? "+" : "") + juce::String(juce::roundToInt(shift)) + " Hz",
                   plotArea_.reduced(6, 4), juce::Justification::topRight);
    }

    g.setColour(kBorder);
    g.drawRect(plotArea_, 1);
}

void SpectralFxUI::paintLevelStrip(juce::Graphics& g) {
    const auto strip = stripArea_.toFloat();
    for (int y = stripArea_.getY(); y < stripArea_.getBottom(); ++y) {
        const float db = SpectrogramTelemetry::kFloorDb *
                         (static_cast<float>(y - stripArea_.getY()) / strip.getHeight());
        g.setColour(juce::Colour(colourForDb(db)));
        g.drawHorizontalLine(y, strip.getX(), strip.getRight());
    }

    const double gateDb = controls_[SpectralFx::kGateParamIndex].slider->getValue();
    if (gateDb > SpectralFx::kGateOffDb) {
        const float y = strip.getY() + static_cast<float>(gateDb / SpectrogramTelemetry::kFloorDb) *
                                           strip.getHeight();
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRect(strip.withTop(y));
        g.setColour(kAccent);
        g.fillRect(
            juce::Rectangle<float>(strip.getX() - 3.0f, y - 1.0f, strip.getWidth() + 3.0f, 2.0f));
    }
    g.setColour(kBorder);
    g.drawRect(stripArea_, 1);
}

void SpectralFxUI::resized() {
    auto r = getLocalBounds().reduced(8);

    auto row = r.removeFromBottom(38);
    r.removeFromBottom(8);
    const int cellW = row.getWidth() / kControlCount;
    for (auto& c : controls_) {
        auto cell = row.removeFromLeft(cellW).reduced(4, 0);
        c.label->setBounds(cell.removeFromTop(13));
        c.slider->setBounds(cell.removeFromTop(24));
    }

    stripArea_ = r.removeFromRight(10);
    r.removeFromRight(6);
    plotArea_ = r;
}

}  // namespace magda::daw::ui
