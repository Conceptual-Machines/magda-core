#include "compiled/CompiledEqCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "../../../utils/CurveLabelLayout.hpp"
#include "audio/plugins/compiled/MagdaEqCompiledPlugin.hpp"
#include "compiled/CompiledEqBandLayout.hpp"
#include "core/GestureRouter.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 50;
constexpr int kPathSamples = 200;

constexpr float kFreqMinHz = 20.0f;
constexpr float kFreqMaxHz = 20000.0f;
constexpr float kGainAxisDb = 24.0f;  // ±24 dB display range

constexpr float kTwoPi = 6.28318530717958647692f;

// Visualisation-only sample rate. The biquad magnitudes are slightly rate-
// dependent near Nyquist; 48 kHz is close enough to what the user hears.
constexpr float kVisualSampleRate = 48000.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;  // a0 normalised to 1
};

// Audio EQ Cookbook coefficients, matching the webaudio.lib biquads the
// audio thread is running. Shelf filters use the WebAudio convention
// (S = 1 → α = sin(ω0)/√2, no exposed Q).
BiquadCoeffs makeBiquad(magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType type, float f0,
                        float gainDb, float q, float sampleRate) {
    using BandType = magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType;

    BiquadCoeffs out;
    const float safeQ = std::max(0.05f, q);
    const float fc = juce::jlimit(1.0f, sampleRate * 0.49f, f0);
    const float w0 = kTwoPi * fc / sampleRate;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * safeQ);

    auto normalise = [&](float b0, float b1, float b2, float a0, float a1, float a2) {
        const float inv = 1.0f / a0;
        out.b0 = b0 * inv;
        out.b1 = b1 * inv;
        out.b2 = b2 * inv;
        out.a1 = a1 * inv;
        out.a2 = a2 * inv;
    };

    switch (type) {
        case BandType::Highpass: {
            const float b0 = (1.0f + cw) * 0.5f;
            const float b1 = -(1.0f + cw);
            const float b2 = (1.0f + cw) * 0.5f;
            const float a0 = 1.0f + alpha;
            const float a1 = -2.0f * cw;
            const float a2 = 1.0f - alpha;
            normalise(b0, b1, b2, a0, a1, a2);
            break;
        }
        case BandType::Lowpass: {
            const float b0 = (1.0f - cw) * 0.5f;
            const float b1 = 1.0f - cw;
            const float b2 = (1.0f - cw) * 0.5f;
            const float a0 = 1.0f + alpha;
            const float a1 = -2.0f * cw;
            const float a2 = 1.0f - alpha;
            normalise(b0, b1, b2, a0, a1, a2);
            break;
        }
        case BandType::Bell: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float b0 = 1.0f + alpha * A;
            const float b1 = -2.0f * cw;
            const float b2 = 1.0f - alpha * A;
            const float a0 = 1.0f + alpha / A;
            const float a1 = -2.0f * cw;
            const float a2 = 1.0f - alpha / A;
            normalise(b0, b1, b2, a0, a1, a2);
            break;
        }
        case BandType::LowShelf: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float shelfAlpha = sw / 1.41421356f;  // S = 1
            const float sqA2 = 2.0f * std::sqrt(A) * shelfAlpha;
            const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + sqA2);
            const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
            const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - sqA2);
            const float a0 = (A + 1.0f) + (A - 1.0f) * cw + sqA2;
            const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
            const float a2 = (A + 1.0f) + (A - 1.0f) * cw - sqA2;
            normalise(b0, b1, b2, a0, a1, a2);
            break;
        }
        case BandType::HighShelf: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float shelfAlpha = sw / 1.41421356f;
            const float sqA2 = 2.0f * std::sqrt(A) * shelfAlpha;
            const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cw + sqA2);
            const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
            const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cw - sqA2);
            const float a0 = (A + 1.0f) - (A - 1.0f) * cw + sqA2;
            const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
            const float a2 = (A + 1.0f) - (A - 1.0f) * cw - sqA2;
            normalise(b0, b1, b2, a0, a1, a2);
            break;
        }
        case BandType::Notch: {
            const float b0 = 1.0f;
            const float b1 = -2.0f * cw;
            const float b2 = 1.0f;
            const float a0 = 1.0f + alpha;
            const float a1 = -2.0f * cw;
            const float a2 = 1.0f - alpha;
            normalise(b0, b1, b2, a0, a1, a2);
            break;
        }
    }
    return out;
}

