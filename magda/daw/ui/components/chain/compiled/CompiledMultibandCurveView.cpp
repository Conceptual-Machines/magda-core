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
constexpr float kCollapseButtonSize = 18.0f;
constexpr float kCollapseButtonMargin = 4.0f;
constexpr float kMinFreq = 20.0f;
constexpr float kMaxFreq = 20000.0f;
constexpr float kHandlePickPx = 8.0f;
constexpr float kThresholdPickPx = 6.0f;

constexpr float kLowXoMin = 40.0f;
constexpr float kLowXoMax = 500.0f;
constexpr float kHighXoMin = 500.0f;
constexpr float kHighXoMax = 8000.0f;
constexpr float kMinXoGapHz = 5.0f;
constexpr float kThreshAboveMin = -60.0f;
constexpr float kThreshAboveMax = 0.0f;
constexpr float kThreshBelowMin = -80.0f;
constexpr float kThreshBelowMax = 0.0f;
constexpr float kThreshExpandMin = -80.0f;
constexpr float kThreshExpandMax = 0.0f;
constexpr float kMinThresholdGapDb = 1.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

float logLerp(float t, float lo, float hi) {
    return lo * std::exp(juce::jlimit(0.0f, 1.0f, t) * std::log(hi / lo));
}

float invLogLerp(float v, float lo, float hi) {
    return std::log(juce::jlimit(lo, hi, v) / lo) / std::log(hi / lo);
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
    auto threshExpand = threshExpandDb_;
    auto ratiosAbove = ratiosAbove_;
    auto ratiosBelow = ratiosBelow_;
    auto expandRatios = expandRatios_;

    auto readSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };
    auto readDevice = [this](int slot, float fallback) {
        return valueForSlot(deviceSnapshot_, slot, fallback);
    };
    const bool hasPlugin = compiledPlugin_ != nullptr;

    low = hasPlugin ? readSlot(Mb::kLowXoSlot, low) : readDevice(Mb::kLowXoSlot, low);
    high = hasPlugin ? readSlot(Mb::kHighXoSlot, high) : readDevice(Mb::kHighXoSlot, high);

    for (int b = 0; b < 3; ++b) {
        const auto idx = static_cast<size_t>(b);
        const int aboveSlot = (b == 0)   ? Mb::kLowThreshAboveSlot
                              : (b == 1) ? Mb::kMidThreshAboveSlot
                                         : Mb::kHighThreshAboveSlot;
        const int belowSlot = (b == 0)   ? Mb::kLowThreshBelowSlot
                              : (b == 1) ? Mb::kMidThreshBelowSlot
                                         : Mb::kHighThreshBelowSlot;
        const int expandSlot = (b == 0)   ? Mb::kLowThreshExpandSlot
                               : (b == 1) ? Mb::kMidThreshExpandSlot
                                          : Mb::kHighThreshExpandSlot;
        const int raSlot = (b == 0)   ? Mb::kLowRatioAboveSlot
                           : (b == 1) ? Mb::kMidRatioAboveSlot
                                      : Mb::kHighRatioAboveSlot;
        const int rbSlot = (b == 0)   ? Mb::kLowRatioBelowSlot
                           : (b == 1) ? Mb::kMidRatioBelowSlot
                                      : Mb::kHighRatioBelowSlot;
        const int erSlot = (b == 0)   ? Mb::kLowExpandRatioSlot
                           : (b == 1) ? Mb::kMidExpandRatioSlot
                                      : Mb::kHighExpandRatioSlot;

        threshAbove[idx] = hasPlugin ? readSlot(aboveSlot, threshAbove[idx])
                                     : readDevice(aboveSlot, threshAbove[idx]);
        threshBelow[idx] = hasPlugin ? readSlot(belowSlot, threshBelow[idx])
                                     : readDevice(belowSlot, threshBelow[idx]);
        threshExpand[idx] = hasPlugin ? readSlot(expandSlot, threshExpand[idx])
                                      : readDevice(expandSlot, threshExpand[idx]);
        ratiosAbove[idx] =
            hasPlugin ? readSlot(raSlot, ratiosAbove[idx]) : readDevice(raSlot, ratiosAbove[idx]);
        ratiosBelow[idx] =
            hasPlugin ? readSlot(rbSlot, ratiosBelow[idx]) : readDevice(rbSlot, ratiosBelow[idx]);
        expandRatios[idx] =
            hasPlugin ? readSlot(erSlot, expandRatios[idx]) : readDevice(erSlot, expandRatios[idx]);
    }

    bool changed = std::fabs(low - lowXoHz_) > 0.5f || std::fabs(high - highXoHz_) > 0.5f;
    for (int b = 0; b < 3 && !changed; ++b) {
        const auto idx = static_cast<size_t>(b);
        changed = std::fabs(threshAbove[idx] - threshAboveDb_[idx]) > 0.05f ||
                  std::fabs(threshBelow[idx] - threshBelowDb_[idx]) > 0.05f ||
                  std::fabs(threshExpand[idx] - threshExpandDb_[idx]) > 0.05f ||
                  std::fabs(ratiosAbove[idx] - ratiosAbove_[idx]) > 0.01f ||
                  std::fabs(ratiosBelow[idx] - ratiosBelow_[idx]) > 0.01f ||
                  std::fabs(expandRatios[idx] - expandRatios_[idx]) > 0.01f;
    }

    if (changed) {
        lowXoHz_ = low;
        highXoHz_ = high;
        threshAboveDb_ = threshAbove;
        threshBelowDb_ = threshBelow;
        threshExpandDb_ = threshExpand;
        ratiosAbove_ = ratiosAbove;
        ratiosBelow_ = ratiosBelow;
        expandRatios_ = expandRatios;
        repaint();
    }
}

