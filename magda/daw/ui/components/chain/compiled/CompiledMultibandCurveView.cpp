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
constexpr float kMinXoGapHz = 10.0f;
constexpr float kDbMin = -80.0f;
constexpr float kDbMax = 12.0f;
constexpr float kAttackMinMs = 0.1f;
constexpr float kAttackMaxMs = 100.0f;
constexpr float kReleaseMinMs = 5.0f;
constexpr float kReleaseMaxMs = 1000.0f;
constexpr float kRatioMin = 0.05f;
constexpr float kRatioMax = 100.0f;
constexpr float kRangeMin = 0.0f;
constexpr float kRangeMax = 48.0f;

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

juce::String ratioLabel(float ratio) {
    ratio = juce::jlimit(kRatioMin, kRatioMax, ratio);
    if (std::abs(ratio - 1.0f) < 0.02f)
        return "1:1";
    if (ratio >= 1.0f)
        return juce::String(ratio, ratio < 10.0f ? 1 : 0) + ":1";
    const float inverse = 1.0f / ratio;
    return "1:" + juce::String(inverse, inverse < 10.0f ? 1 : 0);
}

float adjustRatio(float ratio, float octaves) {
    return juce::jlimit(kRatioMin, kRatioMax, ratio * std::pow(2.0f, octaves));
}

float adjustTimeMs(float value, float octaves, float minValue, float maxValue) {
    return juce::jlimit(minValue, maxValue, value * std::pow(2.0f, octaves));
}

juce::String timeLabel(float ms) {
    if (ms < 10.0f)
        return juce::String(ms, 1) + " ms";
    if (ms < 100.0f)
        return juce::String(ms, 0) + " ms";
    return juce::String(static_cast<int>(std::round(ms))) + " ms";
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

    auto readSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return valueForSlot(deviceSnapshot_, slot, fallback);
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    const float low = readSlot(Mb::kLowXoSlot, lowXoHz_);
    const float high = readSlot(Mb::kHighXoSlot, highXoHz_);
    std::array<float, 3> attack{
        readSlot(Mb::kLowAttackSlot, attackMs_[0]),
        readSlot(Mb::kMidAttackSlot, attackMs_[1]),
        readSlot(Mb::kHighAttackSlot, attackMs_[2]),
    };
    std::array<float, 3> release{
        readSlot(Mb::kLowReleaseSlot, releaseMs_[0]),
        readSlot(Mb::kMidReleaseSlot, releaseMs_[1]),
        readSlot(Mb::kHighReleaseSlot, releaseMs_[2]),
    };
    std::array<float, 3> lowerThreshold{
        readSlot(Mb::kLowLowerThresholdSlot, lowerThresholdDb_[0]),
        readSlot(Mb::kMidLowerThresholdSlot, lowerThresholdDb_[1]),
        readSlot(Mb::kHighLowerThresholdSlot, lowerThresholdDb_[2]),
    };
    std::array<float, 3> upperThreshold{
        readSlot(Mb::kLowUpperThresholdSlot, upperThresholdDb_[0]),
        readSlot(Mb::kMidUpperThresholdSlot, upperThresholdDb_[1]),
        readSlot(Mb::kHighUpperThresholdSlot, upperThresholdDb_[2]),
    };
    std::array<float, 3> belowRatio{
        readSlot(Mb::kLowBelowRatioSlot, belowRatio_[0]),
        readSlot(Mb::kMidBelowRatioSlot, belowRatio_[1]),
        readSlot(Mb::kHighBelowRatioSlot, belowRatio_[2]),
    };
    std::array<float, 3> aboveRatio{
        readSlot(Mb::kLowAboveRatioSlot, aboveRatio_[0]),
        readSlot(Mb::kMidAboveRatioSlot, aboveRatio_[1]),
        readSlot(Mb::kHighAboveRatioSlot, aboveRatio_[2]),
    };
    std::array<float, 3> range{
        readSlot(Mb::kLowRangeSlot, rangeDb_[0]),
        readSlot(Mb::kMidRangeSlot, rangeDb_[1]),
        readSlot(Mb::kHighRangeSlot, rangeDb_[2]),
    };
    std::array<float, 3> limit{
        readSlot(Mb::kLowLimitSlot, limitDb_[0]),
        readSlot(Mb::kMidLimitSlot, limitDb_[1]),
        readSlot(Mb::kHighLimitSlot, limitDb_[2]),
    };

    bool changed = std::fabs(low - lowXoHz_) > 0.5f || std::fabs(high - highXoHz_) > 0.5f;
    for (int b = 0; b < 3 && !changed; ++b) {
        changed =
            std::fabs(lowerThreshold[static_cast<size_t>(b)] -
                      lowerThresholdDb_[static_cast<size_t>(b)]) > 0.05f ||
            std::fabs(upperThreshold[static_cast<size_t>(b)] -
                      upperThresholdDb_[static_cast<size_t>(b)]) > 0.05f ||
            std::fabs(belowRatio[static_cast<size_t>(b)] - belowRatio_[static_cast<size_t>(b)]) >
                0.01f ||
            std::fabs(aboveRatio[static_cast<size_t>(b)] - aboveRatio_[static_cast<size_t>(b)]) >
                0.01f ||
            std::fabs(range[static_cast<size_t>(b)] - rangeDb_[static_cast<size_t>(b)]) > 0.05f ||
            std::fabs(limit[static_cast<size_t>(b)] - limitDb_[static_cast<size_t>(b)]) > 0.05f ||
            std::fabs(attack[static_cast<size_t>(b)] - attackMs_[static_cast<size_t>(b)]) > 0.05f ||
            std::fabs(release[static_cast<size_t>(b)] - releaseMs_[static_cast<size_t>(b)]) > 0.5f;
    }

    if (changed) {
        lowXoHz_ = low;
        highXoHz_ = high;
        attackMs_ = attack;
        releaseMs_ = release;
        lowerThresholdDb_ = lowerThreshold;
        upperThresholdDb_ = upperThreshold;
        belowRatio_ = belowRatio;
        aboveRatio_ = aboveRatio;
        rangeDb_ = range;
        limitDb_ = limit;
        repaint();
    }
}