// Magnitude squared of H(e^{jω}) for a normalised biquad (a0 = 1).
float magnitudeSquaredAt(const BiquadCoeffs& c, float freqHz, float sampleRate) {
    const float w = kTwoPi * freqHz / sampleRate;
    const float cw = std::cos(w);
    const float c2w = std::cos(2.0f * w);
    const float sw = std::sin(w);
    const float s2w = std::sin(2.0f * w);

    const float numR = c.b0 + c.b1 * cw + c.b2 * c2w;
    const float numI = c.b1 * sw + c.b2 * s2w;
    const float denR = 1.0f + c.a1 * cw + c.a2 * c2w;
    const float denI = c.a1 * sw + c.a2 * s2w;

    const float num = numR * numR + numI * numI;
    const float den = denR * denR + denI * denI;
    if (den < 1.0e-20f)
        return 0.0f;
    return num / den;
}

const char* bandTypeShortName(magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType t) {
    using BandType = magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType;
    switch (t) {
        case BandType::Highpass:
            return "HP";
        case BandType::LowShelf:
            return "LS";
        case BandType::Bell:
            return "Bell";
        case BandType::HighShelf:
            return "HS";
        case BandType::Lowpass:
            return "LP";
        case BandType::Notch:
            return "Notch";
    }
    return "?";
}

juce::String noteNameForFrequency(float freqHz) {
    if (freqHz <= 0.0f || !std::isfinite(freqHz))
        return {};

    static constexpr const char* kNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                             "F#", "G",  "G#", "A",  "A#", "B"};
    const int midi = juce::jlimit(
        0, 127, static_cast<int>(std::round(69.0f + 12.0f * std::log2(freqHz / 440.0f))));
    const int octave = midi / 12 - 1;
    return juce::String(kNames[midi % 12]) + juce::String(octave);
}

juce::String frequencyReadout(float freqHz) {
    return juce::String(freqHz, freqHz >= 1000.0f ? 0 : 1) + " Hz " + noteNameForFrequency(freqHz);
}

float logFreqToX(float freq, juce::Rectangle<float> area) {
    const float logMin = std::log10(kFreqMinHz);
    const float logMax = std::log10(kFreqMaxHz);
    const float u =
        (std::log10(juce::jlimit(kFreqMinHz, kFreqMaxHz, freq)) - logMin) / (logMax - logMin);
    return area.getX() + u * area.getWidth();
}

float dbToY(float db, juce::Rectangle<float> area) {
    const float u = 0.5f - juce::jlimit(-kGainAxisDb, kGainAxisDb, db) / (2.0f * kGainAxisDb);
    return area.getY() + u * area.getHeight();
}

float xToLogFreq(float x, juce::Rectangle<float> area) {
    const float logMin = std::log10(kFreqMinHz);
    const float logMax = std::log10(kFreqMaxHz);
    const float u = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / std::max(1.0f, area.getWidth()));
    return std::pow(10.0f, logMin + u * (logMax - logMin));
}

float yToLinearDb(float y, juce::Rectangle<float> area) {
    const float u = juce::jlimit(0.0f, 1.0f, (y - area.getY()) / std::max(1.0f, area.getHeight()));
    return (0.5f - u) * 2.0f * kGainAxisDb;
}

// True if a click at (px, py) should grab band `b`. We hit-test against the
// dot's centre with a generous radius; for HP/LP/Notch the dot sits at 0 dB
// so the "hot zone" is a vertical column rather than a point.
constexpr float kHitRadiusPx = 12.0f;

// The active band's live readout owns a strip along the top of the plot. The
// per-band number rides above its dot and must stay out of that strip, so both
// are laid out from the same numbers.
constexpr float kReadoutTopInset = 2.0f;
constexpr float kReadoutHeight = 14.0f;
constexpr float kReadoutWidth = 120.0f;
constexpr float kBandNumberWidth = 10.0f;
constexpr float kBandNumberHeight = 12.0f;
constexpr float kBandNumberGap = 6.0f;

float readoutStripBottom(juce::Rectangle<float> plotArea) {
    return plotArea.getY() + kReadoutTopInset + kReadoutHeight;
}

