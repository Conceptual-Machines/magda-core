#include "SpectrumAnalyzerUI.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::ui {

namespace {
constexpr float kSmoothing = 0.5f;    // 0..1, higher = snappier
constexpr float kPeakDecayDb = 0.6f;  // dB dropped per frame on the peak trace
}  // namespace

SpectrumAnalyzerUI::SpectrumAnalyzerUI() {
    readBuf_.assign(static_cast<size_t>(kFftSize), 0.0f);
    fftData_.assign(static_cast<size_t>(kFftSize) * 2, 0.0f);
    smoothedDb_.assign(static_cast<size_t>(kNumBins), kMinDb);
    peakDb_.assign(static_cast<size_t>(kNumBins), kMinDb);
    startTimerHz(30);
}

SpectrumAnalyzerUI::~SpectrumAnalyzerUI() {
    stopTimer();
}

void SpectrumAnalyzerUI::timerCallback() {
    if (plugin_ == nullptr) {
        repaint();
        return;
    }

    plugin_->getTapBuffer().readLatest(readBuf_.data(), kFftSize);

    std::copy(readBuf_.begin(), readBuf_.end(), fftData_.begin());
    std::fill(fftData_.begin() + kFftSize, fftData_.end(), 0.0f);
    window_.multiplyWithWindowingTable(fftData_.data(), static_cast<size_t>(kFftSize));
    fft_.performFrequencyOnlyForwardTransform(fftData_.data());

    const float norm = 2.0f / static_cast<float>(kFftSize);
    for (int i = 0; i < kNumBins; ++i) {
        const float mag = fftData_[static_cast<size_t>(i)] * norm;
        float db = 20.0f * std::log10(std::max(mag, 1.0e-6f));
        db = juce::jlimit(kMinDb, kMaxDb, db);

        smoothedDb_[static_cast<size_t>(i)] +=
            kSmoothing * (db - smoothedDb_[static_cast<size_t>(i)]);
        if (smoothedDb_[static_cast<size_t>(i)] > peakDb_[static_cast<size_t>(i)])
            peakDb_[static_cast<size_t>(i)] = smoothedDb_[static_cast<size_t>(i)];
        else
            peakDb_[static_cast<size_t>(i)] =
                juce::jmax(kMinDb, peakDb_[static_cast<size_t>(i)] - kPeakDecayDb);
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

void SpectrumAnalyzerUI::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat().reduced(4.0f);

    g.setColour(juce::Colour(0xff10141a));
    g.fillRoundedRectangle(area, 4.0f);

    // Frequency + dB grid.
    g.setColour(juce::Colour(0xff20262e));
    for (float f : {100.0f, 1000.0f, 10000.0f})
        g.drawVerticalLine(static_cast<int>(freqToX(f, area)), area.getY(), area.getBottom());
    for (float db = kMaxDb; db >= kMinDb; db -= 20.0f)
        g.drawHorizontalLine(static_cast<int>(dbToY(db, area)), area.getX(), area.getRight());

    const double sr = (plugin_ != nullptr) ? plugin_->getSampleRate() : 44100.0;
    const float binHz = static_cast<float>(sr / static_cast<double>(kFftSize));

    auto buildPath = [&](const std::vector<float>& db) {
        juce::Path p;
        bool started = false;
        for (int i = 1; i < kNumBins; ++i) {  // skip DC bin
            const float x = freqToX(static_cast<float>(i) * binHz, area);
            const float y = dbToY(db[static_cast<size_t>(i)], area);
            if (!started) {
                p.startNewSubPath(x, y);
                started = true;
            } else {
                p.lineTo(x, y);
            }
        }
        return p;
    };

    g.setColour(juce::Colour(0x80ffb347));  // peak-hold trace
    g.strokePath(buildPath(peakDb_), juce::PathStrokeType(1.0f));

    g.setColour(juce::Colour(0xff4fd1c5));  // live spectrum
    g.strokePath(buildPath(smoothedDb_), juce::PathStrokeType(1.5f));
}

}  // namespace magda::daw::ui
