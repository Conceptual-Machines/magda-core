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
constexpr float kLimMin = -24.0f;
constexpr float kLimMax = 12.0f;
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
    auto threshExpandBelow = threshExpandBelowDb_;
    auto threshExpandAbove = threshExpandAboveDb_;
    auto ratiosAbove = ratiosAbove_;
    auto ratiosBelow = ratiosBelow_;
    auto expandRatiosBelow = expandRatiosBelow_;
    auto expandRatiosAbove = expandRatiosAbove_;
    auto limitDb = limitDb_;

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
        const int expBelowSlot = (b == 0)   ? Mb::kLowThreshExpandBelowSlot
                                 : (b == 1) ? Mb::kMidThreshExpandBelowSlot
                                            : Mb::kHighThreshExpandBelowSlot;
        const int expAboveSlot = (b == 0)   ? Mb::kLowThreshExpandAboveSlot
                                 : (b == 1) ? Mb::kMidThreshExpandAboveSlot
                                            : Mb::kHighThreshExpandAboveSlot;
        const int raSlot = (b == 0)   ? Mb::kLowRatioAboveSlot
                           : (b == 1) ? Mb::kMidRatioAboveSlot
                                      : Mb::kHighRatioAboveSlot;
        const int rbSlot = (b == 0)   ? Mb::kLowRatioBelowSlot
                           : (b == 1) ? Mb::kMidRatioBelowSlot
                                      : Mb::kHighRatioBelowSlot;
        const int erbSlot = (b == 0)   ? Mb::kLowExpandRatioBelowSlot
                            : (b == 1) ? Mb::kMidExpandRatioBelowSlot
                                       : Mb::kHighExpandRatioBelowSlot;
        const int eraSlot = (b == 0)   ? Mb::kLowExpandRatioAboveSlot
                            : (b == 1) ? Mb::kMidExpandRatioAboveSlot
                                       : Mb::kHighExpandRatioAboveSlot;
        const int limSlot = (b == 0)   ? Mb::kLowLimitSlot
                            : (b == 1) ? Mb::kMidLimitSlot
                                       : Mb::kHighLimitSlot;

        threshAbove[idx] = hasPlugin ? readSlot(aboveSlot, threshAbove[idx])
                                     : readDevice(aboveSlot, threshAbove[idx]);
        threshBelow[idx] = hasPlugin ? readSlot(belowSlot, threshBelow[idx])
                                     : readDevice(belowSlot, threshBelow[idx]);
        threshExpandBelow[idx] = hasPlugin ? readSlot(expBelowSlot, threshExpandBelow[idx])
                                           : readDevice(expBelowSlot, threshExpandBelow[idx]);
        threshExpandAbove[idx] = hasPlugin ? readSlot(expAboveSlot, threshExpandAbove[idx])
                                           : readDevice(expAboveSlot, threshExpandAbove[idx]);
        ratiosAbove[idx] =
            hasPlugin ? readSlot(raSlot, ratiosAbove[idx]) : readDevice(raSlot, ratiosAbove[idx]);
        ratiosBelow[idx] =
            hasPlugin ? readSlot(rbSlot, ratiosBelow[idx]) : readDevice(rbSlot, ratiosBelow[idx]);
        expandRatiosBelow[idx] = hasPlugin ? readSlot(erbSlot, expandRatiosBelow[idx])
                                           : readDevice(erbSlot, expandRatiosBelow[idx]);
        expandRatiosAbove[idx] = hasPlugin ? readSlot(eraSlot, expandRatiosAbove[idx])
                                           : readDevice(eraSlot, expandRatiosAbove[idx]);
        limitDb[idx] =
            hasPlugin ? readSlot(limSlot, limitDb[idx]) : readDevice(limSlot, limitDb[idx]);
    }

    bool changed = std::fabs(low - lowXoHz_) > 0.5f || std::fabs(high - highXoHz_) > 0.5f;
    for (int b = 0; b < 3 && !changed; ++b) {
        const auto idx = static_cast<size_t>(b);
        changed = std::fabs(threshAbove[idx] - threshAboveDb_[idx]) > 0.05f ||
                  std::fabs(threshBelow[idx] - threshBelowDb_[idx]) > 0.05f ||
                  std::fabs(threshExpandBelow[idx] - threshExpandBelowDb_[idx]) > 0.05f ||
                  std::fabs(threshExpandAbove[idx] - threshExpandAboveDb_[idx]) > 0.05f ||
                  std::fabs(ratiosAbove[idx] - ratiosAbove_[idx]) > 0.01f ||
                  std::fabs(ratiosBelow[idx] - ratiosBelow_[idx]) > 0.01f ||
                  std::fabs(expandRatiosBelow[idx] - expandRatiosBelow_[idx]) > 0.01f ||
                  std::fabs(expandRatiosAbove[idx] - expandRatiosAbove_[idx]) > 0.01f ||
                  std::fabs(limitDb[idx] - limitDb_[idx]) > 0.05f;
    }

    if (changed) {
        lowXoHz_ = low;
        highXoHz_ = high;
        threshAboveDb_ = threshAbove;
        threshBelowDb_ = threshBelow;
        threshExpandBelowDb_ = threshExpandBelow;
        threshExpandAboveDb_ = threshExpandAbove;
        ratiosAbove_ = ratiosAbove;
        ratiosBelow_ = ratiosBelow;
        expandRatiosBelow_ = expandRatiosBelow;
        expandRatiosAbove_ = expandRatiosAbove;
        limitDb_ = limitDb;
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
        const int expBelowSlot = (b == 0)   ? Mb::kLowThreshExpandBelowSlot
                                 : (b == 1) ? Mb::kMidThreshExpandBelowSlot
                                            : Mb::kHighThreshExpandBelowSlot;
        const int expAboveSlot = (b == 0)   ? Mb::kLowThreshExpandAboveSlot
                                 : (b == 1) ? Mb::kMidThreshExpandAboveSlot
                                            : Mb::kHighThreshExpandAboveSlot;
        const int raSlot = (b == 0)   ? Mb::kLowRatioAboveSlot
                           : (b == 1) ? Mb::kMidRatioAboveSlot
                                      : Mb::kHighRatioAboveSlot;
        const int rbSlot = (b == 0)   ? Mb::kLowRatioBelowSlot
                           : (b == 1) ? Mb::kMidRatioBelowSlot
                                      : Mb::kHighRatioBelowSlot;
        const int erbSlot = (b == 0)   ? Mb::kLowExpandRatioBelowSlot
                            : (b == 1) ? Mb::kMidExpandRatioBelowSlot
                                       : Mb::kHighExpandRatioBelowSlot;
        const int eraSlot = (b == 0)   ? Mb::kLowExpandRatioAboveSlot
                            : (b == 1) ? Mb::kMidExpandRatioAboveSlot
                                       : Mb::kHighExpandRatioAboveSlot;
        const int limSlot = (b == 0)   ? Mb::kLowLimitSlot
                            : (b == 1) ? Mb::kMidLimitSlot
                                       : Mb::kHighLimitSlot;

        threshAboveDb_[idx] = valueForSlot(deviceSnapshot_, aboveSlot, threshAboveDb_[idx]);
        threshBelowDb_[idx] = valueForSlot(deviceSnapshot_, belowSlot, threshBelowDb_[idx]);
        threshExpandBelowDb_[idx] =
            valueForSlot(deviceSnapshot_, expBelowSlot, threshExpandBelowDb_[idx]);
        threshExpandAboveDb_[idx] =
            valueForSlot(deviceSnapshot_, expAboveSlot, threshExpandAboveDb_[idx]);
        ratiosAbove_[idx] = valueForSlot(deviceSnapshot_, raSlot, ratiosAbove_[idx]);
        ratiosBelow_[idx] = valueForSlot(deviceSnapshot_, rbSlot, ratiosBelow_[idx]);
        expandRatiosBelow_[idx] = valueForSlot(deviceSnapshot_, erbSlot, expandRatiosBelow_[idx]);
        expandRatiosAbove_[idx] = valueForSlot(deviceSnapshot_, eraSlot, expandRatiosAbove_[idx]);
        limitDb_[idx] = valueForSlot(deviceSnapshot_, limSlot, limitDb_[idx]);
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
    switch (h) {
        case Handle::LowThreshExpandBelow:
        case Handle::LowThreshExpandAbove:
        case Handle::MidThreshExpandBelow:
        case Handle::MidThreshExpandAbove:
        case Handle::HighThreshExpandBelow:
        case Handle::HighThreshExpandAbove:
            return true;
        default:
            return false;
    }
}

