#include "compiled/CompiledGateCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaGateExpanderCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kMinDb = -80.0f;
constexpr float kMaxDb = 0.0f;
constexpr float kPlotPad = 8.0f;
constexpr int kPollMs = 33;
constexpr float kHandlePickPx = 8.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

// Gate transfer function: output dB for a given input dB.
float gateOutputDb(float inputDb, float thresholdDb, float ratio, float rangeDb) {
    if (inputDb >= thresholdDb)
        return inputDb;
    const float over = thresholdDb - inputDb;
    const float attenuation = std::min(rangeDb, over * (std::max(1.0f, ratio) - 1.0f));
    return inputDb - attenuation;
}

}  // namespace

CompiledGateCurveView::CompiledGateCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(true, false);
    startTimer(kPollMs);
}

void CompiledGateCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledGateCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledGateCurveView::timerCallback() {
    using Gate = magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin;

    auto read = [this](int slot, float fallback) -> float {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    const float threshold = compiledPlugin_ != nullptr
                                ? read(Gate::kThresholdSlot, thresholdDb_)
                                : valueForSlot(deviceSnapshot_, Gate::kThresholdSlot, thresholdDb_);
    const float ratio = compiledPlugin_ != nullptr
                            ? read(Gate::kRatioSlot, ratio_)
                            : valueForSlot(deviceSnapshot_, Gate::kRatioSlot, ratio_);
    const float range = compiledPlugin_ != nullptr
                            ? read(Gate::kRangeSlot, rangeDb_)
                            : valueForSlot(deviceSnapshot_, Gate::kRangeSlot, rangeDb_);

    const bool moved = std::fabs(threshold - thresholdDb_) > 0.001f ||
                       std::fabs(ratio - ratio_) > 0.001f || std::fabs(range - rangeDb_) > 0.001f;
    if (moved) {
        thresholdDb_ = threshold;
        ratio_ = ratio;
        rangeDb_ = range;
        repaint();
    }
}

void CompiledGateCurveView::resampleFromDevice() {
    using Gate = magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin;
    thresholdDb_ = valueForSlot(deviceSnapshot_, Gate::kThresholdSlot, thresholdDb_);
    ratio_ = valueForSlot(deviceSnapshot_, Gate::kRatioSlot, ratio_);
    rangeDb_ = valueForSlot(deviceSnapshot_, Gate::kRangeSlot, rangeDb_);
}

float CompiledGateCurveView::xToDb(float x) const {
    if (plotArea_.getWidth() <= 0.0f)
        return kMinDb;
    const float t = (x - plotArea_.getX()) / plotArea_.getWidth();
    return juce::jmap(juce::jlimit(0.0f, 1.0f, t), kMinDb, kMaxDb);
}

float CompiledGateCurveView::dbToX(float db) const {
    const float t = juce::jmap(juce::jlimit(kMinDb, kMaxDb, db), kMinDb, kMaxDb, 0.0f, 1.0f);
    return plotArea_.getX() + t * plotArea_.getWidth();
}

float CompiledGateCurveView::dbToY(float db) const {
    const float t = juce::jmap(juce::jlimit(kMinDb, kMaxDb, db), kMinDb, kMaxDb, 0.0f, 1.0f);
    return plotArea_.getBottom() - t * plotArea_.getHeight();
}

CompiledGateCurveView::Handle CompiledGateCurveView::pickHandle(float x) const {
    return std::fabs(x - dbToX(thresholdDb_)) <= kHandlePickPx ? Handle::Threshold : Handle::None;
}

void CompiledGateCurveView::mouseMove(const juce::MouseEvent& e) {
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

void CompiledGateCurveView::mouseExit(const juce::MouseEvent&) {
    if (draggedHandle_ == Handle::None) {
        hoveredHandle_ = Handle::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompiledGateCurveView::mouseDown(const juce::MouseEvent& e) {
    const auto picked = pickHandle(static_cast<float>(e.x));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledGateCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Gate = magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin;
    if (draggedHandle_ != Handle::Threshold)
        return;

    const float threshold = juce::jlimit(kMinDb, 0.0f, xToDb(static_cast<float>(e.x)));
    if (std::fabs(threshold - thresholdDb_) > 0.05f) {
        thresholdDb_ = threshold;
        if (onParameterChanged)
            onParameterChanged(Gate::kThresholdSlot, threshold);
        repaint();
    }
}

void CompiledGateCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedHandle_ = Handle::None;
    hoveredHandle_ = pickHandle(static_cast<float>(e.x));
    setMouseCursor(hoveredHandle_ == Handle::None ? juce::MouseCursor::NormalCursor
                                                  : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void CompiledGateCurveView::mouseWheelMove(const juce::MouseEvent& e,
                                           const juce::MouseWheelDetails& wheel) {
    using Gate = magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin;
    if (!plotArea_.contains(e.position))
        return;

    const float newRatio = juce::jlimit(1.0f, 50.0f, ratio_ * std::pow(2.0f, wheel.deltaY * 1.5f));
    if (std::fabs(newRatio - ratio_) > 0.01f) {
        ratio_ = newRatio;
        if (onParameterChanged)
            onParameterChanged(Gate::kRatioSlot, ratio_);
        repaint();
    }
}

void CompiledGateCurveView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    plotArea_ = getLocalBounds().toFloat().reduced(kPlotPad);
    if (plotArea_.getWidth() < 16.0f || plotArea_.getHeight() < 16.0f)
        return;

    const juce::Colour borderColour(0xFF444444);
    const juce::Colour gridColour(0xFF333333);
    const juce::Colour curveColour(0xFF00D4FF);   // cyan — gate curve
    const juce::Colour threshColour(0xFFFF8C00);  // orange — threshold

    g.setColour(borderColour);
    g.drawRect(plotArea_, 1.0f);

    {
        juce::Graphics::ScopedSaveState clipGuard(g);
        g.reduceClipRegion(plotArea_.toNearestInt());

        // Grid lines at -60, -48, -36, -24, -12 dB
        g.setColour(gridColour);
        for (float db : {-60.0f, -48.0f, -36.0f, -24.0f, -12.0f}) {
            const float x = dbToX(db);
            const float y = dbToY(db);
            g.drawVerticalLine(static_cast<int>(std::round(x)), plotArea_.getY(),
                               plotArea_.getBottom());
            g.drawHorizontalLine(static_cast<int>(std::round(y)), plotArea_.getX(),
                                 plotArea_.getRight());
        }

        // Unity diagonal (1:1 reference)
        juce::Path diagonal;
        diagonal.startNewSubPath(dbToX(kMinDb), dbToY(kMinDb));
        diagonal.lineTo(dbToX(kMaxDb), dbToY(kMaxDb));
        g.setColour(borderColour.brighter(0.3f));
        g.strokePath(diagonal, juce::PathStrokeType(1.0f));

        // Transfer curve
        juce::Path curve;
        const int samples = juce::jmax(64, static_cast<int>(std::ceil(plotArea_.getWidth())));
        for (int i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
            const float inputDb = juce::jmap(t, kMinDb, kMaxDb);
            const float outputDb = gateOutputDb(inputDb, thresholdDb_, ratio_, rangeDb_);
            const float x = plotArea_.getX() + t * plotArea_.getWidth();
            const float y = dbToY(outputDb);
            if (i == 0)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
        }
        g.setColour(curveColour.withAlpha(0.9f));
        g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Threshold line
        const float threshX = dbToX(thresholdDb_);
        const bool threshHot =
            hoveredHandle_ == Handle::Threshold || draggedHandle_ == Handle::Threshold;
        g.setColour(threshColour.withAlpha(threshHot ? 0.95f : 0.7f));
        g.drawLine(threshX, plotArea_.getY(), threshX, plotArea_.getBottom(), 1.0f);
        g.fillEllipse(threshX - 4.0f, plotArea_.getCentreY() - 4.0f, 8.0f, 8.0f);
    }

    // Labels
    auto font = FontManager::getInstance().getUIFont(9.0f);
    g.setFont(font);
    g.setColour(juce::Colour(0xFF888888));
    g.drawFittedText("THR " + juce::String(thresholdDb_, 1) + " dB  R " + juce::String(ratio_, 1),
                     plotArea_.toNearestInt().reduced(5, 4), juce::Justification::topLeft, 1);
}

const CompiledPresentationSpec& getMagdaGatePresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin::xmlTypeName,
        .layoutCellCount = 6,
        .layoutCellsPerRow = 3,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledGateCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

void CompiledGateCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaGateExpanderCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