void CompiledMultibandCurveView::resampleFromPlugin() {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    lowXoHz_ = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, lowXoHz_);
    highXoHz_ = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, highXoHz_);
    for (int b = 0; b < 3; ++b) {
        const auto idx = static_cast<size_t>(b);
        const int aboveSlot = (b == 0)   ? Mb::kLowThreshAboveSlot
                              : (b == 1) ? Mb::kMidThreshAboveSlot
                                         : Mb::kHighThreshAboveSlot;
        const int belowSlot = (b == 0)   ? Mb::kLowThreshBelowSlot
                              : (b == 1) ? Mb::kMidThreshBelowSlot
                                         : Mb::kHighThreshBelowSlot;
        const int expandSlot = (b == 0)   ? Mb::kLowThreshExpandSlot
                               : (b == 1) ? Mb::kMidThreshExpandSlot
                                          : Mb::kHighThreshExpandSlot;
        const int raSlot = (b == 0)   ? Mb::kLowRatioAboveSlot
                           : (b == 1) ? Mb::kMidRatioAboveSlot
                                      : Mb::kHighRatioAboveSlot;
        const int rbSlot = (b == 0)   ? Mb::kLowRatioBelowSlot
                           : (b == 1) ? Mb::kMidRatioBelowSlot
                                      : Mb::kHighRatioBelowSlot;
        const int erSlot = (b == 0)   ? Mb::kLowExpandRatioSlot
                           : (b == 1) ? Mb::kMidExpandRatioSlot
                                      : Mb::kHighExpandRatioSlot;

        threshAboveDb_[idx] = valueForSlot(deviceSnapshot_, aboveSlot, threshAboveDb_[idx]);
        threshBelowDb_[idx] = valueForSlot(deviceSnapshot_, belowSlot, threshBelowDb_[idx]);
        threshExpandDb_[idx] = valueForSlot(deviceSnapshot_, expandSlot, threshExpandDb_[idx]);
        ratiosAbove_[idx] = valueForSlot(deviceSnapshot_, raSlot, ratiosAbove_[idx]);
        ratiosBelow_[idx] = valueForSlot(deviceSnapshot_, rbSlot, ratiosBelow_[idx]);
        expandRatios_[idx] = valueForSlot(deviceSnapshot_, erSlot, expandRatios_[idx]);
    }
}

bool CompiledMultibandCurveView::wantsFullBody() const {
    return compiledPlugin_ != nullptr && compiledPlugin_->isCurveCollapsed();
}

