#include "compiled/CompiledMultibandCurveView.hpp"

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
constexpr float kThresholdPickPx = 6.0f;

// Plugin slot range limits — must mirror the host slot info in
// MagdaMultibandCompiledPlugin::buildHostParameters. Drags clamp to
// these so the user can't push a control into a value the host param
// would refuse to accept.
constexpr float kLowXoMin = 40.0f;
constexpr float kLowXoMax = 500.0f;
constexpr float kHighXoMin = 500.0f;
constexpr float kHighXoMax = 8000.0f;
constexpr float kMinXoGapHz = 5.0f;  // keep low strictly below high
constexpr float kThreshAboveMin = -60.0f;
constexpr float kThreshAboveMax = 0.0f;
constexpr float kThreshBelowMin = -80.0f;
constexpr float kThreshBelowMax = 0.0f;
constexpr float kMinThresholdGapDb = 1.0f;

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
    auto threshAbove = threshAboveDb_;
    auto threshBelow = threshBelowDb_;
    auto ratios = ratios_;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    if (compiledPlugin_ != nullptr) {
        low = readPluginSlot(Mb::kLowXoSlot, low);
        high = readPluginSlot(Mb::kHighXoSlot, high);
        threshAbove = {{readPluginSlot(Mb::kLowThreshAboveSlot, threshAbove[0]),
                        readPluginSlot(Mb::kMidThreshAboveSlot, threshAbove[1]),
                        readPluginSlot(Mb::kHighThreshAboveSlot, threshAbove[2])}};
        threshBelow = {{readPluginSlot(Mb::kLowThreshBelowSlot, threshBelow[0]),
                        readPluginSlot(Mb::kMidThreshBelowSlot, threshBelow[1]),
                        readPluginSlot(Mb::kHighThreshBelowSlot, threshBelow[2])}};
        ratios = {{readPluginSlot(Mb::kLowRatioSlot, ratios[0]),
                   readPluginSlot(Mb::kMidRatioSlot, ratios[1]),
                   readPluginSlot(Mb::kHighRatioSlot, ratios[2])}};
    } else {
        low = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, low);
        high = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, high);
        threshAbove = {{valueForSlot(deviceSnapshot_, Mb::kLowThreshAboveSlot, threshAbove[0]),
                        valueForSlot(deviceSnapshot_, Mb::kMidThreshAboveSlot, threshAbove[1]),
                        valueForSlot(deviceSnapshot_, Mb::kHighThreshAboveSlot, threshAbove[2])}};
        threshBelow = {{valueForSlot(deviceSnapshot_, Mb::kLowThreshBelowSlot, threshBelow[0]),
                        valueForSlot(deviceSnapshot_, Mb::kMidThreshBelowSlot, threshBelow[1]),
                        valueForSlot(deviceSnapshot_, Mb::kHighThreshBelowSlot, threshBelow[2])}};
        ratios = {{valueForSlot(deviceSnapshot_, Mb::kLowRatioSlot, ratios[0]),
                   valueForSlot(deviceSnapshot_, Mb::kMidRatioSlot, ratios[1]),
                   valueForSlot(deviceSnapshot_, Mb::kHighRatioSlot, ratios[2])}};
    }

    const bool dynamicsMoved = std::fabs(threshAbove[0] - threshAboveDb_[0]) > 0.05f ||
                               std::fabs(threshAbove[1] - threshAboveDb_[1]) > 0.05f ||
                               std::fabs(threshAbove[2] - threshAboveDb_[2]) > 0.05f ||
                               std::fabs(threshBelow[0] - threshBelowDb_[0]) > 0.05f ||
                               std::fabs(threshBelow[1] - threshBelowDb_[1]) > 0.05f ||
                               std::fabs(threshBelow[2] - threshBelowDb_[2]) > 0.05f ||
                               std::fabs(ratios[0] - ratios_[0]) > 0.01f ||
                               std::fabs(ratios[1] - ratios_[1]) > 0.01f ||
                               std::fabs(ratios[2] - ratios_[2]) > 0.01f;

    if (std::fabs(low - lowXoHz_) > 0.5f || std::fabs(high - highXoHz_) > 0.5f || dynamicsMoved) {
        lowXoHz_ = low;
        highXoHz_ = high;
        threshAboveDb_ = threshAbove;
        threshBelowDb_ = threshBelow;
        ratios_ = ratios;
        repaint();
    }
}

