#include "SpectrumAnalyzerUI.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "AnalyzerColours.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kControlRowH = 22;
constexpr float kSlopeOptions[] = {0.0f, 3.0f, 4.5f, 6.0f};  // dB/oct
constexpr float kSpeedOptions[] = {0.15f, 0.4f, 0.8f};       // smoothing (Slow/Med/Fast)

template <typename Range> int nearestId(const Range& options, float value) {
    int bestId = 1;
    float bestErr = std::numeric_limits<float>::max();
    int i = 0;
    for (float opt : options) {
        const float err = std::abs(opt - value);
        if (err < bestErr) {
            bestErr = err;
            bestId = i + 1;
        }
        ++i;
    }
    return bestId;
}
}  // namespace

SpectrumAnalyzerUI::SpectrumAnalyzerUI() {
    auto styleLabel = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(FontManager::getInstance().getUIFont(10.0f));
        l.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        l.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(l);
    };
    auto styleCombo = [this](juce::ComboBox& c) {
        c.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
        c.setColour(juce::ComboBox::backgroundColourId,
                    DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
        c.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        c.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(c);
    };

    styleLabel(fftLabel_, "FFT");
    fftCombo_.addItem("2048", 1);
    fftCombo_.addItem("4096", 2);
    fftCombo_.onChange = [this] {
        const int order = fftCombo_.getSelectedId() == 2 ? 12 : 11;
        if (plugin_ != nullptr)
            plugin_->setFftOrder(order);
        rebuildFft(order);
    };
    styleCombo(fftCombo_);

    styleLabel(slopeLabel_, "Slope");
    slopeCombo_.addItem("0 dB/oct", 1);
    slopeCombo_.addItem("3 dB/oct", 2);
    slopeCombo_.addItem("4.5 dB/oct", 3);
    slopeCombo_.addItem("6 dB/oct", 4);
    slopeCombo_.onChange = [this] {
        const int idx = slopeCombo_.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(std::size(kSlopeOptions))) {
            slopeDbPerOct_ = kSlopeOptions[static_cast<size_t>(idx)];
            if (plugin_ != nullptr)
                plugin_->setSlopeDbPerOct(slopeDbPerOct_);
        }
    };
    styleCombo(slopeCombo_);

    styleLabel(speedLabel_, "Time");
    speedCombo_.addItem("Slow", 1);
    speedCombo_.addItem("Med", 2);
    speedCombo_.addItem("Fast", 3);
    speedCombo_.onChange = [this] {
        const int idx = speedCombo_.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(std::size(kSpeedOptions))) {
            smoothing_ = kSpeedOptions[static_cast<size_t>(idx)];
            if (plugin_ != nullptr)
                plugin_->setSmoothing(smoothing_);
        }
    };
    styleCombo(speedCombo_);

    styleLabel(colourLabel_, "Color");
    for (int i = 0; i < kAnalyzerColourCount; ++i)
        colourCombo_.addItem(kAnalyzerColourNames[i], i + 1);
    colourCombo_.onChange = [this] {
        if (plugin_ != nullptr)
            plugin_->setTraceColourIndex(colourCombo_.getSelectedId() - 1);
    };
    styleCombo(colourCombo_);

    rebuildFft(11);
    startTimerHz(30);
}

SpectrumAnalyzerUI::~SpectrumAnalyzerUI() {
    stopTimer();
    // Clear the shared LookAndFeel before the combos are destroyed.
    fftCombo_.setLookAndFeel(nullptr);
    slopeCombo_.setLookAndFeel(nullptr);
    speedCombo_.setLookAndFeel(nullptr);
    colourCombo_.setLookAndFeel(nullptr);
}

void SpectrumAnalyzerUI::rebuildFft(int order) {
    fftOrder_ = juce::jlimit(11, 12, order);
    fftSize_ = 1 << fftOrder_;
    numBins_ = fftSize_ / 2;
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder_);
    window_ = std::make_unique<juce::dsp::WindowingFunction<float>>(
        static_cast<size_t>(fftSize_), juce::dsp::WindowingFunction<float>::hann);
    readBuf_.assign(static_cast<size_t>(fftSize_), 0.0f);
    fftData_.assign(static_cast<size_t>(fftSize_) * 2, 0.0f);
    smoothedDb_.assign(static_cast<size_t>(numBins_), kMinDb);
    peakDb_.assign(static_cast<size_t>(numBins_), kMinDb);
}

