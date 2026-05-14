#include "compiled/CompiledCompressorCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaCompressorCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 6.0f;
constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr float kMeterWidth = 58.0f;
constexpr int kPollMs = 33;
constexpr float kHandlePickPx = 8.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float linearToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

float gainReductionForLevel(float levelDb, float thresholdDb, float ratio, float kneeDb) {
    ratio = std::max(1.0f, ratio);
    kneeDb = std::max(0.0f, kneeDb);

    const float over = levelDb - thresholdDb;
    float compressedOver = over;
    if (kneeDb > 0.0f) {
        const float halfKnee = kneeDb * 0.5f;
        if (over <= -halfKnee) {
            compressedOver = over;
        } else if (over >= halfKnee) {
            compressedOver = over / ratio;
        } else {
            const float x = over + halfKnee;
            compressedOver = over + (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
        }
    } else if (over > 0.0f) {
        compressedOver = over / ratio;
    }

    return std::max(0.0f, over - compressedOver);
}

float compressedOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb,
                         float makeupDb, float outputDb, float mix) {
    const float compressedDb =
        inputDb - gainReductionForLevel(inputDb, thresholdDb, ratio, kneeDb) + makeupDb + outputDb;
    const float wet = dbToLinear(compressedDb);
    const float dry = dbToLinear(inputDb + outputDb);
    return linearToDb(dry * (1.0f - mix) + wet * mix);
}

juce::String formatDb(float db) {
    if (db <= -90.0f)
        return "-inf";
    return juce::String(db, db > -10.0f ? 1 : 0);
}

}  // namespace

CompiledCompressorCurveView::CompiledCompressorCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    startTimer(kPollMs);
}

void CompiledCompressorCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaCompressorCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledCompressorCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

void CompiledCompressorCurveView::timerCallback() {
    using Comp = magda::daw::audio::compiled::MagdaCompressorCompiledPlugin;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    float threshold = thresholdDb_;
    float ratio = ratio_;
    float knee = kneeDb_;
    float makeup = makeupDb_;
    float mix = mix_;
    float output = outputDb_;

    if (compiledPlugin_ != nullptr) {
        threshold = readPluginSlot(Comp::kThresholdSlot, threshold);
        ratio = readPluginSlot(Comp::kRatioSlot, ratio);
        knee = readPluginSlot(Comp::kKneeSlot, knee);
        makeup = readPluginSlot(Comp::kMakeupSlot, makeup);
        mix = readPluginSlot(Comp::kMixSlot, mix);
        output = readPluginSlot(Comp::kOutputSlot, output);

        inputPeakDb_ = compiledPlugin_->getInputPeakDb();
        keyPeakDb_ = compiledPlugin_->getKeyPeakDb();
        outputPeakDb_ = compiledPlugin_->getOutputPeakDb();
        gainReductionDb_ = compiledPlugin_->getGainReductionDb();
        externalSidechain_ = compiledPlugin_->isUsingExternalSidechain();
    } else {
        threshold = valueForSlot(deviceSnapshot_, Comp::kThresholdSlot, threshold);
        ratio = valueForSlot(deviceSnapshot_, Comp::kRatioSlot, ratio);
        knee = valueForSlot(deviceSnapshot_, Comp::kKneeSlot, knee);
        makeup = valueForSlot(deviceSnapshot_, Comp::kMakeupSlot, makeup);
        mix = valueForSlot(deviceSnapshot_, Comp::kMixSlot, mix);
        output = valueForSlot(deviceSnapshot_, Comp::kOutputSlot, output);
    }

    // Peak-meter style smoothing: fast rise, slow fall. Each tick is ~33ms;
    // attack coefficient 0.45 reaches ~98% in two ticks (~66ms), release
    // coefficient 0.88 lets values fall over ~250ms — gives the crosshair
    // lines a natural meter feel instead of per-block jitter.
    auto envelope = [](float& smoothed, float incoming, float attackCoeff, float releaseCoeff) {
        const float coeff = incoming > smoothed ? attackCoeff : releaseCoeff;
        smoothed = smoothed * coeff + incoming * (1.0f - coeff);
    };
    envelope(smoothedInputPeakDb_, juce::jmax(-120.0f, inputPeakDb_), 0.45f, 0.88f);
    envelope(smoothedKeyPeakDb_, juce::jmax(-120.0f, keyPeakDb_), 0.45f, 0.88f);
    envelope(smoothedOutputPeakDb_, juce::jmax(-120.0f, outputPeakDb_), 0.45f, 0.88f);
    envelope(smoothedGainReductionDb_, juce::jlimit(0.0f, 36.0f, gainReductionDb_), 0.4f, 0.85f);

    const bool moved = std::fabs(threshold - thresholdDb_) > 0.001f ||
                       std::fabs(ratio - ratio_) > 0.001f || std::fabs(knee - kneeDb_) > 0.001f ||
                       std::fabs(makeup - makeupDb_) > 0.001f || std::fabs(mix - mix_) > 0.001f ||
                       std::fabs(output - outputDb_) > 0.001f;
    if (moved) {
        thresholdDb_ = threshold;
        ratio_ = ratio;
        kneeDb_ = knee;
        makeupDb_ = makeup;
        mix_ = mix;
        outputDb_ = output;
    }

    repaint();
}