void CompiledMultibandCurveView::resampleFromPlugin() {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    lowXoHz_ = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, lowXoHz_);
    highXoHz_ = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, highXoHz_);
    attackMs_[0] = valueForSlot(deviceSnapshot_, Mb::kLowAttackSlot, attackMs_[0]);
    attackMs_[1] = valueForSlot(deviceSnapshot_, Mb::kMidAttackSlot, attackMs_[1]);
    attackMs_[2] = valueForSlot(deviceSnapshot_, Mb::kHighAttackSlot, attackMs_[2]);
    releaseMs_[0] = valueForSlot(deviceSnapshot_, Mb::kLowReleaseSlot, releaseMs_[0]);
    releaseMs_[1] = valueForSlot(deviceSnapshot_, Mb::kMidReleaseSlot, releaseMs_[1]);
    releaseMs_[2] = valueForSlot(deviceSnapshot_, Mb::kHighReleaseSlot, releaseMs_[2]);
    lowerThresholdDb_[0] =
        valueForSlot(deviceSnapshot_, Mb::kLowLowerThresholdSlot, lowerThresholdDb_[0]);
    lowerThresholdDb_[1] =
        valueForSlot(deviceSnapshot_, Mb::kMidLowerThresholdSlot, lowerThresholdDb_[1]);
    lowerThresholdDb_[2] =
        valueForSlot(deviceSnapshot_, Mb::kHighLowerThresholdSlot, lowerThresholdDb_[2]);
    upperThresholdDb_[0] =
        valueForSlot(deviceSnapshot_, Mb::kLowUpperThresholdSlot, upperThresholdDb_[0]);
    upperThresholdDb_[1] =
        valueForSlot(deviceSnapshot_, Mb::kMidUpperThresholdSlot, upperThresholdDb_[1]);
    upperThresholdDb_[2] =
        valueForSlot(deviceSnapshot_, Mb::kHighUpperThresholdSlot, upperThresholdDb_[2]);
    belowRatio_[0] = valueForSlot(deviceSnapshot_, Mb::kLowBelowRatioSlot, belowRatio_[0]);
    belowRatio_[1] = valueForSlot(deviceSnapshot_, Mb::kMidBelowRatioSlot, belowRatio_[1]);
    belowRatio_[2] = valueForSlot(deviceSnapshot_, Mb::kHighBelowRatioSlot, belowRatio_[2]);
    aboveRatio_[0] = valueForSlot(deviceSnapshot_, Mb::kLowAboveRatioSlot, aboveRatio_[0]);
    aboveRatio_[1] = valueForSlot(deviceSnapshot_, Mb::kMidAboveRatioSlot, aboveRatio_[1]);
    aboveRatio_[2] = valueForSlot(deviceSnapshot_, Mb::kHighAboveRatioSlot, aboveRatio_[2]);
    rangeDb_[0] = valueForSlot(deviceSnapshot_, Mb::kLowRangeSlot, rangeDb_[0]);
    rangeDb_[1] = valueForSlot(deviceSnapshot_, Mb::kMidRangeSlot, rangeDb_[1]);
    rangeDb_[2] = valueForSlot(deviceSnapshot_, Mb::kHighRangeSlot, rangeDb_[2]);
    limitDb_[0] = valueForSlot(deviceSnapshot_, Mb::kLowLimitSlot, limitDb_[0]);
    limitDb_[1] = valueForSlot(deviceSnapshot_, Mb::kMidLimitSlot, limitDb_[1]);
    limitDb_[2] = valueForSlot(deviceSnapshot_, Mb::kHighLimitSlot, limitDb_[2]);
}