void SpectrumAnalyzerUI::setPlugin(daw::audio::SpectrumAnalyzerPlugin* plugin) {
    plugin_ = plugin;
    if (plugin_ == nullptr)
        return;

    slopeDbPerOct_ = plugin_->getSlopeDbPerOct();
    smoothing_ = plugin_->getSmoothing();
    fftCombo_.setSelectedId(plugin_->getFftOrder() >= 12 ? 2 : 1, juce::dontSendNotification);
    slopeCombo_.setSelectedId(nearestId(kSlopeOptions, slopeDbPerOct_), juce::dontSendNotification);
    speedCombo_.setSelectedId(nearestId(kSpeedOptions, smoothing_), juce::dontSendNotification);
    colourCombo_.setSelectedId(plugin_->getTraceColourIndex() + 1, juce::dontSendNotification);
    rebuildFft(plugin_->getFftOrder());
}

void SpectrumAnalyzerUI::resized() {
    auto controls = getLocalBounds().removeFromBottom(kControlRowH);
    auto cell = [&controls](int labelW, int comboW) {
        controls.removeFromLeft(4);
        auto label = controls.removeFromLeft(labelW);
        auto combo = controls.removeFromLeft(comboW);
        return std::pair<juce::Rectangle<int>, juce::Rectangle<int>>{label, combo};
    };
    auto [fftL, fftC] = cell(34, 70);
    fftLabel_.setBounds(fftL);
    fftCombo_.setBounds(fftC.reduced(2, 1));
    auto [slL, slC] = cell(40, 96);
    slopeLabel_.setBounds(slL);
    slopeCombo_.setBounds(slC.reduced(2, 1));
    auto [spL, spC] = cell(34, 64);
    speedLabel_.setBounds(spL);
    speedCombo_.setBounds(spC.reduced(2, 1));
    auto [colL, colC] = cell(34, 70);
    colourLabel_.setBounds(colL);
    colourCombo_.setBounds(colC.reduced(2, 1));
}

void SpectrumAnalyzerUI::timerCallback() {
    if (plugin_ == nullptr || fft_ == nullptr) {
        repaint();
        return;
    }

    plugin_->getTapBuffer().readLatest(readBuf_.data(), fftSize_);
    std::copy(readBuf_.begin(), readBuf_.end(), fftData_.begin());
    std::fill(fftData_.begin() + fftSize_, fftData_.end(), 0.0f);
    window_->multiplyWithWindowingTable(fftData_.data(), static_cast<size_t>(fftSize_));
    fft_->performFrequencyOnlyForwardTransform(fftData_.data());

    const float norm = 2.0f / static_cast<float>(fftSize_);
    const double sr = plugin_->getSampleRate();
    const float binHz = static_cast<float>(sr / static_cast<double>(fftSize_));
    for (int i = 0; i < numBins_; ++i) {
        const float mag = fftData_[static_cast<size_t>(i)] * norm;
        float db = 20.0f * std::log10(std::max(mag, 1.0e-6f));
        // Display slope (tilt) pivoted at 1 kHz so music reads roughly flat.
        if (i > 0)
            db += slopeDbPerOct_ * std::log2(static_cast<float>(i) * binHz / 1000.0f);
        db = juce::jlimit(kMinDb, kMaxDb, db);

        float& sm = smoothedDb_[static_cast<size_t>(i)];
        sm += smoothing_ * (db - sm);
        float& pk = peakDb_[static_cast<size_t>(i)];
        pk = sm > pk ? sm : juce::jmax(kMinDb, pk - kPeakDecayDb);
    }
    repaint();
}

float SpectrumAnalyzerUI::freqToX(float hz, juce::Rectangle<float> area) const {
    const float t = std::log(juce::jmax(kMinHz, hz) / kMinHz) / std::log(kMaxHz / kMinHz);
    return area.getX() + juce::jlimit(0.0f, 1.0f, t) * area.getWidth();
}

float SpectrumAnalyzerUI::dbToY(float db, juce::Rectangle<float> area) const {
    const float t = (db - kMinDb) / (kMaxDb - kMinDb);
    return area.getBottom() - juce::jlimit(0.0f, 1.0f, t) * area.getHeight();
}

juce::Rectangle<float> SpectrumAnalyzerUI::plotArea() const {
    auto a = getLocalBounds();
    a.removeFromBottom(kControlRowH);
    return a.toFloat().reduced(4.0f);
}