void CompiledCompressorCurveView::resampleFromPlugin() {
    using Comp = magda::daw::audio::compiled::MagdaCompressorCompiledPlugin;
    thresholdDb_ = valueForSlot(deviceSnapshot_, Comp::kThresholdSlot, thresholdDb_);
    ratio_ = valueForSlot(deviceSnapshot_, Comp::kRatioSlot, ratio_);
    kneeDb_ = valueForSlot(deviceSnapshot_, Comp::kKneeSlot, kneeDb_);
    makeupDb_ = valueForSlot(deviceSnapshot_, Comp::kMakeupSlot, makeupDb_);
    mix_ = valueForSlot(deviceSnapshot_, Comp::kMixSlot, mix_);
    outputDb_ = valueForSlot(deviceSnapshot_, Comp::kOutputSlot, outputDb_);
}

float CompiledCompressorCurveView::xToDb(float x) const {
    if (plotArea_.getWidth() <= 0.0f)
        return kMinDb;
    const float t = (x - plotArea_.getX()) / plotArea_.getWidth();
    return juce::jmap(juce::jlimit(0.0f, 1.0f, t), kMinDb, kMaxDb);
}

float CompiledCompressorCurveView::dbToX(float db) const {
    const float t = juce::jmap(juce::jlimit(kMinDb, kMaxDb, db), kMinDb, kMaxDb, 0.0f, 1.0f);
    return plotArea_.getX() + t * plotArea_.getWidth();
}

float CompiledCompressorCurveView::dbToY(float db) const {
    const float t = juce::jmap(juce::jlimit(kMinDb, kMaxDb, db), kMinDb, kMaxDb, 0.0f, 1.0f);
    return plotArea_.getBottom() - t * plotArea_.getHeight();
}

CompiledCompressorCurveView::Handle CompiledCompressorCurveView::pickHandle(float x) const {
    const float thresholdX = dbToX(thresholdDb_);
    return std::fabs(x - thresholdX) <= kHandlePickPx ? Handle::Threshold : Handle::None;
}