int CompiledMultibandCurveView::thresholdBandIndex(Handle h) {
    switch (h) {
        case Handle::LowThreshAbove:
        case Handle::LowThreshBelow:
        case Handle::LowThreshExpandBelow:
        case Handle::LowThreshExpandAbove:
        case Handle::LowLimit:
            return 0;
        case Handle::MidThreshAbove:
        case Handle::MidThreshBelow:
        case Handle::MidThreshExpandBelow:
        case Handle::MidThreshExpandAbove:
        case Handle::MidLimit:
            return 1;
        case Handle::HighThreshAbove:
        case Handle::HighThreshBelow:
        case Handle::HighThreshExpandBelow:
        case Handle::HighThreshExpandAbove:
        case Handle::HighLimit:
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
        case Handle::LowThreshExpandBelow:
            return Mb::kLowThreshExpandBelowSlot;
        case Handle::LowThreshExpandAbove:
            return Mb::kLowThreshExpandAboveSlot;
        case Handle::LowLimit:
            return Mb::kLowLimitSlot;
        case Handle::MidThreshAbove:
            return Mb::kMidThreshAboveSlot;
        case Handle::MidThreshBelow:
            return Mb::kMidThreshBelowSlot;
        case Handle::MidThreshExpandBelow:
            return Mb::kMidThreshExpandBelowSlot;
        case Handle::MidThreshExpandAbove:
            return Mb::kMidThreshExpandAboveSlot;
        case Handle::MidLimit:
            return Mb::kMidLimitSlot;
        case Handle::HighThreshAbove:
            return Mb::kHighThreshAboveSlot;
        case Handle::HighThreshBelow:
            return Mb::kHighThreshBelowSlot;
        case Handle::HighThreshExpandBelow:
            return Mb::kHighThreshExpandBelowSlot;
        case Handle::HighThreshExpandAbove:
            return Mb::kHighThreshExpandAboveSlot;
        case Handle::HighLimit:
            return Mb::kHighLimitSlot;
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
    const std::array<Handle, 3> expandBelowHandles{{Handle::LowThreshExpandBelow,
                                                    Handle::MidThreshExpandBelow,
                                                    Handle::HighThreshExpandBelow}};
    const std::array<Handle, 3> expandAboveHandles{{Handle::LowThreshExpandAbove,
                                                    Handle::MidThreshExpandAbove,
                                                    Handle::HighThreshExpandAbove}};
    const std::array<Handle, 3> limitHandles{
        {Handle::LowLimit, Handle::MidLimit, Handle::HighLimit}};

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
        check(dbToY(threshExpandBelowDb_[idx]), expandBelowHandles[idx]);
        check(dbToY(threshExpandAboveDb_[idx]), expandAboveHandles[idx]);
        check(dbToY(limitDb_[idx]), limitHandles[idx]);
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
        const bool isVertical =
            isThresholdHandle(picked) || isExpandHandle(picked) || thresholdBandIndex(picked) >= 0;
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                       : isVertical           ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
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
    const bool isVertical =
        isThresholdHandle(picked) || isExpandHandle(picked) || thresholdBandIndex(picked) >= 0;
    setMouseCursor(isVertical ? juce::MouseCursor::UpDownResizeCursor
                              : juce::MouseCursor::LeftRightResizeCursor);
}

void CompiledMultibandCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

    const int band = thresholdBandIndex(draggedHandle_);
    if (band >= 0) {
        const auto idx = static_cast<size_t>(band);
        const float rawDb = yToDb(static_cast<float>(e.y));
        const int slot = thresholdSlotForHandle(draggedHandle_);

        auto emit = [&](float& stored, float clamped) {
            if (std::fabs(clamped - stored) > 0.05f) {
                stored = clamped;
                if (slot >= 0 && onParameterChanged)
                    onParameterChanged(slot, clamped);
                repaint();
            }
        };

        if (isAboveThresholdHandle(draggedHandle_)) {
            // Must stay above threshBelow.
            const float floor = std::max(kThreshAboveMin, threshBelowDb_[idx] + kMinThresholdGapDb);
            emit(threshAboveDb_[idx], juce::jlimit(floor, kThreshAboveMax, rawDb));
        } else if (draggedHandle_ == Handle::LowThreshExpandBelow ||
                   draggedHandle_ == Handle::MidThreshExpandBelow ||
                   draggedHandle_ == Handle::HighThreshExpandBelow) {
            // Expand-below: must stay below threshBelow.
            const float ceiling =
                std::min(kThreshExpandMax, threshBelowDb_[idx] - kMinThresholdGapDb);
            emit(threshExpandBelowDb_[idx], juce::jlimit(kThreshExpandMin, ceiling, rawDb));
        } else if (draggedHandle_ == Handle::LowThreshExpandAbove ||
                   draggedHandle_ == Handle::MidThreshExpandAbove ||
                   draggedHandle_ == Handle::HighThreshExpandAbove) {
            // Expand-above: must stay below threshAbove.
            const float ceiling =
                std::min(kThreshAboveMax, threshAboveDb_[idx] - kMinThresholdGapDb);
            emit(threshExpandAboveDb_[idx], juce::jlimit(kThreshAboveMin, ceiling, rawDb));
        } else if (draggedHandle_ == Handle::LowLimit || draggedHandle_ == Handle::MidLimit ||
                   draggedHandle_ == Handle::HighLimit) {
            emit(limitDb_[idx], juce::jlimit(kLimMin, kLimMax, rawDb));
        } else {
            // threshBelow: must stay below threshAbove and above threshExpandBelow.
            const float ceiling =
                std::min(kThreshBelowMax, threshAboveDb_[idx] - kMinThresholdGapDb);
            const float floor =
                std::max(kThreshBelowMin, threshExpandBelowDb_[idx] + kMinThresholdGapDb);
            emit(threshBelowDb_[idx], juce::jlimit(floor, ceiling, rawDb));
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
    const bool isVertical = isThresholdHandle(hoveredHandle_) || isExpandHandle(hoveredHandle_) ||
                            thresholdBandIndex(hoveredHandle_) >= 0;
    setMouseCursor(hoveredHandle_ == Handle::None ? juce::MouseCursor::NormalCursor
                   : isVertical                   ? juce::MouseCursor::UpDownResizeCursor
                                                  : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void CompiledMultibandCurveView::mouseWheelMove(const juce::MouseEvent& e,
                                                const juce::MouseWheelDetails& wheel) {
    const int band = bandAtX(static_cast<float>(e.x));
    if (band < 0)
        return;

    const auto idx = static_cast<size_t>(band);
    const float mouseY = static_cast<float>(e.y);
    const float yExpandAbove = dbToY(threshExpandAboveDb_[idx]);
    const float yAbove = dbToY(threshAboveDb_[idx]);
    const float yBelow = dbToY(threshBelowDb_[idx]);
    const float yExpandBelow = dbToY(threshExpandBelowDb_[idx]);

    // Zones from top to bottom: expandAbove → above → below → expandBelow
    const bool inExpandAboveZone = mouseY < yExpandAbove;
    const bool inAboveZone = mouseY >= yExpandAbove && mouseY < yAbove;
    const bool inBelowZone = mouseY >= yBelow && mouseY < yExpandBelow;
    const bool inExpandBelowZone = mouseY >= yExpandBelow;
    if (!inExpandAboveZone && !inAboveZone && !inBelowZone && !inExpandBelowZone)
        return;

    constexpr float kRatioStep = 0.5f;
    constexpr float kRatioMin = 1.0f;
    constexpr float kRatioMax = 50.0f;
    const float delta = wheel.deltaY > 0.0f ? kRatioStep : -kRatioStep;

    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (inAboveZone) {
        const float newRatio = juce::jlimit(kRatioMin, kRatioMax, ratiosAbove_[idx] + delta);
        if (std::fabs(newRatio - ratiosAbove_[idx]) > 0.01f) {
            ratiosAbove_[idx] = newRatio;
            ratioScrollBand_ = band;
            ratioScrollZone_ = 0;
            if (onParameterChanged)
                onParameterChanged(ratioSlotForBand(band, true), newRatio);
            repaint();
        }
    } else if (inBelowZone) {
        const float newRatio = juce::jlimit(kRatioMin, kRatioMax, ratiosBelow_[idx] + delta);
        if (std::fabs(newRatio - ratiosBelow_[idx]) > 0.01f) {
            ratiosBelow_[idx] = newRatio;
            ratioScrollBand_ = band;
            ratioScrollZone_ = 1;
            if (onParameterChanged)
                onParameterChanged(ratioSlotForBand(band, false), newRatio);
            repaint();
        }
    } else if (inExpandBelowZone) {
        const int erbSlot = (band == 0)   ? Mb::kLowExpandRatioBelowSlot
                            : (band == 1) ? Mb::kMidExpandRatioBelowSlot
                                          : Mb::kHighExpandRatioBelowSlot;
        const float newRatio = juce::jlimit(kRatioMin, kRatioMax, expandRatiosBelow_[idx] + delta);
        if (std::fabs(newRatio - expandRatiosBelow_[idx]) > 0.01f) {
            expandRatiosBelow_[idx] = newRatio;
            ratioScrollBand_ = band;
            ratioScrollZone_ = 2;
            if (onParameterChanged)
                onParameterChanged(erbSlot, newRatio);
            repaint();
        }
    } else {
        const int eraSlot = (band == 0)   ? Mb::kLowExpandRatioAboveSlot
                            : (band == 1) ? Mb::kMidExpandRatioAboveSlot
                                          : Mb::kHighExpandRatioAboveSlot;
        const float newRatio = juce::jlimit(kRatioMin, kRatioMax, expandRatiosAbove_[idx] + delta);
        if (std::fabs(newRatio - expandRatiosAbove_[idx]) > 0.01f) {
            expandRatiosAbove_[idx] = newRatio;
            ratioScrollBand_ = band;
            ratioScrollZone_ = 3;
            if (onParameterChanged)
                onParameterChanged(eraSlot, newRatio);
            repaint();
        }
    }
}

void CompiledMultibandCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colours::black);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    plotArea_ = plot;
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.45f));
    g.drawRect(plot, 1.0f);

    juce::Graphics::ScopedSaveState clipGuard(g);
    g.reduceClipRegion(plot.toNearestInt());

    // Decade grid.
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (float decade : {100.0f, 1000.0f, 10000.0f}) {
        const float x = freqToX(decade);
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
    }

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);
    const std::array<float, 4> bandEdges{{plot.getX(), lowX, highX, plot.getRight()}};

    // Line colours: orange=compAbove, cyan=compBelow, purple=expand thresholds, red=limiter.
    const auto aboveColour = juce::Colour(0xFFFF8C00);   // orange
    const auto belowColour = juce::Colour(0xFF00D4FF);   // cyan
    const auto expandColour = juce::Colour(0xFFAA55FF);  // purple
    const auto limitColour = juce::Colour(0xFFFF3333);   // red
    const auto xoColour = juce::Colours::white;

    // Band border tints on the crossover lines.
    const std::array<Handle, 3> aboveHandles{
        {Handle::LowThreshAbove, Handle::MidThreshAbove, Handle::HighThreshAbove}};
    const std::array<Handle, 3> belowHandles{
        {Handle::LowThreshBelow, Handle::MidThreshBelow, Handle::HighThreshBelow}};
    const std::array<Handle, 3> expBelowHandles{{Handle::LowThreshExpandBelow,
                                                 Handle::MidThreshExpandBelow,
                                                 Handle::HighThreshExpandBelow}};
    const std::array<Handle, 3> expAboveHandles{{Handle::LowThreshExpandAbove,
                                                 Handle::MidThreshExpandAbove,
                                                 Handle::HighThreshExpandAbove}};
    const std::array<Handle, 3> limitHandles{
        {Handle::LowLimit, Handle::MidLimit, Handle::HighLimit}};

    auto drawThreshLine = [&](float x0, float x1, float y, Handle h, juce::Colour colour,
                              float baseAlpha, float baseThickness) {
        const bool active = h == hoveredHandle_ || h == draggedHandle_;
        g.setColour(colour.withAlpha(active ? 1.0f : baseAlpha));
        g.drawLine(x0 + 2.0f, y, x1 - 2.0f, y, active ? baseThickness + 0.8f : baseThickness);
    };

    auto drawThreshLabel = [&](float x0, float x1, float y, float db, Handle h,
                               juce::Colour colour) {
        if (h != hoveredHandle_ && h != draggedHandle_)
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
        const float yExpBelow = dbToY(threshExpandBelowDb_[idx]);
        const float yExpAbove = dbToY(threshExpandAboveDb_[idx]);
        const float yLimit = dbToY(limitDb_[idx]);

        // Compressor zone border — faint dashed-effect via thin rect outline.
        g.setColour(aboveColour.withAlpha(0.12f));
        g.drawRect(juce::Rectangle<float>(x0 + 1.0f, plot.getY(), x1 - x0 - 2.0f,
                                          juce::jmax(0.0f, yAbove - plot.getY())),
                   0.5f);
        g.setColour(belowColour.withAlpha(0.12f));
        g.drawRect(juce::Rectangle<float>(x0 + 1.0f, yBelow, x1 - x0 - 2.0f,
                                          juce::jmax(0.0f, yExpBelow - yBelow)),
                   0.5f);

        // Threshold lines.
        drawThreshLine(x0, x1, yAbove, aboveHandles[idx], aboveColour, 0.80f, 1.5f);
        drawThreshLine(x0, x1, yBelow, belowHandles[idx], belowColour, 0.80f, 1.5f);
        drawThreshLine(x0, x1, yExpBelow, expBelowHandles[idx], expandColour, 0.55f, 1.0f);
        drawThreshLine(x0, x1, yExpAbove, expAboveHandles[idx], expandColour, 0.55f, 1.0f);
        drawThreshLine(x0, x1, yLimit, limitHandles[idx], limitColour, 0.65f, 1.2f);

        drawThreshLabel(x0, x1, yAbove, threshAboveDb_[idx], aboveHandles[idx], aboveColour);
        drawThreshLabel(x0, x1, yBelow, threshBelowDb_[idx], belowHandles[idx], belowColour);
        drawThreshLabel(x0, x1, yExpBelow, threshExpandBelowDb_[idx], expBelowHandles[idx],
                        expandColour);
        drawThreshLabel(x0, x1, yExpAbove, threshExpandAboveDb_[idx], expAboveHandles[idx],
                        expandColour);
        drawThreshLabel(x0, x1, yLimit, limitDb_[idx], limitHandles[idx], limitColour);
    }

    // Crossover lines.
    auto drawXoLine = [&](float x, Handle which) {
        const bool active = (which == hoveredHandle_) || (which == draggedHandle_);
        g.setColour(xoColour.withAlpha(active ? 0.90f : 0.50f));
        const float thickness = active ? 2.0f : 1.0f;
        g.fillRect(
            juce::Rectangle<float>(x - thickness * 0.5f, plot.getY(), thickness, plot.getHeight()));
    };
    drawXoLine(lowX, Handle::LowXo);
    drawXoLine(highX, Handle::HighXo);

    // Frequency labels on active crossover handles.
    auto drawXoLabel = [&](float x, float hz, Handle which) {
        if (which != hoveredHandle_ && which != draggedHandle_)
            return;
        const auto text = (hz >= 1000.0f) ? juce::String(hz / 1000.0f, 2) + " kHz"
                                          : juce::String(static_cast<int>(std::round(hz))) + " Hz";
        g.setColour(xoColour);
        g.setFont(11.0f);
        constexpr int textW = 64, textH = 14;
        const float lx =
            juce::jlimit(plot.getX() + 2.0f, plot.getRight() - textW - 2.0f, x - textW * 0.5f);
        g.drawText(text,
                   juce::Rectangle<float>(lx, plot.getY() + 2.0f, textW, textH).toNearestInt(),
                   juce::Justification::centred);
    };
    drawXoLabel(lowX, lowXoHz_, Handle::LowXo);
    drawXoLabel(highX, highXoHz_, Handle::HighXo);

    // Ratio labels — amplified during scroll.
    g.setFont(10.0f);
    for (int band = 0; band < 3; ++band) {
        const float x0 = bandEdges[static_cast<size_t>(band)];
        const float x1 = bandEdges[static_cast<size_t>(band + 1)];
        if (x1 <= x0 + 2.0f)
            continue;
        const auto idx = static_cast<size_t>(band);
        const bool isScrollBand = (ratioScrollBand_ == band);
        const float cx = (x0 + x1) * 0.5f;

        auto drawRatioLabel = [&](float ratio, int zone, float yRef, juce::Colour col) {
            const bool isActive = isScrollBand && (ratioScrollZone_ == zone);
            const float alpha = isActive ? 0.95f : 0.38f;
            g.setColour(col.withAlpha(alpha));
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
        const float yExpAbove = dbToY(threshExpandAboveDb_[idx]);
        const float yAbove = dbToY(threshAboveDb_[idx]);
        const float yBelow = dbToY(threshBelowDb_[idx]);
        const float yExpBelow = dbToY(threshExpandBelowDb_[idx]);
        drawRatioLabel(expandRatiosAbove_[idx], 3, yExpAbove - 2.0f, expandColour);
        drawRatioLabel(ratiosAbove_[idx], 0, yAbove - 2.0f, aboveColour);
        drawRatioLabel(ratiosBelow_[idx], 1, yBelow + 16.0f, belowColour);
        drawRatioLabel(expandRatiosBelow_[idx], 2, yExpBelow + 16.0f, expandColour);
    }

    // Collapse toggle.
    collapseButtonArea_ = juce::Rectangle<float>(
        plot.getRight() - kCollapseButtonSize - kCollapseButtonMargin,
        plot.getY() + kCollapseButtonMargin, kCollapseButtonSize, kCollapseButtonSize);

    const bool collapsed = compiledPlugin_ != nullptr && compiledPlugin_->isCurveCollapsed();
    const auto chevronColour =
        juce::Colours::white.withAlpha(collapseButtonHovered_ ? 0.95f : 0.50f);
    if (collapseButtonHovered_) {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
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
        .layoutCellCount = 9,
        .layoutCellsPerRow = 3,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledMultibandCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
        .visualMinFractionNumerator = 3,
        .visualMinFractionDenominator = 4,
    };
    return kSpec;
}

void CompiledMultibandCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaMultibandCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