int CompiledMultibandCurveView::bandAtX(float x) const {
    if (plotArea_.getWidth() <= 0.0f || x < plotArea_.getX() || x > plotArea_.getRight())
        return -1;
    if (x < freqToX(lowXoHz_))
        return 0;
    if (x < freqToX(highXoHz_))
        return 1;
    return 2;
}

int CompiledMultibandCurveView::ratioSlotForBand(int band, bool above) const {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (above) {
        switch (band) {
            case 0:
                return Mb::kLowRatioAboveSlot;
            case 1:
                return Mb::kMidRatioAboveSlot;
            case 2:
                return Mb::kHighRatioAboveSlot;
        }
    } else {
        switch (band) {
            case 0:
                return Mb::kLowRatioBelowSlot;
            case 1:
                return Mb::kMidRatioBelowSlot;
            case 2:
                return Mb::kHighRatioBelowSlot;
        }
    }
    return -1;
}

float CompiledMultibandCurveView::xToFreq(float x) const {
    if (plotArea_.getWidth() <= 0.0f)
        return kMinFreq;
    return logLerp((x - plotArea_.getX()) / plotArea_.getWidth(), kMinFreq, kMaxFreq);
}

float CompiledMultibandCurveView::freqToX(float hz) const {
    return plotArea_.getX() + invLogLerp(hz, kMinFreq, kMaxFreq) * plotArea_.getWidth();
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
    return -80.0f * juce::jlimit(0.0f, 1.0f, (y - plotArea_.getY()) / plotArea_.getHeight());
}

bool CompiledMultibandCurveView::isThresholdHandle(Handle h) {
    switch (h) {
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

bool CompiledMultibandCurveView::isExpandHandle(Handle h) {
    return h == Handle::LowThreshExpand || h == Handle::MidThreshExpand ||
           h == Handle::HighThreshExpand;
}

int CompiledMultibandCurveView::thresholdBandIndex(Handle h) {
    switch (h) {
        case Handle::LowThreshAbove:
        case Handle::LowThreshBelow:
        case Handle::LowThreshExpand:
            return 0;
        case Handle::MidThreshAbove:
        case Handle::MidThreshBelow:
        case Handle::MidThreshExpand:
            return 1;
        case Handle::HighThreshAbove:
        case Handle::HighThreshBelow:
        case Handle::HighThreshExpand:
            return 2;
        default:
            return -1;
    }
}

bool CompiledMultibandCurveView::isAboveThresholdHandle(Handle h) {
    return h == Handle::LowThreshAbove || h == Handle::MidThreshAbove ||
           h == Handle::HighThreshAbove;
}

int CompiledMultibandCurveView::thresholdSlotForHandle(Handle h) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    switch (h) {
        case Handle::LowThreshAbove:
            return Mb::kLowThreshAboveSlot;
        case Handle::LowThreshBelow:
            return Mb::kLowThreshBelowSlot;
        case Handle::LowThreshExpand:
            return Mb::kLowThreshExpandSlot;
        case Handle::MidThreshAbove:
            return Mb::kMidThreshAboveSlot;
        case Handle::MidThreshBelow:
            return Mb::kMidThreshBelowSlot;
        case Handle::MidThreshExpand:
            return Mb::kMidThreshExpandSlot;
        case Handle::HighThreshAbove:
            return Mb::kHighThreshAboveSlot;
        case Handle::HighThreshBelow:
            return Mb::kHighThreshBelowSlot;
        case Handle::HighThreshExpand:
            return Mb::kHighThreshExpandSlot;
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
    const std::array<Handle, 3> expandHandles{
        {Handle::LowThreshExpand, Handle::MidThreshExpand, Handle::HighThreshExpand}};

    Handle nearest = Handle::None;
    float nearestDist = kThresholdPickPx + 1.0f;

    for (int band = 0; band < 3; ++band) {
        const float x0 = bandEdges[static_cast<size_t>(band)] - 2.0f;
        const float x1 = bandEdges[static_cast<size_t>(band + 1)] + 2.0f;
        if (x < x0 || x > x1)
            continue;
        const auto idx = static_cast<size_t>(band);
        auto check = [&](float lineY, Handle h) {
            const float d = std::fabs(y - lineY);
            if (d <= kThresholdPickPx && d < nearestDist) {
                nearest = h;
                nearestDist = d;
            }
        };
        check(dbToY(threshAboveDb_[idx]), aboveHandles[idx]);
        check(dbToY(threshBelowDb_[idx]), belowHandles[idx]);
        check(dbToY(threshExpandDb_[idx]), expandHandles[idx]);
    }
    if (nearest != Handle::None)
        return nearest;

    const float dLow = std::fabs(x - lowX);
    const float dHigh = std::fabs(x - highX);
    if (dLow > kHandlePickPx && dHigh > kHandlePickPx)
        return Handle::None;
    return (dLow <= dHigh) ? Handle::LowXo : Handle::HighXo;
}

void CompiledMultibandCurveView::mouseMove(const juce::MouseEvent& e) {
    if (draggedHandle_ != Handle::None)
        return;

    const bool overChevron = collapseButtonArea_.contains(e.position);
    if (overChevron != collapseButtonHovered_) {
        collapseButtonHovered_ = overChevron;
        repaint();
    }
    if (overChevron) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        if (hoveredHandle_ != Handle::None) {
            hoveredHandle_ = Handle::None;
            repaint();
        }
        return;
    }

    const auto picked = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    if (picked != hoveredHandle_) {
        hoveredHandle_ = picked;
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                                              : (isThresholdHandle(picked) || isExpandHandle(picked)
                                                     ? juce::MouseCursor::UpDownResizeCursor
                                                     : juce::MouseCursor::LeftRightResizeCursor));
        repaint();
    }
}

