#include "CompiledMultibandCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaMultibandCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;
constexpr float kHandlePickPx = 8.0f;  // mouse must be within 8 px of a line

// Plugin slot range limits — must mirror the host slot info in
// MagdaMultibandCompiledPlugin::buildHostParameters. Drags clamp to
// these so the user can't push a crossover into a value the host param
// would refuse to accept.
constexpr float kLowXoMin = 40.0f;
constexpr float kLowXoMax = 500.0f;
constexpr float kHighXoMin = 500.0f;
constexpr float kHighXoMax = 8000.0f;
constexpr float kMinXoGapHz = 5.0f;  // keep low strictly below high

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float logLerp(float t, float lo, float hi) {
    const float clamped = juce::jlimit(0.0f, 1.0f, t);
    return lo * std::exp(clamped * std::log(hi / lo));
}

float invLogLerp(float v, float lo, float hi) {
    const float clamped = juce::jlimit(lo, hi, v);
    return std::log(clamped / lo) / std::log(hi / lo);
}

}  // namespace

CompiledMultibandCurveView::CompiledMultibandCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    startTimer(kPollMs);
}

void CompiledMultibandCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledMultibandCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

void CompiledMultibandCurveView::timerCallback() {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;

    float low = lowXoHz_;
    float high = highXoHz_;
    if (compiledPlugin_ != nullptr) {
        if (auto* p = compiledPlugin_->getSlotParameter(Mb::kLowXoSlot))
            low = compiledPlugin_->nativeValueToDisplayValue(Mb::kLowXoSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Mb::kHighXoSlot))
            high =
                compiledPlugin_->nativeValueToDisplayValue(Mb::kHighXoSlot, p->getCurrentValue());
    } else {
        low = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, low);
        high = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, high);
    }

    if (std::fabs(low - lowXoHz_) > 0.5f || std::fabs(high - highXoHz_) > 0.5f) {
        lowXoHz_ = low;
        highXoHz_ = high;
        repaint();
    }
}

void CompiledMultibandCurveView::resampleFromPlugin() {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    lowXoHz_ = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, lowXoHz_);
    highXoHz_ = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, highXoHz_);
}

float CompiledMultibandCurveView::xToFreq(float x) const {
    if (plotArea_.getWidth() <= 0.0f)
        return kMinFreq;
    const float t = (x - plotArea_.getX()) / plotArea_.getWidth();
    return logLerp(t, kMinFreq, kMaxFreq);
}

float CompiledMultibandCurveView::freqToX(float hz) const {
    const float t = invLogLerp(hz, kMinFreq, kMaxFreq);
    return plotArea_.getX() + t * plotArea_.getWidth();
}

CompiledMultibandCurveView::Handle CompiledMultibandCurveView::pickHandle(float x) const {
    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);
    const float dLow = std::fabs(x - lowX);
    const float dHigh = std::fabs(x - highX);
    if (dLow > kHandlePickPx && dHigh > kHandlePickPx)
        return Handle::None;
    return (dLow <= dHigh) ? Handle::LowXo : Handle::HighXo;
}