bool bandGainAffectsCurve(magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType t) {
    using BandType = magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType;
    return t == BandType::Bell || t == BandType::LowShelf || t == BandType::HighShelf;
}

}  // namespace

namespace {

/// The six types in the order the editor offers them, low cut to high cut.
constexpr std::array<magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType, 6> kTypeOrder{{
    magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType::Highpass,
    magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType::LowShelf,
    magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType::Bell,
    magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType::Notch,
    magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType::HighShelf,
    magda::daw::audio::compiled::MagdaEqCompiledPlugin::BandType::Lowpass,
}};

juce::String compactFrequency(float hz) {
    if (hz >= 1000.0f) {
        const float k = hz / 1000.0f;
        return (k >= 10.0f || std::abs(k - std::round(k)) < 0.05f
                    ? juce::String(juce::roundToInt(k))
                    : juce::String(k, 1)) +
               "k";
    }
    return juce::String(juce::roundToInt(hz));
}

}  // namespace

CompiledEqCurveView::CompiledEqCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(true, true);
    typeChoice_.setOptions({"LC", "LS", "Bell", "Notch", "HS", "HC"});
    typeChoice_.onChange = [this](int index) {
        setBandType(selectedBand_, kTypeOrder[static_cast<size_t>(index)]);
        repaint();
    };
    addAndMakeVisible(typeChoice_);
    startTimer(kPollMs);
}

void CompiledEqCurveView::setCompiledPlugin(
    std::shared_ptr<magda::daw::audio::compiled::MagdaEqCompiledPlugin> plugin) {
    if (compiledPlugin_ == plugin)
        return;

    compiledPlugin_ = std::move(plugin);
    spectrum_.reset();
}

void CompiledEqCurveView::bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    setCompiledPlugin(std::dynamic_pointer_cast<magda::daw::audio::compiled::MagdaEqCompiledPlugin>(
        std::move(device)));
}

void CompiledEqCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledEqCurveView::resampleFromDevice() {
    using Eq = magda::daw::audio::compiled::MagdaEqCompiledPlugin;

    for (int band = 0; band < Eq::kBandCount; ++band) {
        // Every slot falls back to what is already cached, never to a fixed
        // default: a snapshot that omits a slot must leave the view showing
        // the last known state rather than silently reading the band as
        // disabled (or as a Bell).
        BandSnapshot snap;
        snap.enabled = valueForSlot(deviceSnapshot_, Eq::bandSlot(band, Eq::kBandEnabledOffset),
                                    bands_[band].enabled ? 1.0f : 0.0f) >= 0.5f;
        const int typeIndex =
            juce::jlimit(0, Eq::kBandTypeCount - 1,
                         static_cast<int>(std::round(
                             valueForSlot(deviceSnapshot_, Eq::bandSlot(band, Eq::kBandTypeOffset),
                                          static_cast<float>(bands_[band].type)))));
        snap.type = static_cast<BandType>(typeIndex);
        snap.freq = valueForSlot(deviceSnapshot_, Eq::bandSlot(band, Eq::kBandFreqOffset),
                                 bands_[band].freq);
        snap.gainDb = valueForSlot(deviceSnapshot_, Eq::bandSlot(band, Eq::kBandGainOffset),
                                   bands_[band].gainDb);
        snap.q =
            valueForSlot(deviceSnapshot_, Eq::bandSlot(band, Eq::kBandQOffset), bands_[band].q);
        bands_[band] = snap;
    }
    outputDb_ = valueForSlot(deviceSnapshot_, Eq::kOutputSlot, outputDb_);
}

void CompiledEqCurveView::timerCallback() {
    if (compiledPlugin_ != nullptr) {
        for (int band = 0; band < Plugin::kBandCount; ++band)
            bands_[band] = compiledPlugin_->getBandSnapshot(band);
        outputDb_ = compiledPlugin_->getOutputDb();
        spectrum_.update(compiledPlugin_->getPreSpectrumTapBuffer(),
                         compiledPlugin_->getPostSpectrumTapBuffer());
        syncTypeChoice();
    }
    repaint();
}