void CompiledMultibandCurveView::resampleFromPlugin() {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    lowXoHz_ = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, lowXoHz_);
    highXoHz_ = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, highXoHz_);
    threshAboveDb_ = {{valueForSlot(deviceSnapshot_, Mb::kLowThreshAboveSlot, threshAboveDb_[0]),
                       valueForSlot(deviceSnapshot_, Mb::kMidThreshAboveSlot, threshAboveDb_[1]),
                       valueForSlot(deviceSnapshot_, Mb::kHighThreshAboveSlot, threshAboveDb_[2])}};
    threshBelowDb_ = {{valueForSlot(deviceSnapshot_, Mb::kLowThreshBelowSlot, threshBelowDb_[0]),
                       valueForSlot(deviceSnapshot_, Mb::kMidThreshBelowSlot, threshBelowDb_[1]),
                       valueForSlot(deviceSnapshot_, Mb::kHighThreshBelowSlot, threshBelowDb_[2])}};
    ratios_ = {{valueForSlot(deviceSnapshot_, Mb::kLowRatioSlot, ratios_[0]),
                valueForSlot(deviceSnapshot_, Mb::kMidRatioSlot, ratios_[1]),
                valueForSlot(deviceSnapshot_, Mb::kHighRatioSlot, ratios_[2])}};
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

float CompiledMultibandCurveView::dbToY(float db) const {
    if (plotArea_.getHeight() <= 0.0f)
        return 0.0f;
    const float t = juce::jlimit(0.0f, 1.0f, (-db) / 80.0f);
    return plotArea_.getY() + t * plotArea_.getHeight();
}

float CompiledMultibandCurveView::yToDb(float y) const {
    if (plotArea_.getHeight() <= 0.0f)
        return 0.0f;
    const float t = juce::jlimit(0.0f, 1.0f, (y - plotArea_.getY()) / plotArea_.getHeight());
    return -80.0f * t;
}

bool CompiledMultibandCurveView::isThresholdHandle(Handle handle) {
    switch (handle) {
        case Handle::LowThreshAbove:
        case Handle::LowThreshBelow:
        case Handle::MidThreshAbove:
        case Handle::MidThreshBelow:
        case Handle::HighThreshAbove:
        case Handle::HighThreshBelow:
            return true;
        default:
            return false;
    }
}

int CompiledMultibandCurveView::thresholdBandIndex(Handle handle) {
    switch (handle) {
        case Handle::LowThreshAbove:
        case Handle::LowThreshBelow:
            return 0;
        case Handle::MidThreshAbove:
        case Handle::MidThreshBelow:
            return 1;
        case Handle::HighThreshAbove:
        case Handle::HighThreshBelow:
            return 2;
        default:
            return -1;
    }
}

bool CompiledMultibandCurveView::isAboveThresholdHandle(Handle handle) {
    switch (handle) {
        case Handle::LowThreshAbove:
        case Handle::MidThreshAbove:
        case Handle::HighThreshAbove:
            return true;
        default:
            return false;
    }
}

int CompiledMultibandCurveView::thresholdSlotForHandle(Handle handle) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    switch (handle) {
        case Handle::LowThreshAbove:
            return Mb::kLowThreshAboveSlot;
        case Handle::LowThreshBelow:
            return Mb::kLowThreshBelowSlot;
        case Handle::MidThreshAbove:
            return Mb::kMidThreshAboveSlot;
        case Handle::MidThreshBelow:
            return Mb::kMidThreshBelowSlot;
        case Handle::HighThreshAbove:
            return Mb::kHighThreshAboveSlot;
        case Handle::HighThreshBelow:
            return Mb::kHighThreshBelowSlot;
        default:
            return -1;
    }
}