bool CompiledMultibandCurveView::wantsFullBody() const {
    return compiledPlugin_ != nullptr && compiledPlugin_->isCurveCollapsed();
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
    const float t = 1.0f - (juce::jlimit(kDbMin, kDbMax, db) - kDbMin) / (kDbMax - kDbMin);
    return plotArea_.getY() + t * plotArea_.getHeight();
}

float CompiledMultibandCurveView::yToDb(float y) const {
    if (plotArea_.getHeight() <= 0.0f)
        return 0.0f;
    const float t = juce::jlimit(0.0f, 1.0f, (y - plotArea_.getY()) / plotArea_.getHeight());
    return kDbMax - t * (kDbMax - kDbMin);
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

int CompiledMultibandCurveView::lowerThresholdSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0   ? Mb::kLowLowerThresholdSlot
           : band == 1 ? Mb::kMidLowerThresholdSlot
                       : Mb::kHighLowerThresholdSlot;
}

int CompiledMultibandCurveView::upperThresholdSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0   ? Mb::kLowUpperThresholdSlot
           : band == 1 ? Mb::kMidUpperThresholdSlot
                       : Mb::kHighUpperThresholdSlot;
}

int CompiledMultibandCurveView::belowRatioSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0   ? Mb::kLowBelowRatioSlot
           : band == 1 ? Mb::kMidBelowRatioSlot
                       : Mb::kHighBelowRatioSlot;
}

int CompiledMultibandCurveView::aboveRatioSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0   ? Mb::kLowAboveRatioSlot
           : band == 1 ? Mb::kMidAboveRatioSlot
                       : Mb::kHighAboveRatioSlot;
}

int CompiledMultibandCurveView::rangeSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowRangeSlot : band == 1 ? Mb::kMidRangeSlot : Mb::kHighRangeSlot;
}

int CompiledMultibandCurveView::limitSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowLimitSlot : band == 1 ? Mb::kMidLimitSlot : Mb::kHighLimitSlot;
}

int CompiledMultibandCurveView::attackSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowAttackSlot : band == 1 ? Mb::kMidAttackSlot : Mb::kHighAttackSlot;
}

int CompiledMultibandCurveView::releaseSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowReleaseSlot : band == 1 ? Mb::kMidReleaseSlot : Mb::kHighReleaseSlot;
}

int CompiledMultibandCurveView::bandForHandle(Handle h) {
    switch (h) {
        case Handle::LowLowerThreshold:
        case Handle::LowUpperThreshold:
        case Handle::LowBelowRatio:
        case Handle::LowAboveRatio:
        case Handle::LowAttack:
        case Handle::LowRelease:
        case Handle::LowLimit:
            return 0;
        case Handle::MidLowerThreshold:
        case Handle::MidUpperThreshold:
        case Handle::MidBelowRatio:
        case Handle::MidAboveRatio:
        case Handle::MidAttack:
        case Handle::MidRelease:
        case Handle::MidLimit:
            return 1;
        case Handle::HighLowerThreshold:
        case Handle::HighUpperThreshold:
        case Handle::HighBelowRatio:
        case Handle::HighAboveRatio:
        case Handle::HighAttack:
        case Handle::HighRelease:
        case Handle::HighLimit:
            return 2;
        default:
            return -1;
    }
}

bool CompiledMultibandCurveView::isLimitHandle(Handle h) {
    return h == Handle::LowLimit || h == Handle::MidLimit || h == Handle::HighLimit;
}

bool CompiledMultibandCurveView::isUpperThresholdHandle(Handle h) {
    return h == Handle::LowUpperThreshold || h == Handle::MidUpperThreshold ||
           h == Handle::HighUpperThreshold;
}

bool CompiledMultibandCurveView::isRatioHandle(Handle h) {
    return h == Handle::LowBelowRatio || h == Handle::LowAboveRatio || h == Handle::MidBelowRatio ||
           h == Handle::MidAboveRatio || h == Handle::HighBelowRatio || h == Handle::HighAboveRatio;
}

bool CompiledMultibandCurveView::isAboveRatioHandle(Handle h) {
    return h == Handle::LowAboveRatio || h == Handle::MidAboveRatio || h == Handle::HighAboveRatio;
}

bool CompiledMultibandCurveView::isTimingHandle(Handle h) {
    return h == Handle::LowAttack || h == Handle::LowRelease || h == Handle::MidAttack ||
           h == Handle::MidRelease || h == Handle::HighAttack || h == Handle::HighRelease;
}

bool CompiledMultibandCurveView::isReleaseTimingHandle(Handle h) {
    return h == Handle::LowRelease || h == Handle::MidRelease || h == Handle::HighRelease;
}