void CompiledCompressorCurveView::mouseMove(const juce::MouseEvent& e) {
    if (draggedHandle_ != Handle::None)
        return;
    const auto picked = pickHandle(static_cast<float>(e.x));
    if (picked != hoveredHandle_) {
        hoveredHandle_ = picked;
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void CompiledCompressorCurveView::mouseExit(const juce::MouseEvent&) {
    if (draggedHandle_ == Handle::None) {
        hoveredHandle_ = Handle::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompiledCompressorCurveView::mouseDown(const juce::MouseEvent& e) {
    const auto picked = pickHandle(static_cast<float>(e.x));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledCompressorCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Comp = magda::daw::audio::compiled::MagdaCompressorCompiledPlugin;
    if (draggedHandle_ != Handle::Threshold)
        return;

    const float threshold = juce::jlimit(kMinDb, 0.0f, xToDb(static_cast<float>(e.x)));
    if (std::fabs(threshold - thresholdDb_) > 0.05f) {
        thresholdDb_ = threshold;
        if (onParameterChanged)
            onParameterChanged(Comp::kThresholdSlot, threshold);
        repaint();
    }
}

void CompiledCompressorCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedHandle_ = Handle::None;
    hoveredHandle_ = pickHandle(static_cast<float>(e.x));
    setMouseCursor(hoveredHandle_ == Handle::None ? juce::MouseCursor::NormalCursor
                                                  : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void CompiledCompressorCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto area = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    auto meterArea = area.removeFromRight(kMeterWidth);
    area.removeFromRight(8.0f);
    plotArea_ = area;

    if (plotArea_.getWidth() < 16.0f || plotArea_.getHeight() < 16.0f)
        return;

    const auto border = DarkTheme::getColour(DarkTheme::BORDER);
    const auto text = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    const auto grColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto keyColour = externalSidechain_ ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT)
                                              : DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);

    g.setColour(border.withAlpha(0.55f));
    g.drawRect(plotArea_, 1.0f);

    {
        juce::Graphics::ScopedSaveState clipGuard(g);
        g.reduceClipRegion(plotArea_.toNearestInt());

        g.setColour(border.withAlpha(0.18f));
        for (float db : {-48.0f, -36.0f, -24.0f, -12.0f, 0.0f}) {
            const float x = dbToX(db);
            const float y = dbToY(db);
            g.drawVerticalLine(static_cast<int>(std::round(x)), plotArea_.getY(),
                               plotArea_.getBottom());
            g.drawHorizontalLine(static_cast<int>(std::round(y)), plotArea_.getX(),
                                 plotArea_.getRight());
        }

        juce::Path diagonal;
        diagonal.startNewSubPath(dbToX(kMinDb), dbToY(kMinDb));
        diagonal.lineTo(dbToX(kMaxDb), dbToY(kMaxDb));
        g.setColour(border.withAlpha(0.32f));
        g.strokePath(diagonal, juce::PathStrokeType(1.0f));

        if (kneeDb_ > 0.1f) {
            const float kneeLeft = dbToX(thresholdDb_ - kneeDb_ * 0.5f);
            const float kneeRight = dbToX(thresholdDb_ + kneeDb_ * 0.5f);
            g.setColour(grColour.withAlpha(0.08f));
            g.fillRect(juce::Rectangle<float>(kneeLeft, plotArea_.getY(), kneeRight - kneeLeft,
                                              plotArea_.getHeight()));
        }

        juce::Path curve;
        const int samples = juce::jmax(64, static_cast<int>(std::ceil(plotArea_.getWidth())));
        for (int i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
            const float inputDb = juce::jmap(t, kMinDb, kMaxDb);
            const float outputDb =
                compressedOutputDb(inputDb, thresholdDb_, ratio_, kneeDb_, makeupDb_, outputDb_,
                                   juce::jlimit(0.0f, 1.0f, mix_));
            const float x = plotArea_.getX() + t * plotArea_.getWidth();
            const float y = dbToY(outputDb);
            if (i == 0)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
        }

        g.setColour(accent.withAlpha(0.95f));
        g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Float-coord drawing for the crosshair lines: drawHorizontalLine /
        // drawVerticalLine snap to integer pixels, so even a perfectly
        // smoothed dB value will jump in 1-pixel steps. drawLine uses
        // sub-pixel positioning + antialiasing for continuous motion.
        const float thresholdX = dbToX(thresholdDb_);
        const bool thresholdHot =
            hoveredHandle_ == Handle::Threshold || draggedHandle_ == Handle::Threshold;
        g.setColour(grColour.withAlpha(thresholdHot ? 0.95f : 0.7f));
        g.drawLine(thresholdX, plotArea_.getY(), thresholdX, plotArea_.getBottom(), 1.0f);
        g.fillEllipse(thresholdX - 4.0f, dbToY(thresholdDb_) - 4.0f, 8.0f, 8.0f);

        const float keyX = dbToX(smoothedKeyPeakDb_);
        g.setColour(keyColour.withAlpha(0.8f));
        g.drawLine(keyX, plotArea_.getY(), keyX, plotArea_.getBottom(), 1.0f);

        const float inY = dbToY(smoothedInputPeakDb_);
        const float outY = dbToY(smoothedOutputPeakDb_);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.65f));
        g.drawLine(plotArea_.getX(), inY, plotArea_.getRight(), inY, 1.0f);
        g.setColour(accent.withAlpha(0.65f));
        g.drawLine(plotArea_.getX(), outY, plotArea_.getRight(), outY, 1.0f);
    }

    auto font = FontManager::getInstance().getUIFont(9.0f);
    g.setFont(font);
    g.setColour(text.withAlpha(0.85f));
    g.drawFittedText("THR " + juce::String(thresholdDb_, 1), plotArea_.toNearestInt().reduced(5, 4),
                     juce::Justification::topLeft, 1);

    auto meter = meterArea.reduced(2.0f, 0.0f);
    g.setColour(border.withAlpha(0.55f));
    g.drawRoundedRectangle(meter, 3.0f, 1.0f);

    auto grMeter = meter.removeFromLeft(22.0f).reduced(5.0f, 18.0f);
    auto peakMeter = meter.reduced(2.0f, 18.0f);

    g.setColour(border.withAlpha(0.25f));
    g.fillRoundedRectangle(grMeter, 2.0f);
    const float gr01 = juce::jlimit(0.0f, 1.0f, smoothedGainReductionDb_ / 24.0f);
    auto grFill = grMeter.withTrimmedTop(grMeter.getHeight() * (1.0f - gr01));
    g.setColour(grColour.withAlpha(0.9f));
    g.fillRoundedRectangle(grFill, 2.0f);

    g.setColour(border.withAlpha(0.25f));
    g.fillRoundedRectangle(peakMeter, 2.0f);
    const float out01 =
        juce::jmap(juce::jlimit(kMinDb, 0.0f, smoothedOutputPeakDb_), kMinDb, 0.0f, 0.0f, 1.0f);
    auto outFill = peakMeter.withTrimmedTop(peakMeter.getHeight() * (1.0f - out01));
    g.setColour(accent.withAlpha(0.8f));
    g.fillRoundedRectangle(outFill, 2.0f);

    const float key01 =
        juce::jmap(juce::jlimit(kMinDb, 0.0f, smoothedKeyPeakDb_), kMinDb, 0.0f, 0.0f, 1.0f);
    const float keyY = peakMeter.getBottom() - peakMeter.getHeight() * key01;
    g.setColour(keyColour.withAlpha(0.9f));
    g.drawLine(peakMeter.getX(), keyY, peakMeter.getRight(), keyY, 1.4f);

    g.setColour(text.withAlpha(0.85f));
    g.drawFittedText("GR", grMeter.withTrimmedTop(-17.0f).toNearestInt(),
                     juce::Justification::centredTop, 1);
    g.drawFittedText("OUT", peakMeter.withTrimmedTop(-17.0f).toNearestInt(),
                     juce::Justification::centredTop, 1);
    g.drawFittedText("-" + juce::String(smoothedGainReductionDb_, 1),
                     grMeter.withTrimmedBottom(-17.0f).toNearestInt(),
                     juce::Justification::centredBottom, 1);
    g.drawFittedText(formatDb(smoothedOutputPeakDb_),
                     peakMeter.withTrimmedBottom(-17.0f).toNearestInt(),
                     juce::Justification::centredBottom, 1);
}

const CompiledPresentationSpec& getMagdaCompressorPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaCompressorCompiledPlugin::xmlTypeName,
        .layoutCellCount = 15,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledCompressorCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

void CompiledCompressorCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaCompressorCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
