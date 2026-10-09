#include "compiled/SpectrumOverlay.hpp"

#include <algorithm>
#include <cmath>

#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui {

namespace {
constexpr float kSmoothing = 0.45f;
}

SpectrumOverlay::SpectrumOverlay()
    : readBuffer_(static_cast<size_t>(kFftSize), 0.0f),
      fftData_(static_cast<size_t>(kFftSize) * 2, 0.0f),
      inputDb_(static_cast<size_t>(kNumBins), kMinDb),
      outputDb_(static_cast<size_t>(kNumBins), kMinDb) {}

void SpectrumOverlay::reset() {
    lastInputPosition_ = lastOutputPosition_ = 0;
    std::fill(inputDb_.begin(), inputDb_.end(), kMinDb);
    std::fill(outputDb_.begin(), outputDb_.end(), kMinDb);
}

void SpectrumOverlay::update(const magda::engine::SampleRing& input,
                             const magda::engine::SampleRing& output) {
    updateTrace(input, lastInputPosition_, inputDb_);
    updateTrace(output, lastOutputPosition_, outputDb_);
}

void SpectrumOverlay::updateTrace(const magda::engine::SampleRing& tap, size_t& lastPosition,
                                  std::vector<float>& traceDb) {
    if (tap.writePosition() == lastPosition)
        return;
    lastPosition = tap.readLatest(readBuffer_.data(), kFftSize);
    if (lastPosition == 0)
        return;

    std::copy(readBuffer_.begin(), readBuffer_.end(), fftData_.begin());
    std::fill(fftData_.begin() + kFftSize, fftData_.end(), 0.0f);
    window_.multiplyWithWindowingTable(fftData_.data(), static_cast<size_t>(kFftSize));
    fft_.performFrequencyOnlyForwardTransform(fftData_.data());

    const float norm = 2.0f / static_cast<float>(kFftSize);
    for (int i = 0; i < kNumBins; ++i) {
        const float magnitude = fftData_[static_cast<size_t>(i)] * norm;
        const float db =
            juce::jlimit(kMinDb, kMaxDb, 20.0f * std::log10(std::max(magnitude, 1.0e-6f)));
        float& smoothed = traceDb[static_cast<size_t>(i)];
        smoothed += kSmoothing * (db - smoothed);
    }
}

void SpectrumOverlay::draw(juce::Graphics& g, juce::Rectangle<float> area, double sampleRate,
                           float minHz, float maxHz,
                           const std::function<float(float hz)>& frequencyToX) const {
    if (sampleRate <= 0.0)
        return;

    const auto binHz = static_cast<float>(sampleRate / static_cast<double>(kFftSize));
    const auto dbToY = [area](float db) {
        const float t = (db - kMinDb) / (kMaxDb - kMinDb);
        return area.getBottom() - juce::jlimit(0.0f, 1.0f, t) * area.getHeight();
    };
    const auto buildPath = [&](const std::vector<float>& traceDb) {
        juce::Path path;
        bool started = false;
        for (int i = 1; i < kNumBins; ++i) {
            const float hz = static_cast<float>(i) * binHz;
            if (hz < minHz || hz > maxHz)
                continue;
            const float x = frequencyToX(hz);
            const float y = dbToY(traceDb[static_cast<size_t>(i)]);
            if (!started) {
                path.startNewSubPath(x, y);
                started = true;
            } else {
                path.lineTo(x, y);
            }
        }
        return path;
    };

    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY).withAlpha(0.16f));
    g.strokePath(buildPath(inputDb_), juce::PathStrokeType(1.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_INFO).withAlpha(0.32f));
    g.strokePath(buildPath(outputDb_), juce::PathStrokeType(1.1f));
}

}  // namespace magda::daw::ui