void CompiledEqCurveView::resized() {
    geometry_ = EqBandEditorGeometry::of(getLocalBounds());
    plotArea_ = geometry_.plot.toFloat();
    // The type segments sit at the right of the editor's controls.
    const int width = typeChoice_.getPreferredWidth();
    typeChoice_.setBounds(
        geometry_.editorControls.removeFromRight(width).withSizeKeepingCentre(width, 30));
}

void CompiledEqCurveView::selectBand(int band) {
    if (band < 0 || band >= Plugin::kBandCount)
        return;
    selectedBand_ = band;
    syncTypeChoice();
    if (onPageRequested_)
        onPageRequested_(band);
    repaint();
}

void CompiledEqCurveView::syncTypeChoice() {
    const auto type = bands_[static_cast<size_t>(selectedBand_)].type;
    for (int i = 0; i < static_cast<int>(kTypeOrder.size()); ++i)
        if (kTypeOrder[static_cast<size_t>(i)] == type)
            typeChoice_.setSelectedIndex(i);
}

juce::Rectangle<float> CompiledEqCurveView::chipBounds(int band) const {
    constexpr float kGap = 6.0f;
    const auto row = geometry_.chips.toFloat();
    const float width = (row.getWidth() - kGap * (Plugin::kBandCount - 1)) / Plugin::kBandCount;
    return {row.getX() + static_cast<float>(band) * (width + kGap), row.getY(), width,
            row.getHeight()};
}

int CompiledEqCurveView::chipAt(juce::Point<float> p) const {
    for (int band = 0; band < Plugin::kBandCount; ++band)
        if (chipBounds(band).contains(p))
            return band;
    return -1;
}

juce::Rectangle<float> CompiledEqCurveView::enableSwitchBounds() const {
    const auto controls = geometry_.editorControls.toFloat();
    return juce::Rectangle<float>(36.0f, 20.0f)
        .withCentre({controls.getX() + 18.0f, controls.getCentreY()});
}

void CompiledEqCurveView::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(plotArea_, 4.0f);
    if (plotArea_.getWidth() < 8.0f || plotArea_.getHeight() < 8.0f)
        return;

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (float db : {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f})
        g.drawHorizontalLine(juce::roundToInt(dbToY(db, plotArea_)), plotArea_.getX(),
                             plotArea_.getRight());
    for (float hz : {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f})
        g.drawVerticalLine(juce::roundToInt(logFreqToX(hz, plotArea_)), plotArea_.getY(),
                           plotArea_.getBottom());
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    g.setFont(fonts.getMonoFont(9.0f));
    for (float db : {12.0f, 6.0f, -6.0f})
        g.drawText((db > 0.0f ? "+" : "") + juce::String(juce::roundToInt(db)),
                   juce::Rectangle<float>(plotArea_.getRight() - 34.0f,
                                          dbToY(db, plotArea_) - 13.0f, 28.0f, 12.0f),
                   juce::Justification::centredRight, false);

    if (compiledPlugin_ != nullptr)
        spectrum_.draw(g, plotArea_, compiledPlugin_->getSampleRate(), kFreqMinHz, kFreqMaxHz,
                       [this](float hz) { return logFreqToX(hz, plotArea_); });

    // The total response, and the selected band's own share of it, shaded in its colour.
    juce::Path curve;
    juce::Path selectedShare;
    const float zeroY = dbToY(0.0f, plotArea_);
    const auto& selected = bands_[static_cast<size_t>(selectedBand_)];
    const auto selectedCoeffs =
        makeBiquad(selected.type, selected.freq, selected.gainDb, selected.q, kVisualSampleRate);
    for (int i = 0; i <= kPathSamples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kPathSamples);
        const float freq = kFreqMinHz * std::pow(kFreqMaxHz / kFreqMinHz, u);
        float totalDb = outputDb_;
        for (const auto& band : bands_) {
            if (!band.enabled)
                continue;
            const auto coeffs =
                makeBiquad(band.type, band.freq, band.gainDb, band.q, kVisualSampleRate);
            totalDb +=
                10.0f *
                std::log10(std::max(1.0e-12f, magnitudeSquaredAt(coeffs, freq, kVisualSampleRate)));
        }
        const float ownDb =
            10.0f * std::log10(std::max(
                        1.0e-12f, magnitudeSquaredAt(selectedCoeffs, freq, kVisualSampleRate)));
        const float x = plotArea_.getX() + u * plotArea_.getWidth();
        const float y = dbToY(totalDb, plotArea_);
        const float ownY = dbToY(ownDb, plotArea_);
        if (i == 0) {
            curve.startNewSubPath(x, y);
            selectedShare.startNewSubPath(x, zeroY);
        } else {
            curve.lineTo(x, y);
        }
        selectedShare.lineTo(x, ownY);
    }
    selectedShare.lineTo(plotArea_.getRight(), zeroY);
    selectedShare.closeSubPath();
    const auto bandColour = [](int band) { return device_shell::chainColour(band); };
    if (selected.enabled) {
        g.setColour(bandColour(selectedBand_).withAlpha(0.18f));
        g.fillPath(selectedShare);
    }
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT).withAlpha(0.9f));
    g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Nodes: numbered, in their band's colour; a band that is off is a grey ring.
    g.setFont(fonts.getMonoFont(10.0f).boldened());
    for (int b = 0; b < Plugin::kBandCount; ++b) {
        const auto& band = bands_[static_cast<size_t>(b)];
        const float nodeDb = bandGainAffectsCurve(band.type) ? band.gainDb : 0.0f;
        const auto centre =
            juce::Point<float>(logFreqToX(band.freq, plotArea_), dbToY(nodeDb, plotArea_));
        const auto node = juce::Rectangle<float>(18.0f, 18.0f).withCentre(centre);
        if (b == selectedBand_ || b == draggedBand_ || b == hoveredBand_) {
            g.setColour(bandColour(b).withAlpha(0.25f));
            g.fillEllipse(node.expanded(5.0f));
        }
        if (band.enabled) {
            g.setColour(bandColour(b));
            g.fillEllipse(node);
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
        } else {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
            g.fillEllipse(node);
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            g.drawEllipse(node.reduced(0.75f), 1.5f);
        }
        g.drawText(juce::String(b + 1), node, juce::Justification::centred, false);
    }

    paintChips(g);
    paintEditor(g);
}