CompiledMultibandCurveView::Handle CompiledMultibandCurveView::pickHandle(float x, float y) const {
    if (plotArea_.getWidth() <= 0.0f || plotArea_.getHeight() <= 0.0f)
        return Handle::None;

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);
    const std::array<float, 4> bandEdges{{plotArea_.getX(), lowX, highX, plotArea_.getRight()}};
    const std::array<Handle, 3> aboveHandles{
        {Handle::LowThreshAbove, Handle::MidThreshAbove, Handle::HighThreshAbove}};
    const std::array<Handle, 3> belowHandles{
        {Handle::LowThreshBelow, Handle::MidThreshBelow, Handle::HighThreshBelow}};

    Handle nearestThreshold = Handle::None;
    float nearestThresholdDistance = kThresholdPickPx + 1.0f;
    for (int band = 0; band < 3; ++band) {
        const float x0 = bandEdges[static_cast<size_t>(band)] - 2.0f;
        const float x1 = bandEdges[static_cast<size_t>(band + 1)] + 2.0f;
        if (x < x0 || x > x1)
            continue;

        const float yAbove = dbToY(threshAboveDb_[static_cast<size_t>(band)]);
        const float yBelow = dbToY(threshBelowDb_[static_cast<size_t>(band)]);
        const float dAbove = std::fabs(y - yAbove);
        const float dBelow = std::fabs(y - yBelow);
        if (dAbove <= kThresholdPickPx && dAbove < nearestThresholdDistance) {
            nearestThreshold = aboveHandles[static_cast<size_t>(band)];
            nearestThresholdDistance = dAbove;
        }
        if (dBelow <= kThresholdPickPx && dBelow < nearestThresholdDistance) {
            nearestThreshold = belowHandles[static_cast<size_t>(band)];
            nearestThresholdDistance = dBelow;
        }
    }
    if (nearestThreshold != Handle::None)
        return nearestThreshold;

    const float dLow = std::fabs(x - lowX);
    const float dHigh = std::fabs(x - highX);
    if (dLow > kHandlePickPx && dHigh > kHandlePickPx)
        return Handle::None;
    return (dLow <= dHigh) ? Handle::LowXo : Handle::HighXo;
}