void CompiledMultibandCurveView::mouseMove(const juce::MouseEvent& e) {
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

void CompiledMultibandCurveView::mouseExit(const juce::MouseEvent&) {
    if (hoveredHandle_ != Handle::None && draggedHandle_ == Handle::None) {
        hoveredHandle_ = Handle::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompiledMultibandCurveView::mouseDown(const juce::MouseEvent& e) {
    const auto picked = pickHandle(static_cast<float>(e.x));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledMultibandCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

    const float rawHz = xToFreq(static_cast<float>(e.x));

    if (draggedHandle_ == Handle::LowXo) {
        // Clamp to the slot's own range AND keep a small gap below the
        // current high crossover so dragging never makes Low cross High.
        const float ceiling = std::min(kLowXoMax, highXoHz_ - kMinXoGapHz);
        const float clamped = juce::jlimit(kLowXoMin, ceiling, rawHz);
        if (std::fabs(clamped - lowXoHz_) > 0.5f) {
            lowXoHz_ = clamped;
            if (onParameterChanged)
                onParameterChanged(Mb::kLowXoSlot, clamped);
            repaint();
        }
    } else {
        const float floor_ = std::max(kHighXoMin, lowXoHz_ + kMinXoGapHz);
        const float clamped = juce::jlimit(floor_, kHighXoMax, rawHz);
        if (std::fabs(clamped - highXoHz_) > 0.5f) {
            highXoHz_ = clamped;
            if (onParameterChanged)
                onParameterChanged(Mb::kHighXoSlot, clamped);
            repaint();
        }
    }
}

void CompiledMultibandCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedHandle_ = Handle::None;
    // Refresh hover so the cursor reverts to normal once the mouse
    // leaves the handle's hit area after the drag ends.
    hoveredHandle_ = pickHandle(static_cast<float>(e.x));
    setMouseCursor(hoveredHandle_ == Handle::None ? juce::MouseCursor::NormalCursor
                                                  : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void CompiledMultibandCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    plotArea_ = plot;
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    juce::Graphics::ScopedSaveState clipGuard(g);
    g.reduceClipRegion(plot.toNearestInt());

    // Decade grid — keep the log axis legible.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(decade);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    // Tinted band fills. Three colours from the existing theme palette so
    // the bands are visually distinct without introducing a new colour
    // scheme. Alpha low so the bands read as background tints, not foreground.
    const auto lowColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    const auto midColour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    const auto highColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    constexpr float kBandFillAlpha = 0.22f;

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);

    g.setColour(lowColour.withAlpha(kBandFillAlpha));
    g.fillRect(
        juce::Rectangle<float>(plot.getX(), plot.getY(), lowX - plot.getX(), plot.getHeight()));
    g.setColour(midColour.withAlpha(kBandFillAlpha));
    g.fillRect(juce::Rectangle<float>(lowX, plot.getY(), highX - lowX, plot.getHeight()));
    g.setColour(highColour.withAlpha(kBandFillAlpha));
    g.fillRect(
        juce::Rectangle<float>(highX, plot.getY(), plot.getRight() - highX, plot.getHeight()));

    // Crossover handles. Brighter when hovered or being dragged so the
    // user knows what they're about to grab.
    auto drawHandle = [&](float x, Handle which, juce::Colour base) {
        const bool active = (which == hoveredHandle_) || (which == draggedHandle_);
        g.setColour(base.withAlpha(active ? 0.95f : 0.65f));
        const float thickness = active ? 2.0f : 1.0f;
        g.fillRect(
            juce::Rectangle<float>(x - thickness * 0.5f, plot.getY(), thickness, plot.getHeight()));
    };
    drawHandle(lowX, Handle::LowXo, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    drawHandle(highX, Handle::HighXo, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

    // Frequency labels above each handle when active. Helps drag precision.
    auto drawLabel = [&](float x, float hz, Handle which) {
        if (which != hoveredHandle_ && which != draggedHandle_)
            return;
        const auto text = (hz >= 1000.0f) ? juce::String(hz / 1000.0f, 2) + " kHz"
                                          : juce::String(static_cast<int>(std::round(hz))) + " Hz";
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(11.0f);
        const int textW = 64;
        const int textH = 14;
        const float lx =
            juce::jlimit(plot.getX() + 2.0f, plot.getRight() - textW - 2.0f, x - textW * 0.5f);
        g.drawText(text,
                   juce::Rectangle<float>(lx, plot.getY() + 2.0f, textW, textH).toNearestInt(),
                   juce::Justification::centred);
    };
    drawLabel(lowX, lowXoHz_, Handle::LowXo);
    drawLabel(highX, highXoHz_, Handle::HighXo);
}

}  // namespace magda::daw::ui