void CompiledEqCurveView::paintChips(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    for (int b = 0; b < Plugin::kBandCount; ++b) {
        const auto& band = bands_[static_cast<size_t>(b)];
        const auto chip = chipBounds(b);
        const auto colour = device_shell::chainColour(b);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(chip, 4.0f);
        if (b == selectedBand_) {
            g.setColour(colour);
            g.drawRoundedRectangle(chip.reduced(0.75f), 4.0f, 1.5f);
        }
        auto inner = chip.reduced(10.0f, 0.0f);
        const auto dot = juce::Rectangle<float>(8.0f, 8.0f)
                             .withCentre({inner.getX() + 4.0f, inner.getCentreY()});
        if (band.enabled) {
            g.setColour(colour);
            g.fillEllipse(dot);
        } else {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            g.drawEllipse(dot, 1.2f);
        }
        inner.removeFromLeft(16.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
        g.setFont(fonts.getMonoFont(11.0f).boldened());
        g.drawText(juce::String(b + 1), inner, juce::Justification::centredLeft, false);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.setFont(fonts.getMonoFont(10.0f));
        g.drawText(compactFrequency(band.freq), inner, juce::Justification::centredRight, false);
    }
}

void CompiledEqCurveView::paintEditor(juce::Graphics& g) {
    const auto editor = geometry_.editor.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(editor, 4.0f);

    // The band's switch, then its name.
    const auto& band = bands_[static_cast<size_t>(selectedBand_)];
    const auto colour = device_shell::chainColour(selectedBand_);
    const auto track = enableSwitchBounds();
    g.setColour(band.enabled ? colour : ActiveTheme::getColour(ActiveTheme::DEVICE_SWITCH_OFF));
    g.fillRoundedRectangle(track, track.getHeight() / 2.0f);
    const float knobX = band.enabled ? track.getRight() - track.getHeight() : track.getX();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
    g.fillEllipse(juce::Rectangle<float>(knobX, track.getY(), track.getHeight(), track.getHeight())
                      .reduced(3.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.setFont(FontManager::getInstance().getHeadingFont(15.0f));
    g.drawText(
        "Band " + juce::String(selectedBand_ + 1),
        juce::Rectangle<float>(track.getRight() + 12.0f, editor.getY(), 90.0f, editor.getHeight()),
        juce::Justification::centredLeft, false);

    // The divider before Output.
    const auto output = geometry_.output.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.fillRect(juce::Rectangle<float>(output.getX() - 13.0f, editor.getY() + 16.0f, 1.0f,
                                      editor.getHeight() - 32.0f));
}

// ============================================================================
// Interaction
// ============================================================================

int CompiledEqCurveView::findBandAt(juce::Point<float> p) const {
    int closestBand = -1;
    float closestDist = kHitRadiusPx;
    for (int b = 0; b < Plugin::kBandCount; ++b) {
        const auto& band = bands_[b];
        const float dotDb = bandGainAffectsCurve(band.type) ? band.gainDb : 0.0f;
        const float dx = logFreqToX(band.freq, plotArea_) - p.x;
        const float dy = dbToY(dotDb, plotArea_) - p.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < closestDist) {
            closestDist = dist;
            closestBand = b;
        }
    }
    return closestBand;
}

float CompiledEqCurveView::xToFreq(float x) const {
    return xToLogFreq(x, plotArea_);
}

float CompiledEqCurveView::yToDb(float y) const {
    return yToLinearDb(y, plotArea_);
}

void CompiledEqCurveView::writeBandParam(int band, int slotOffset, float displayValue) {
    if (!onParameterChanged || band < 0 || band >= Plugin::kBandCount)
        return;
    onParameterChanged(Plugin::bandSlot(band, slotOffset), displayValue);
}

void CompiledEqCurveView::setBandType(int band, BandType type) {
    if (band < 0 || band >= Plugin::kBandCount)
        return;

    bands_[band].type = type;
    writeBandParam(band, Plugin::kBandTypeOffset, static_cast<float>(type));
}

void CompiledEqCurveView::setBandEnabled(int band, bool enabled) {
    if (band < 0 || band >= Plugin::kBandCount)
        return;

    bands_[band].enabled = enabled;
    writeBandParam(band, Plugin::kBandEnabledOffset, enabled ? 1.0f : 0.0f);
}

void CompiledEqCurveView::mouseMove(const juce::MouseEvent& e) {
    if (draggedBand_ != -1)
        return;
    const int picked = findBandAt(e.position);
    if (picked != hoveredBand_) {
        hoveredBand_ = picked;
        setMouseCursor(picked == -1 ? juce::MouseCursor::NormalCursor
                                    : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void CompiledEqCurveView::mouseExit(const juce::MouseEvent&) {
    if (draggedBand_ != -1)
        return;
    bool needsRepaint = false;
    if (hoveredBand_ != -1) {
        hoveredBand_ = -1;
        needsRepaint = true;
    }
    if (needsRepaint) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompiledEqCurveView::mouseDown(const juce::MouseEvent& e) {
    if (const int chip = chipAt(e.position); chip >= 0) {
        selectBand(chip);
        return;
    }
    if (enableSwitchBounds().contains(e.position)) {
        setBandEnabled(selectedBand_, !bands_[static_cast<size_t>(selectedBand_)].enabled);
        repaint();
        return;
    }

    const int picked = findBandAt(e.position);
    if (picked == -1)
        return;
    if (picked != selectedBand_)
        selectBand(picked);

    if (e.mods.isRightButtonDown()) {
        showBandTypeMenu(picked);
        return;
    }

    draggedBand_ = picked;
    hoveredBand_ = picked;
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void CompiledEqCurveView::mouseDoubleClick(const juce::MouseEvent& e) {
    const int picked = findBandAt(e.position);
    if (picked == -1)
        return;

    setBandEnabled(picked, !bands_[picked].enabled);
    hoveredBand_ = picked;
    repaint();
}

void CompiledEqCurveView::mouseDrag(const juce::MouseEvent& e) {
    if (draggedBand_ == -1)
        return;

    const auto& band = bands_[draggedBand_];
    const float newFreq = juce::jlimit(kFreqMinHz, kFreqMaxHz, xToFreq(e.position.x));
    writeBandParam(draggedBand_, Plugin::kBandFreqOffset, newFreq);

    if (bandGainAffectsCurve(band.type)) {
        const float newGain = juce::jlimit(-kGainAxisDb, kGainAxisDb, yToDb(e.position.y));
        writeBandParam(draggedBand_, Plugin::kBandGainOffset, newGain);
    }

    // Update the local snapshot eagerly so the dot follows the cursor on
    // this frame instead of waiting for the next poll round-trip.
    bands_[draggedBand_].freq = newFreq;
    if (bandGainAffectsCurve(band.type))
        bands_[draggedBand_].gainDb = juce::jlimit(-kGainAxisDb, kGainAxisDb, yToDb(e.position.y));
    repaint();
}

void CompiledEqCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedBand_ = -1;
    hoveredBand_ = findBandAt(e.position);
    setMouseCursor(hoveredBand_ == -1 ? juce::MouseCursor::NormalCursor
                                      : juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void CompiledEqCurveView::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel) {
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::EqCurveEditor, wheel, e.mods, e.getPosition());
    if (!magda::isValueAdjustmentAction(gesture.type)) {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    // Wheel always targets a band — use the dragged one if any, else the
    // band closest to the cursor regardless of hit radius (a generous gate
    // because the trackpad rarely lands the cursor inside the 12 px dot).
    int band = draggedBand_;
    if (band == -1) {
        float closestDx = std::numeric_limits<float>::max();
        for (int b = 0; b < Plugin::kBandCount; ++b) {
            const float dx = std::abs(logFreqToX(bands_[b].freq, plotArea_) - e.position.x);
            if (dx < closestDx) {
                closestDx = dx;
                band = b;
            }
        }
    }
    if (band == -1)
        return;

    // Q is on a log scale (0.1 .. 10). The router binding turns discrete and
    // smooth wheel deltas into the established log-domain adjustment.
    constexpr float kQMin = 0.1f;
    constexpr float kQMax = 10.0f;
    const float logMin = std::log10(kQMin);
    const float logMax = std::log10(kQMax);
    const float currentLog = std::log10(juce::jlimit(kQMin, kQMax, bands_[band].q));
    const float newLog = juce::jlimit(logMin, logMax, currentLog + gesture.magnitude);
    const float newQ = std::pow(10.0f, newLog);

    bands_[band].q = newQ;
    writeBandParam(band, Plugin::kBandQOffset, newQ);
    repaint();
}

void CompiledEqCurveView::showBandTypeMenu(int band) {
    if (band < 0 || band >= Plugin::kBandCount)
        return;

    juce::PopupMenu menu;
    static constexpr int kMenuBandTypeCount = Plugin::kBandTypeCount;
    const std::array<std::pair<BandType, juce::String>, kMenuBandTypeCount> kEntries{{
        {BandType::Highpass, "Highpass"},
        {BandType::LowShelf, "Low Shelf"},
        {BandType::Bell, "Bell"},
        {BandType::HighShelf, "High Shelf"},
        {BandType::Lowpass, "Lowpass"},
        {BandType::Notch, "Notch"},
    }};
    const BandType currentType = bands_[band].type;
    for (size_t i = 0; i < kEntries.size(); ++i) {
        const auto& [type, label] = kEntries[i];
        menu.addItem(static_cast<int>(i) + 1, label, true, type == currentType);
    }

    juce::Component::SafePointer<CompiledEqCurveView> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [self, band](int chosen) {
                           if (self == nullptr || chosen <= 0)
                               return;
                           self->setBandType(band, static_cast<BandType>(chosen - 1));
                           self->repaint();
                       });
}

const CompiledPresentationSpec& getMagdaEqPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaEqCompiledPlugin::xmlTypeName,
        // Show the 40 band slots (8 bands × {Enabled, Type, Freq, Gain, Q})
        // only. Output is host slot 40 but excluded from the grid so rows fit
        // cleanly into 5 × 8 with no sparse trailing row. The Output param
        // remains fully addressable via automation, macros and AI aliases.
        .layoutCellCount = magda::daw::audio::compiled::MagdaEqCompiledPlugin::kBandCount *
                           magda::daw::audio::compiled::MagdaEqCompiledPlugin::kSlotsPerBand,
        .layoutCellsPerRow = 8,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledEqCurveView>(pluginId);
        },
        // The curve, a chip per band, and one band's controls at a time.
        .preferredSlotWidth = 900,
        .columnMajorGrid = true,
        .createLayout = []() -> std::unique_ptr<DeviceParamLayout> {
            return std::make_unique<CompiledEqBandLayout>();
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