int CompiledMultibandCurveView::slotForHandle(Handle h) const {
    const int band = bandForHandle(h);
    if (band < 0)
        return -1;
    if (isLimitHandle(h))
        return limitSlotForBand(band);
    if (isRatioHandle(h))
        return isAboveRatioHandle(h) ? aboveRatioSlotForBand(band) : belowRatioSlotForBand(band);
    if (isTimingHandle(h))
        return isReleaseTimingHandle(h) ? releaseSlotForBand(band) : attackSlotForBand(band);
    return isUpperThresholdHandle(h) ? upperThresholdSlotForBand(band)
                                     : lowerThresholdSlotForBand(band);
}

CompiledMultibandCurveView::Handle CompiledMultibandCurveView::pickHandle(float x, float y) const {
    if (plotArea_.getWidth() <= 0.0f || plotArea_.getHeight() <= 0.0f)
        return Handle::None;

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);
    const std::array<float, 4> bandEdges{{plotArea_.getX(), lowX, highX, plotArea_.getRight()}};
    const std::array<Handle, 3> lowerThresholdHandles{
        {Handle::LowLowerThreshold, Handle::MidLowerThreshold, Handle::HighLowerThreshold}};
    const std::array<Handle, 3> upperThresholdHandles{
        {Handle::LowUpperThreshold, Handle::MidUpperThreshold, Handle::HighUpperThreshold}};
    const std::array<Handle, 3> limitHandles{
        {Handle::LowLimit, Handle::MidLimit, Handle::HighLimit}};
    const std::array<Handle, 3> belowRatioHandles{
        {Handle::LowBelowRatio, Handle::MidBelowRatio, Handle::HighBelowRatio}};
    const std::array<Handle, 3> aboveRatioHandles{
        {Handle::LowAboveRatio, Handle::MidAboveRatio, Handle::HighAboveRatio}};
    const std::array<Handle, 3> attackHandles{
        {Handle::LowAttack, Handle::MidAttack, Handle::HighAttack}};
    const std::array<Handle, 3> releaseHandles{
        {Handle::LowRelease, Handle::MidRelease, Handle::HighRelease}};

    Handle nearest = Handle::None;
    float nearestDist = kThresholdPickPx + 1.0f;
    for (int band = 0; band < 3; ++band) {
        const float x0 = bandEdges[static_cast<size_t>(band)] - 2.0f;
        const float x1 = bandEdges[static_cast<size_t>(band + 1)] + 2.0f;
        if (x < x0 || x > x1)
            continue;
        auto check = [&](float lineY, Handle h) {
            const float d = std::fabs(y - lineY);
            if (d <= kThresholdPickPx && d < nearestDist) {
                nearest = h;
                nearestDist = d;
            }
        };
        check(dbToY(lowerThresholdDb_[static_cast<size_t>(band)]), lowerThresholdHandles[band]);
        check(dbToY(upperThresholdDb_[static_cast<size_t>(band)]), upperThresholdHandles[band]);
        check(dbToY(limitDb_[static_cast<size_t>(band)]), limitHandles[band]);
    }
    if (nearest != Handle::None)
        return nearest;

    for (int band = 0; band < 3; ++band) {
        const auto idx = static_cast<size_t>(band);
        if (x < bandEdges[idx] || x > bandEdges[idx + 1])
            continue;

        if (attackAreas_[idx].contains(x, y))
            return attackHandles[idx];
        if (releaseAreas_[idx].contains(x, y))
            return releaseHandles[idx];
        if (aboveRatioAreas_[idx].contains(x, y))
            return aboveRatioHandles[idx];
        if (belowRatioAreas_[idx].contains(x, y))
            return belowRatioHandles[idx];
    }

    const float dLow = std::fabs(x - lowX);
    const float dHigh = std::fabs(x - highX);
    if (dLow > kHandlePickPx && dHigh > kHandlePickPx)
        return Handle::None;
    return dLow <= dHigh ? Handle::LowXo : Handle::HighXo;
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
        hoveredHandle_ = Handle::None;
        return;
    }

    const auto picked = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    if (picked != hoveredHandle_) {
        hoveredHandle_ = picked;
        const bool vertical = bandForHandle(picked) >= 0 || isTimingHandle(picked);
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                       : vertical             ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void CompiledMultibandCurveView::mouseExit(const juce::MouseEvent&) {
    hoveredHandle_ = Handle::None;
    collapseButtonHovered_ = false;
    ratioScrollBand_ = -1;
    rangeScrollActive_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
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

    draggedHandle_ = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    hoveredHandle_ = draggedHandle_;
    if (isTimingHandle(draggedHandle_)) {
        const int timingBand = bandForHandle(draggedHandle_);
        if (timingBand >= 0) {
            const auto idx = static_cast<size_t>(timingBand);
            dragStartValue_ =
                isReleaseTimingHandle(draggedHandle_) ? releaseMs_[idx] : attackMs_[idx];
        }
        return;
    }
    const int band = bandForHandle(draggedHandle_);
    if (band >= 0 && isRatioHandle(draggedHandle_)) {
        dragStartValue_ = isAboveRatioHandle(draggedHandle_)
                              ? aboveRatio_[static_cast<size_t>(band)]
                              : belowRatio_[static_cast<size_t>(band)];
    }
}

void CompiledMultibandCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

    if (isTimingHandle(draggedHandle_)) {
        const int timingBand = bandForHandle(draggedHandle_);
        if (timingBand < 0)
            return;
        const auto idx = static_cast<size_t>(timingBand);
        const bool release = isReleaseTimingHandle(draggedHandle_);
        const float dragOctaves = -static_cast<float>(e.getDistanceFromDragStartY()) / 90.0f;
        const float next =
            adjustTimeMs(dragStartValue_, dragOctaves, release ? kReleaseMinMs : kAttackMinMs,
                         release ? kReleaseMaxMs : kAttackMaxMs);
        auto& target = release ? releaseMs_[idx] : attackMs_[idx];
        const float epsilon = release ? 0.5f : 0.05f;
        if (std::fabs(next - target) > epsilon) {
            target = next;
            if (onParameterChanged)
                onParameterChanged(
                    release ? releaseSlotForBand(timingBand) : attackSlotForBand(timingBand), next);
            repaint();
        }
        return;
    }

    if (draggedHandle_ == Handle::LowXo) {
        const float ceiling = std::min(kLowXoMax, highXoHz_ - kMinXoGapHz);
        const float hz = juce::jlimit(kLowXoMin, ceiling, xToFreq(static_cast<float>(e.x)));
        if (std::fabs(hz - lowXoHz_) > 0.5f) {
            lowXoHz_ = hz;
            if (onParameterChanged)
                onParameterChanged(Mb::kLowXoSlot, hz);
            repaint();
        }
        return;
    }
    if (draggedHandle_ == Handle::HighXo) {
        const float floor = std::max(kHighXoMin, lowXoHz_ + kMinXoGapHz);
        const float hz = juce::jlimit(floor, kHighXoMax, xToFreq(static_cast<float>(e.x)));
        if (std::fabs(hz - highXoHz_) > 0.5f) {
            highXoHz_ = hz;
            if (onParameterChanged)
                onParameterChanged(Mb::kHighXoSlot, hz);
            repaint();
        }
        return;
    }

    const int band = bandForHandle(draggedHandle_);
    const int slot = slotForHandle(draggedHandle_);
    if (band < 0 || slot < 0)
        return;
    const auto idx = static_cast<size_t>(band);
    if (isRatioHandle(draggedHandle_)) {
        auto& ratio = isAboveRatioHandle(draggedHandle_) ? aboveRatio_[idx] : belowRatio_[idx];
        const float dragOctaves = -static_cast<float>(e.getDistanceFromDragStartY()) / 80.0f;
        const float next = adjustRatio(dragStartValue_, dragOctaves);
        if (std::fabs(next - ratio) > 0.01f) {
            ratio = next;
            ratioScrollBand_ = band;
            ratioScrollAbove_ = isAboveRatioHandle(draggedHandle_);
            rangeScrollActive_ = false;
            if (onParameterChanged)
                onParameterChanged(slot, next);
            repaint();
        }
        return;
    }

    float db = juce::jlimit(kDbMin, kDbMax, yToDb(static_cast<float>(e.y)));
    auto& target = isLimitHandle(draggedHandle_)            ? limitDb_[idx]
                   : isUpperThresholdHandle(draggedHandle_) ? upperThresholdDb_[idx]
                                                            : lowerThresholdDb_[idx];
    if (!isLimitHandle(draggedHandle_)) {
        constexpr float kMinThresholdGapDb = 1.0f;
        if (isUpperThresholdHandle(draggedHandle_))
            db = std::max(db, lowerThresholdDb_[idx] + kMinThresholdGapDb);
        else
            db = std::min(db, upperThresholdDb_[idx] - kMinThresholdGapDb);
    }
    if (std::fabs(db - target) > 0.05f) {
        target = db;
        if (onParameterChanged)
            onParameterChanged(slot, db);
        repaint();
    }
}

void CompiledMultibandCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedHandle_ = Handle::None;
    hoveredHandle_ = pickHandle(static_cast<float>(e.x), static_cast<float>(e.y));
    repaint();
}

void CompiledMultibandCurveView::mouseWheelMove(const juce::MouseEvent& e,
                                                const juce::MouseWheelDetails& wheel) {
    const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;
    auto adjustTiming = [&](int band, bool release) {
        const auto idx = static_cast<size_t>(band);
        auto& target = release ? releaseMs_[idx] : attackMs_[idx];
        const float step = e.mods.isAltDown() ? 0.125f : 0.25f;
        const float next =
            adjustTimeMs(target, direction * step, release ? kReleaseMinMs : kAttackMinMs,
                         release ? kReleaseMaxMs : kAttackMaxMs);
        const float epsilon = release ? 0.5f : 0.05f;
        if (std::fabs(next - target) > epsilon) {
            target = next;
            if (onParameterChanged)
                onParameterChanged(release ? releaseSlotForBand(band) : attackSlotForBand(band),
                                   next);
            repaint();
        }
    };
    for (int band = 0; band < 3; ++band) {
        const auto idx = static_cast<size_t>(band);
        if (attackAreas_[idx].contains(e.position)) {
            adjustTiming(band, false);
            return;
        }
        if (releaseAreas_[idx].contains(e.position)) {
            adjustTiming(band, true);
            return;
        }
    }

    const int band = bandAtX(static_cast<float>(e.x));
    if (band < 0)
        return;

    const bool adjustRange = e.mods.isShiftDown();
    const auto idx = static_cast<size_t>(band);

    if (adjustRange) {
        const float next = juce::jlimit(kRangeMin, kRangeMax, rangeDb_[idx] + direction);
        if (std::fabs(next - rangeDb_[idx]) > 0.01f) {
            rangeDb_[idx] = next;
            ratioScrollBand_ = band;
            rangeScrollActive_ = true;
            if (onParameterChanged)
                onParameterChanged(rangeSlotForBand(band), next);
            repaint();
        }
        return;
    }

    const float step = e.mods.isAltDown() ? 0.125f : 0.25f;
    const float y = static_cast<float>(e.y);
    const float upperY = dbToY(upperThresholdDb_[idx]);
    const float lowerY = dbToY(lowerThresholdDb_[idx]);
    const bool aboveZone = y < (upperY + lowerY) * 0.5f;
    auto& ratio = aboveZone ? aboveRatio_[idx] : belowRatio_[idx];
    const float next = adjustRatio(ratio, direction * step);
    if (std::fabs(next - ratio) > 0.01f) {
        ratio = next;
        ratioScrollBand_ = band;
        ratioScrollAbove_ = aboveZone;
        rangeScrollActive_ = false;
        if (onParameterChanged)
            onParameterChanged(
                aboveZone ? aboveRatioSlotForBand(band) : belowRatioSlotForBand(band), next);
        repaint();
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

    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (float decade : {100.0f, 1000.0f, 10000.0f})
        g.drawVerticalLine(static_cast<int>(std::round(freqToX(decade))), plot.getY(),
                           plot.getBottom());
    for (float db : {-60.0f, -36.0f, -12.0f, 0.0f})
        g.drawHorizontalLine(static_cast<int>(std::round(dbToY(db))), plot.getX(), plot.getRight());

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);
    const std::array<float, 4> bandEdges{{plot.getX(), lowX, highX, plot.getRight()}};
    const std::array<juce::Colour, 3> bandColours{
        {juce::Colour(0xFF43A0FF), juce::Colour(0xFFFFB347), juce::Colour(0xFFAA66FF)}};
    const std::array<juce::String, 3> bandNames{{"LOW", "MID", "HIGH"}};
    const std::array<Handle, 3> lowerThresholdHandles{
        {Handle::LowLowerThreshold, Handle::MidLowerThreshold, Handle::HighLowerThreshold}};
    const std::array<Handle, 3> upperThresholdHandles{
        {Handle::LowUpperThreshold, Handle::MidUpperThreshold, Handle::HighUpperThreshold}};
    const std::array<Handle, 3> limitHandles{
        {Handle::LowLimit, Handle::MidLimit, Handle::HighLimit}};
    const std::array<Handle, 3> belowRatioHandles{
        {Handle::LowBelowRatio, Handle::MidBelowRatio, Handle::HighBelowRatio}};
    const std::array<Handle, 3> aboveRatioHandles{
        {Handle::LowAboveRatio, Handle::MidAboveRatio, Handle::HighAboveRatio}};
    const std::array<Handle, 3> attackHandles{
        {Handle::LowAttack, Handle::MidAttack, Handle::HighAttack}};
    const std::array<Handle, 3> releaseHandles{
        {Handle::LowRelease, Handle::MidRelease, Handle::HighRelease}};

    for (int band = 0; band < 3; ++band) {
        const auto idx = static_cast<size_t>(band);
        const float x0 = bandEdges[idx];
        const float x1 = bandEdges[idx + 1];
        if (x1 <= x0 + 2.0f)
            continue;

        const auto colour = bandColours[idx];
        const float yLower = dbToY(lowerThresholdDb_[idx]);
        const float yUpper = dbToY(upperThresholdDb_[idx]);
        const float yRangeTop = dbToY(upperThresholdDb_[idx] + rangeDb_[idx]);
        const float yRangeBottom = dbToY(lowerThresholdDb_[idx] - rangeDb_[idx]);
        const float yLimit = dbToY(limitDb_[idx]);
        const bool lowerHot = hoveredHandle_ == lowerThresholdHandles[idx] ||
                              draggedHandle_ == lowerThresholdHandles[idx];
        const bool upperHot = hoveredHandle_ == upperThresholdHandles[idx] ||
                              draggedHandle_ == upperThresholdHandles[idx];
        const bool limitHot =
            hoveredHandle_ == limitHandles[idx] || draggedHandle_ == limitHandles[idx];
        const bool aboveRatioHot =
            hoveredHandle_ == aboveRatioHandles[idx] || draggedHandle_ == aboveRatioHandles[idx];
        const bool belowRatioHot =
            hoveredHandle_ == belowRatioHandles[idx] || draggedHandle_ == belowRatioHandles[idx];
        const bool attackHot =
            hoveredHandle_ == attackHandles[idx] || draggedHandle_ == attackHandles[idx];
        const bool releaseHot =
            hoveredHandle_ == releaseHandles[idx] || draggedHandle_ == releaseHandles[idx];

        g.setColour(colour.withAlpha(0.08f));
        g.fillRect(juce::Rectangle<float>(x0 + 1.0f, plot.getY(), x1 - x0 - 2.0f,
                                          std::max(0.0f, yUpper - plot.getY())));
        g.fillRect(juce::Rectangle<float>(x0 + 1.0f, yLower, x1 - x0 - 2.0f,
                                          std::max(0.0f, plot.getBottom() - yLower)));
        g.setColour(colour.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(x0 + 1.0f, std::min(yRangeTop, yRangeBottom),
                                          x1 - x0 - 2.0f, std::fabs(yRangeBottom - yRangeTop)));

        g.setColour(colour.withAlpha(0.85f));
        g.drawLine(x0 + 2.0f, yUpper, x1 - 2.0f, yUpper, upperHot ? 2.4f : 1.6f);
        g.drawLine(x0 + 2.0f, yLower, x1 - 2.0f, yLower, lowerHot ? 2.4f : 1.6f);

        g.setColour(juce::Colours::red.withAlpha(limitHot ? 0.95f : 0.55f));
        g.drawLine(x0 + 2.0f, yLimit, x1 - 2.0f, yLimit, limitHot ? 2.0f : 1.1f);

        g.setFont(10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.34f));
        g.drawText(
            bandNames[idx],
            juce::Rectangle<float>(x0 + 4.0f, plot.getY() + 3.0f, 38.0f, 12.0f).toNearestInt(),
            juce::Justification::centredLeft);

        const bool ratioActive = ratioScrollBand_ == band && !rangeScrollActive_;
        const bool rangeActive = ratioScrollBand_ == band && rangeScrollActive_;
        const bool ratioEngaged = ratioActive || aboveRatioHot || belowRatioHot;
        const juce::String aboveRatioText = ratioLabel(aboveRatio_[idx]);
        const juce::String belowRatioText = ratioLabel(belowRatio_[idx]);
        const juce::String rangeText =
            rangeActive ? "RNG " + juce::String(rangeDb_[idx], 0) : juce::String(rangeDb_[idx], 0);
        const float cx = (x0 + x1) * 0.5f;
        auto makeBandPillArea = [&](float y) {
            constexpr float preferredWidth = 54.0f;
            constexpr float height = 15.0f;
            const float width = std::max(0.0f, std::min(preferredWidth, x1 - x0 - 6.0f));
            const float minLeft = x0 + 3.0f;
            const float maxLeft = std::max(minLeft, x1 - 3.0f - width);
            const float left = juce::jlimit(minLeft, maxLeft, cx - width * 0.5f);
            return juce::Rectangle<float>(left, y, width, height);
        };
        aboveRatioAreas_[idx] = makeBandPillArea(yUpper - 19.0f);
        belowRatioAreas_[idx] = makeBandPillArea(yLower + 4.0f);
        const float timingWidth = std::max(0.0f, std::min(70.0f, x1 - x0 - 8.0f));
        const float timingX = x0 + 4.0f;
        attackAreas_[idx] =
            juce::Rectangle<float>(timingX, plot.getY() + 18.0f, timingWidth, 14.0f);
        releaseAreas_[idx] = attackAreas_[idx].translated(0.0f, 15.0f);

        auto drawRatioPill = [&](juce::Rectangle<float> area, const juce::String& text, bool active,
                                 bool hot) {
            if (hot || active) {
                g.setColour(colour.withAlpha(hot ? 0.28f : 0.16f));
                g.fillRoundedRectangle(area, 3.0f);
                g.setColour(colour.withAlpha(0.85f));
                g.drawRoundedRectangle(area, 3.0f, 1.0f);
            }
            g.setColour(colour.withAlpha((hot || active) ? 0.98f : 0.62f));
            g.drawText(text, area.toNearestInt(), juce::Justification::centred);
        };

        drawRatioPill(aboveRatioAreas_[idx], "A " + aboveRatioText,
                      ratioActive && ratioScrollAbove_, aboveRatioHot);
        drawRatioPill(belowRatioAreas_[idx], "B " + belowRatioText,
                      ratioActive && !ratioScrollAbove_, belowRatioHot);

        auto drawTimingPill = [&](juce::Rectangle<float> area, const juce::String& text, bool hot) {
            if (hot) {
                g.setColour(colour.withAlpha(0.24f));
                g.fillRoundedRectangle(area, 3.0f);
                g.setColour(colour.withAlpha(0.85f));
                g.drawRoundedRectangle(area, 3.0f, 1.0f);
            }
            g.setColour(colour.withAlpha(hot ? 0.98f : 0.62f));
            g.drawText(text, area.toNearestInt(), juce::Justification::centred);
        };
        drawTimingPill(attackAreas_[idx], "A " + timeLabel(attackMs_[idx]), attackHot);
        drawTimingPill(releaseAreas_[idx], "R " + timeLabel(releaseMs_[idx]), releaseHot);

        g.setColour(colour.withAlpha(rangeActive ? 0.95f : 0.35f));
        g.drawText(rangeText + " dB",
                   juce::Rectangle<float>(cx - 28.0f, (yUpper + yLower) * 0.5f - 6.0f, 56.0f, 12.0f)
                       .toNearestInt(),
                   juce::Justification::centred);

        if ((lowerHot || upperHot || limitHot) && !ratioEngaged && !attackHot && !releaseHot) {
            const float valueDb = upperHot   ? upperThresholdDb_[idx]
                                  : lowerHot ? lowerThresholdDb_[idx]
                                             : limitDb_[idx];
            const auto label = juce::String(upperHot   ? "UP "
                                            : lowerHot ? "LOW "
                                                       : "LIM ") +
                               juce::String(valueDb, 1) + " dB";
            float yLabel = valueDb > -34.0f ? dbToY(valueDb) + 3.0f : dbToY(valueDb) - 15.0f;
            auto labelArea = juce::Rectangle<float>(x0 + 4.0f, yLabel, x1 - x0 - 8.0f, 13.0f);
            if (labelArea.intersects(aboveRatioAreas_[idx]) ||
                labelArea.intersects(belowRatioAreas_[idx])) {
                if (upperHot)
                    yLabel = yUpper + 5.0f;
                else if (lowerHot)
                    yLabel = yLower - 18.0f;
                labelArea.setY(
                    juce::jlimit(plot.getY(), plot.getBottom() - labelArea.getHeight(), yLabel));
            }
            g.setColour(((upperHot || lowerHot) ? colour : juce::Colours::red).withAlpha(0.95f));
            g.drawText(label, labelArea.toNearestInt(), juce::Justification::centred);
        }
    }

    auto drawXo = [&](float x, Handle h, float hz) {
        const bool active = hoveredHandle_ == h || draggedHandle_ == h;
        g.setColour(juce::Colours::white.withAlpha(active ? 0.95f : 0.5f));
        g.fillRect(juce::Rectangle<float>(x - (active ? 1.0f : 0.5f), plot.getY(),
                                          active ? 2.0f : 1.0f, plot.getHeight()));
        if (active) {
            const auto text = hz >= 1000.0f
                                  ? juce::String(hz / 1000.0f, 2) + " kHz"
                                  : juce::String(static_cast<int>(std::round(hz))) + " Hz";
            g.setFont(11.0f);
            g.drawText(
                text,
                juce::Rectangle<float>(x - 32.0f, plot.getY() + 2.0f, 64.0f, 14.0f).toNearestInt(),
                juce::Justification::centred);
        }
    };
    drawXo(lowX, Handle::LowXo, lowXoHz_);
    drawXo(highX, Handle::HighXo, highXoHz_);

    collapseButtonArea_ = juce::Rectangle<float>(
        plot.getRight() - kCollapseButtonSize - kCollapseButtonMargin,
        plot.getY() + kCollapseButtonMargin, kCollapseButtonSize, kCollapseButtonSize);

    const bool collapsed = compiledPlugin_ != nullptr && compiledPlugin_->isCurveCollapsed();
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
    g.setColour(juce::Colours::white.withAlpha(collapseButtonHovered_ ? 0.95f : 0.5f));
    g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

const CompiledPresentationSpec& getMagdaMultibandPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin::xmlTypeName,
        .layoutCellCount = 12,
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