void CompiledMultibandCurveView::mouseExit(const juce::MouseEvent&) {
    bool needsRepaint = false;
    if (hoveredHandle_ != Handle::None && draggedHandle_ == Handle::None) {
        hoveredHandle_ = Handle::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        needsRepaint = true;
    }
    if (collapseButtonHovered_) {
        collapseButtonHovered_ = false;
        needsRepaint = true;
    }
    if (ratioScrollBand_ != -1) {
        ratioScrollBand_ = -1;
        needsRepaint = true;
    }
    if (needsRepaint)
        repaint();
}

void CompiledMultibandCurveView::mouseDown(const juce::MouseEvent& e) {
    if (collapseButtonArea_.contains(e.position)) {
        if (compiledPlugin_ != nullptr) {
            compiledPlugin_->setCurveCollapsed(!compiledPlugin_->isCurveCollapsed());
            if (onLayoutChanged_)
                onLayoutChanged_();
            repaint();
        }
        return;
    }

    const auto picked = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor((isThresholdHandle(picked) || isExpandHandle(picked))
                       ? juce::MouseCursor::UpDownResizeCursor
                       : juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledMultibandCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

    if (isThresholdHandle(draggedHandle_) || isExpandHandle(draggedHandle_)) {
        const int band = thresholdBandIndex(draggedHandle_);
        if (band < 0)
            return;
        const auto idx = static_cast<size_t>(band);
        const float rawDb = yToDb(static_cast<float>(e.y));
        const int slot = thresholdSlotForHandle(draggedHandle_);
        float clamped = rawDb;

        if (isAboveThresholdHandle(draggedHandle_)) {
            const float floor =
                std::min(kThreshAboveMax,
                         std::max(kThreshAboveMin, threshBelowDb_[idx] + kMinThresholdGapDb));
            clamped = juce::jlimit(floor, kThreshAboveMax, rawDb);
            if (std::fabs(clamped - threshAboveDb_[idx]) > 0.05f) {
                threshAboveDb_[idx] = clamped;
                if (slot >= 0 && onParameterChanged)
                    onParameterChanged(slot, clamped);
                repaint();
            }
        } else if (isExpandHandle(draggedHandle_)) {
            const float ceiling =
                std::min(threshBelowDb_[idx] - kMinThresholdGapDb, kThreshExpandMax);
            clamped = juce::jlimit(kThreshExpandMin, ceiling, rawDb);
            if (std::fabs(clamped - threshExpandDb_[idx]) > 0.05f) {
                threshExpandDb_[idx] = clamped;
                if (slot >= 0 && onParameterChanged)
                    onParameterChanged(slot, clamped);
                repaint();
            }
        } else {
            // Below threshold: must stay below above, above expand.
            const float ceiling =
                std::max(kThreshBelowMin,
                         std::min(kThreshBelowMax, threshAboveDb_[idx] - kMinThresholdGapDb));
            const float floor =
                std::max(kThreshBelowMin, threshExpandDb_[idx] + kMinThresholdGapDb);
            clamped = juce::jlimit(floor, ceiling, rawDb);
            if (std::fabs(clamped - threshBelowDb_[idx]) > 0.05f) {
                threshBelowDb_[idx] = clamped;
                if (slot >= 0 && onParameterChanged)
                    onParameterChanged(slot, clamped);
                repaint();
            }
        }
        return;
    }

    // Crossover drag.
    const float rawHz = xToFreq(static_cast<float>(e.x));
    if (draggedHandle_ == Handle::LowXo) {
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
    hoveredHandle_ = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    setMouseCursor(hoveredHandle_ == Handle::None
                       ? juce::MouseCursor::NormalCursor
                       : ((isThresholdHandle(hoveredHandle_) || isExpandHandle(hoveredHandle_))
                              ? juce::MouseCursor::UpDownResizeCursor
                              : juce::MouseCursor::LeftRightResizeCursor));
    repaint();
}

void CompiledMultibandCurveView::mouseWheelMove(const juce::MouseEvent& e,
                                                const juce::MouseWheelDetails& wheel) {
    const int band = bandAtX(static_cast<float>(e.x));
    if (band < 0)
        return;

    const auto idx = static_cast<size_t>(band);
    const float yAbove = dbToY(threshAboveDb_[idx]);
    const float yBelow = dbToY(threshBelowDb_[idx]);
    const float mouseY = static_cast<float>(e.y);

    // Scroll above the "above" threshold adjusts ratioAbove;
    // scroll below the "below" threshold adjusts ratioBelow.
    const bool inAboveZone = mouseY < yAbove;
    const bool inBelowZone = mouseY > yBelow;
    if (!inAboveZone && !inBelowZone)
        return;

    constexpr float kRatioStep = 0.5f;
    constexpr float kRatioMin = 1.0f;
    constexpr float kRatioMax = 20.0f;
    const float delta = wheel.deltaY > 0.0f ? kRatioStep : -kRatioStep;

    if (inAboveZone) {
        const float newRatio = juce::jlimit(kRatioMin, kRatioMax, ratiosAbove_[idx] + delta);
        if (std::fabs(newRatio - ratiosAbove_[idx]) > 0.01f) {
            ratiosAbove_[idx] = newRatio;
            ratioScrollBand_ = band;
            ratioScrollAbove_ = true;
            if (onParameterChanged)
                onParameterChanged(ratioSlotForBand(band, true), newRatio);
            repaint();
        }
    } else {
        const float newRatio = juce::jlimit(kRatioMin, kRatioMax, ratiosBelow_[idx] + delta);
        if (std::fabs(newRatio - ratiosBelow_[idx]) > 0.01f) {
            ratiosBelow_[idx] = newRatio;
            ratioScrollBand_ = band;
            ratioScrollAbove_ = false;
            if (onParameterChanged)
                onParameterChanged(ratioSlotForBand(band, false), newRatio);
            repaint();
        }
    }
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

    // Decade grid.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(decade);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    const auto lowColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    const auto midColour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    const auto highColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);

    // Band fills.
    constexpr float kBandFillAlpha = 0.22f;
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
    const std::array<Handle, 3> expandHandles{
        {Handle::LowThreshExpand, Handle::MidThreshExpand, Handle::HighThreshExpand}};

    const auto aboveColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto belowColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT);
    const auto expandColour = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);

    auto drawThresholdLabel = [&](float x0, float x1, float y, float db, Handle handle,
                                  juce::Colour colour) {
        if (handle != hoveredHandle_ && handle != draggedHandle_)
            return;
        const auto text = juce::String(db, 1) + " dB";
        g.setColour(colour.withAlpha(0.95f));
        g.setFont(11.0f);
        constexpr int textW = 56, textH = 14;
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
        const auto idx = static_cast<size_t>(band);
        const float yAbove = dbToY(threshAboveDb_[idx]);
        const float yBelow = dbToY(threshBelowDb_[idx]);
        const float yExpand = dbToY(threshExpandDb_[idx]);
        const float ratioAboveNorm = juce::jlimit(0.0f, 1.0f, (ratiosAbove_[idx] - 1.0f) / 19.0f);
        const float ratioBelowNorm = juce::jlimit(0.0f, 1.0f, (ratiosBelow_[idx] - 1.0f) / 19.0f);
        const float expandNorm = juce::jlimit(0.0f, 1.0f, (expandRatios_[idx] - 1.0f) / 19.0f);
        const auto colour = bandColours[idx];
        const auto aboveH = aboveHandles[idx];
        const auto belowH = belowHandles[idx];
        const auto expandH = expandHandles[idx];
        const bool aboveActive = aboveH == hoveredHandle_ || aboveH == draggedHandle_;
        const bool belowActive = belowH == hoveredHandle_ || belowH == draggedHandle_;
        const bool expandActive = expandH == hoveredHandle_ || expandH == draggedHandle_;

        // Zone fills — intensity encodes the ratio for that zone.
        g.setColour(colour.withAlpha(0.04f + ratioAboveNorm * 0.10f));
        g.fillRect(juce::Rectangle<float>(x0, plot.getY(), x1 - x0,
                                          juce::jmax(0.0f, yAbove - plot.getY())));
        g.setColour(colour.withAlpha(0.04f + ratioBelowNorm * 0.10f));
        g.fillRect(juce::Rectangle<float>(x0, yBelow, x1 - x0, juce::jmax(0.0f, yExpand - yBelow)));
        // Expander zone gets a distinct tint.
        if (expandNorm > 0.0f) {
            g.setColour(expandColour.withAlpha(0.04f + expandNorm * 0.12f));
            g.fillRect(juce::Rectangle<float>(x0, yExpand, x1 - x0,
                                              juce::jmax(0.0f, plot.getBottom() - yExpand)));
        }

        // Threshold lines.
        g.setColour(aboveColour.withAlpha(aboveActive ? 0.98f : 0.78f));
        g.drawLine(x0 + 2.0f, yAbove, x1 - 2.0f, yAbove,
                   (aboveActive ? 2.2f : 1.4f) + ratioAboveNorm);
        g.setColour(belowColour.withAlpha(belowActive ? 0.98f : 0.78f));
        g.drawLine(x0 + 2.0f, yBelow, x1 - 2.0f, yBelow,
                   (belowActive ? 2.2f : 1.4f) + ratioBelowNorm);
        g.setColour(
            expandColour.withAlpha(expandActive ? 0.88f : (expandNorm > 0.01f ? 0.55f : 0.25f)));
        g.drawLine(x0 + 2.0f, yExpand, x1 - 2.0f, yExpand, expandActive ? 2.0f : 1.0f);

        drawThresholdLabel(x0, x1, yAbove, threshAboveDb_[idx], aboveH, aboveColour);
        drawThresholdLabel(x0, x1, yBelow, threshBelowDb_[idx], belowH, belowColour);
        drawThresholdLabel(x0, x1, yExpand, threshExpandDb_[idx], expandH, expandColour);
    }

    // Crossover handles.
    auto drawHandle = [&](float x, Handle which, juce::Colour base) {
        const bool active = (which == hoveredHandle_) || (which == draggedHandle_);
        g.setColour(base.withAlpha(active ? 0.95f : 0.65f));
        const float thickness = active ? 2.0f : 1.0f;
        g.fillRect(
            juce::Rectangle<float>(x - thickness * 0.5f, plot.getY(), thickness, plot.getHeight()));
    };
    drawHandle(lowX, Handle::LowXo, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    drawHandle(highX, Handle::HighXo, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

    // Frequency labels on active crossover handles.
    auto drawLabel = [&](float x, float hz, Handle which) {
        if (which != hoveredHandle_ && which != draggedHandle_)
            return;
        const auto text = (hz >= 1000.0f) ? juce::String(hz / 1000.0f, 2) + " kHz"
                                          : juce::String(static_cast<int>(std::round(hz))) + " Hz";
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(11.0f);
        constexpr int textW = 64, textH = 14;
        const float lx =
            juce::jlimit(plot.getX() + 2.0f, plot.getRight() - textW - 2.0f, x - textW * 0.5f);
        g.drawText(text,
                   juce::Rectangle<float>(lx, plot.getY() + 2.0f, textW, textH).toNearestInt(),
                   juce::Justification::centred);
    };
    drawLabel(lowX, lowXoHz_, Handle::LowXo);
    drawLabel(highX, highXoHz_, Handle::HighXo);

    // Ratio labels — permanent small text per band, amplified during scroll.
    const std::array<juce::Colour, 3> bandColours2{{lowColour, midColour, highColour}};
    g.setFont(10.0f);
    for (int band = 0; band < 3; ++band) {
        const float x0 = bandEdges[static_cast<size_t>(band)];
        const float x1 = bandEdges[static_cast<size_t>(band + 1)];
        if (x1 <= x0 + 2.0f)
            continue;
        const auto idx = static_cast<size_t>(band);
        const bool isScrollBand = (ratioScrollBand_ == band);
        const float cx = (x0 + x1) * 0.5f;

        // Two small labels: ratioAbove near the top, ratioBelow near the bottom
        // of the neutral zone.
        auto drawRatioLabel = [&](float ratio, bool isAbove, float yRef) {
            const bool isActive = isScrollBand && (ratioScrollAbove_ == isAbove);
            const float alpha = isActive ? 0.95f : 0.40f;
            g.setColour(bandColours2[idx].withAlpha(alpha));
            const juce::String label =
                isActive ? ("R: " + juce::String(ratio, 1)) : juce::String(ratio, 1);
            const int textW = isActive ? 52 : 32;
            const float lx = juce::jlimit(plot.getX() + 2.0f, plot.getRight() - textW - 2.0f,
                                          cx - static_cast<float>(textW) * 0.5f);
            const float ly =
                juce::jlimit(plot.getY() + 2.0f, plot.getBottom() - 14.0f, yRef - 14.0f);
            g.drawText(
                label,
                juce::Rectangle<float>(lx, ly, static_cast<float>(textW), 12.0f).toNearestInt(),
                juce::Justification::centred);
        };
        const float yAbove = dbToY(threshAboveDb_[idx]);
        const float yBelow = dbToY(threshBelowDb_[idx]);
        drawRatioLabel(ratiosAbove_[idx], true, yAbove - 2.0f);
        drawRatioLabel(ratiosBelow_[idx], false, yBelow + 16.0f);
    }

    // Collapse toggle — chevron in the top-right corner.
    collapseButtonArea_ = juce::Rectangle<float>(
        plot.getRight() - kCollapseButtonSize - kCollapseButtonMargin,
        plot.getY() + kCollapseButtonMargin, kCollapseButtonSize, kCollapseButtonSize);

    const bool collapsed = compiledPlugin_ != nullptr && compiledPlugin_->isCurveCollapsed();
    const auto chevronColour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY)
                                   .withAlpha(collapseButtonHovered_ ? 0.95f : 0.55f);
    if (collapseButtonHovered_) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.08f));
        g.fillRoundedRectangle(collapseButtonArea_, 3.0f);
    }
    const auto centre = collapseButtonArea_.getCentre();
    const float armLen = kCollapseButtonSize * 0.28f;
    juce::Path chevron;
    if (collapsed) {
        chevron.startNewSubPath(centre.x - armLen, centre.y + armLen * 0.5f);
        chevron.lineTo(centre.x, centre.y - armLen * 0.5f);
        chevron.lineTo(centre.x + armLen, centre.y + armLen * 0.5f);
    } else {
        chevron.startNewSubPath(centre.x - armLen, centre.y - armLen * 0.5f);
        chevron.lineTo(centre.x, centre.y + armLen * 0.5f);
        chevron.lineTo(centre.x + armLen, centre.y - armLen * 0.5f);
    }
    g.setColour(chevronColour);
    g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

const CompiledPresentationSpec& getMagdaMultibandPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin::xmlTypeName,
        .layoutCellCount = 27,
        .layoutCellsPerRow = 10,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledMultibandCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
        .visualMinFractionNumerator = 1,
        .visualMinFractionDenominator = 2,
    };
    return kSpec;
}

void CompiledMultibandCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaMultibandCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