void SpectrumAnalyzerUI::paint(juce::Graphics& g) {
    const auto plot = plotArea();

    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRoundedRectangle(plot, 4.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE));
    for (float f : {100.0f, 1000.0f, 10000.0f})
        g.drawVerticalLine(static_cast<int>(freqToX(f, plot)), plot.getY(), plot.getBottom());

    // dB scale on the left axis.
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    for (float db = kMaxDb; db >= kMinDb; db -= 20.0f) {
        const float y = dbToY(db, plot);
        g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE));
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
        const float ly = (db >= kMaxDb - 0.01f) ? y + 1.0f : y - 11.0f;  // top label sits below
        g.drawText(juce::String(static_cast<int>(db)),
                   juce::Rectangle<float>(plot.getX() + 2.0f, ly, 30.0f, 11.0f),
                   juce::Justification::topLeft);
    }

    // Frequency axis labels along the bottom of the plot.
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    auto freqLabel = [&](float f, const juce::String& s) {
        const float x = freqToX(f, plot);
        g.drawText(s, juce::Rectangle<float>(x - 18.0f, plot.getBottom() - 13.0f, 36.0f, 12.0f),
                   juce::Justification::centred);
    };
    freqLabel(100.0f, "100");
    freqLabel(1000.0f, "1k");
    freqLabel(10000.0f, "10k");

    if (smoothedDb_.empty())
        return;

    const double sr = (plugin_ != nullptr) ? plugin_->getSampleRate() : 44100.0;
    const float binHz = static_cast<float>(sr / static_cast<double>(fftSize_));

    auto buildPath = [&](const std::vector<float>& db) {
        juce::Path p;
        bool started = false;
        for (int i = 1; i < numBins_; ++i) {  // skip DC
            const float x = freqToX(static_cast<float>(i) * binHz, plot);
            const float y = dbToY(db[static_cast<size_t>(i)], plot);
            if (!started) {
                p.startNewSubPath(x, y);
                started = true;
            } else {
                p.lineTo(x, y);
            }
        }
        return p;
    };

    const juce::Colour trace =
        analyzerTraceColour(plugin_ != nullptr ? plugin_->getTraceColourIndex() : 0);
    g.setColour(trace.withAlpha(0.4f));  // peak-hold
    g.strokePath(buildPath(peakDb_), juce::PathStrokeType(1.0f));
    g.setColour(trace);  // live spectrum
    g.strokePath(buildPath(smoothedDb_), juce::PathStrokeType(1.5f));

    // Hover readout: a vertical cursor plus the frequency and spectrum level at it.
    if (mouseOver_) {
        const float mx =
            juce::jlimit(plot.getX(), plot.getRight(), static_cast<float>(mousePos_.x));
        const float t = (mx - plot.getX()) / plot.getWidth();
        const float freq = kMinHz * std::pow(kMaxHz / kMinHz, juce::jlimit(0.0f, 1.0f, t));
        const int bin = juce::jlimit(1, numBins_ - 1, juce::roundToInt(freq / binHz));
        const float db = smoothedDb_[static_cast<size_t>(bin)];

        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM).withAlpha(0.6f));
        g.drawVerticalLine(static_cast<int>(mx), plot.getY(), plot.getBottom());
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
        g.fillEllipse(mx - 2.5f, dbToY(db, plot) - 2.5f, 5.0f, 5.0f);

        const juce::String fTxt =
            freq >= 1000.0f ? juce::String(freq / 1000.0f, freq >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String(juce::roundToInt(freq)) + " Hz";
        const juce::String txt = fTxt + "   " + juce::String(db, 1) + " dB";
        auto box = plot.reduced(6.0f, 4.0f).removeFromTop(14.0f).withWidth(150.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).withAlpha(0.75f));
        g.fillRect(box);
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.drawText(txt, box, juce::Justification::centredLeft);
    }
}

void SpectrumAnalyzerUI::mouseMove(const juce::MouseEvent& e) {
    mousePos_ = e.getPosition();
    mouseOver_ = plotArea().contains(mousePos_.toFloat());
    repaint();
}

void SpectrumAnalyzerUI::mouseExit(const juce::MouseEvent&) {
    mouseOver_ = false;
    repaint();
}

}  // namespace magda::daw::ui