void CompiledMultibandCurveView::mouseMove(const juce::MouseEvent& e) {
    if (draggedHandle_ != Handle::None)
        return;
    const auto picked = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    if (picked != hoveredHandle_) {
        hoveredHandle_ = picked;
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                                              : (isThresholdHandle(picked)
                                                     ? juce::MouseCursor::UpDownResizeCursor
                                                     : juce::MouseCursor::LeftRightResizeCursor));
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
    const auto picked = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor(isThresholdHandle(picked) ? juce::MouseCursor::UpDownResizeCursor
                                             : juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledMultibandCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

    if (isThresholdHandle(draggedHandle_)) {
        const int band = thresholdBandIndex(draggedHandle_);
        if (band < 0)
            return;

        const auto index = static_cast<size_t>(band);
        const bool isAbove = isAboveThresholdHandle(draggedHandle_);
        const float rawDb = yToDb(static_cast<float>(e.y));
        float clamped = rawDb;
        if (isAbove) {
            const float floor =
                std::min(kThreshAboveMax,
                         std::max(kThreshAboveMin, threshBelowDb_[index] + kMinThresholdGapDb));
            clamped = juce::jlimit(floor, kThreshAboveMax, rawDb);
        } else {
            const float ceiling =
                std::max(kThreshBelowMin,
                         std::min(kThreshBelowMax, threshAboveDb_[index] - kMinThresholdGapDb));
            clamped = juce::jlimit(kThreshBelowMin, ceiling, rawDb);
        }

        auto& target = isAbove ? threshAboveDb_[index] : threshBelowDb_[index];
        if (std::fabs(clamped - target) > 0.05f) {
            target = clamped;
            const int slot = thresholdSlotForHandle(draggedHandle_);
            if (slot >= 0 && onParameterChanged)
                onParameterChanged(slot, clamped);
            repaint();
        }
        return;
    }

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
    hoveredHandle_ = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    setMouseCursor(hoveredHandle_ == Handle::None
                       ? juce::MouseCursor::NormalCursor
                       : (isThresholdHandle(hoveredHandle_)
                              ? juce::MouseCursor::UpDownResizeCursor
                              : juce::MouseCursor::LeftRightResizeCursor));
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

    const std::array<float, 4> bandEdges{{plot.getX(), lowX, highX, plot.getRight()}};
    const std::array<juce::Colour, 3> bandColours{{lowColour, midColour, highColour}};
    const std::array<Handle, 3> aboveHandles{
        {Handle::LowThreshAbove, Handle::MidThreshAbove, Handle::HighThreshAbove}};
    const std::array<Handle, 3> belowHandles{
        {Handle::LowThreshBelow, Handle::MidThreshBelow, Handle::HighThreshBelow}};

    auto drawThresholdLabel = [&](float x0, float x1, float y, float db, Handle handle,
                                  juce::Colour colour) {
        if (handle != hoveredHandle_ && handle != draggedHandle_)
            return;

        const auto text = juce::String(db, 1) + " dB";
        g.setColour(colour.withAlpha(0.95f));
        g.setFont(11.0f);
        const int textW = 56;
        const int textH = 14;
        const float lx = juce::jlimit(plot.getX() + 2.0f, plot.getRight() - textW - 2.0f,
                                      (x0 + x1 - static_cast<float>(textW)) * 0.5f);
        const float ly =
            juce::jlimit(plot.getY() + 2.0f, plot.getBottom() - textH - 2.0f, y - textH - 2.0f);
        g.drawText(
            text,
            juce::Rectangle<float>(lx, ly, static_cast<float>(textW), static_cast<float>(textH))
                .toNearestInt(),
            juce::Justification::centred);
    };

    for (int band = 0; band < 3; ++band) {
        const float x0 = bandEdges[static_cast<size_t>(band)];
        const float x1 = bandEdges[static_cast<size_t>(band + 1)];
        if (x1 <= x0 + 2.0f)
            continue;

        const auto index = static_cast<size_t>(band);
        const float yAbove = dbToY(threshAboveDb_[index]);
        const float yBelow = dbToY(threshBelowDb_[index]);
        const float ratioNorm = juce::jlimit(0.0f, 1.0f, (ratios_[index] - 1.0f) / 19.0f);
        const auto colour = bandColours[index];
        const auto aboveHandle = aboveHandles[index];
        const auto belowHandle = belowHandles[index];
        const bool aboveActive = aboveHandle == hoveredHandle_ || aboveHandle == draggedHandle_;
        const bool belowActive = belowHandle == hoveredHandle_ || belowHandle == draggedHandle_;
        const auto aboveColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
        const auto belowColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT);

        g.setColour(colour.withAlpha(0.04f + ratioNorm * 0.10f));
        g.fillRect(juce::Rectangle<float>(x0, plot.getY(), x1 - x0,
                                          juce::jmax(0.0f, yAbove - plot.getY())));
        g.fillRect(juce::Rectangle<float>(x0, yBelow, x1 - x0,
                                          juce::jmax(0.0f, plot.getBottom() - yBelow)));

        g.setColour(aboveColour.withAlpha(aboveActive ? 0.98f : 0.78f));
        g.drawLine(x0 + 2.0f, yAbove, x1 - 2.0f, yAbove, (aboveActive ? 2.2f : 1.4f) + ratioNorm);
        g.setColour(belowColour.withAlpha(belowActive ? 0.98f : 0.78f));
        g.drawLine(x0 + 2.0f, yBelow, x1 - 2.0f, yBelow, (belowActive ? 2.2f : 1.4f) + ratioNorm);

        drawThresholdLabel(x0, x1, yAbove, threshAboveDb_[index], aboveHandle, aboveColour);
        drawThresholdLabel(x0, x1, yBelow, threshBelowDb_[index], belowHandle, belowColour);
    }

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

const CompiledPresentationSpec& getMagdaMultibandPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin::xmlTypeName,
        .layoutCellCount = 18,
        .layoutCellsPerRow = 9,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledMultibandCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

void CompiledMultibandCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaMultibandCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
