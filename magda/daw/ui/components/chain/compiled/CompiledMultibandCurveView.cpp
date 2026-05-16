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
constexpr float kRatioMin = -20.0f;
constexpr float kRatioMax = 20.0f;
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
    if (std::abs(ratio - 1.0f) < 0.05f)
        return "1.0";
    return juce::String(ratio, 1);
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
    std::array<float, 3> threshold{
        readSlot(Mb::kLowThresholdSlot, thresholdDb_[0]),
        readSlot(Mb::kMidThresholdSlot, thresholdDb_[1]),
        readSlot(Mb::kHighThresholdSlot, thresholdDb_[2]),
    };
    std::array<float, 3> ratio{
        readSlot(Mb::kLowRatioSlot, ratio_[0]),
        readSlot(Mb::kMidRatioSlot, ratio_[1]),
        readSlot(Mb::kHighRatioSlot, ratio_[2]),
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
            std::fabs(threshold[static_cast<size_t>(b)] - thresholdDb_[static_cast<size_t>(b)]) >
                0.05f ||
            std::fabs(ratio[static_cast<size_t>(b)] - ratio_[static_cast<size_t>(b)]) > 0.01f ||
            std::fabs(range[static_cast<size_t>(b)] - rangeDb_[static_cast<size_t>(b)]) > 0.05f ||
            std::fabs(limit[static_cast<size_t>(b)] - limitDb_[static_cast<size_t>(b)]) > 0.05f;
    }

    if (changed) {
        lowXoHz_ = low;
        highXoHz_ = high;
        thresholdDb_ = threshold;
        ratio_ = ratio;
        rangeDb_ = range;
        limitDb_ = limit;
        repaint();
    }
}

void CompiledMultibandCurveView::resampleFromPlugin() {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    lowXoHz_ = valueForSlot(deviceSnapshot_, Mb::kLowXoSlot, lowXoHz_);
    highXoHz_ = valueForSlot(deviceSnapshot_, Mb::kHighXoSlot, highXoHz_);
    thresholdDb_[0] = valueForSlot(deviceSnapshot_, Mb::kLowThresholdSlot, thresholdDb_[0]);
    thresholdDb_[1] = valueForSlot(deviceSnapshot_, Mb::kMidThresholdSlot, thresholdDb_[1]);
    thresholdDb_[2] = valueForSlot(deviceSnapshot_, Mb::kHighThresholdSlot, thresholdDb_[2]);
    ratio_[0] = valueForSlot(deviceSnapshot_, Mb::kLowRatioSlot, ratio_[0]);
    ratio_[1] = valueForSlot(deviceSnapshot_, Mb::kMidRatioSlot, ratio_[1]);
    ratio_[2] = valueForSlot(deviceSnapshot_, Mb::kHighRatioSlot, ratio_[2]);
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

int CompiledMultibandCurveView::thresholdSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0   ? Mb::kLowThresholdSlot
           : band == 1 ? Mb::kMidThresholdSlot
                       : Mb::kHighThresholdSlot;
}

int CompiledMultibandCurveView::ratioSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowRatioSlot : band == 1 ? Mb::kMidRatioSlot : Mb::kHighRatioSlot;
}

int CompiledMultibandCurveView::rangeSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowRangeSlot : band == 1 ? Mb::kMidRangeSlot : Mb::kHighRangeSlot;
}

int CompiledMultibandCurveView::limitSlotForBand(int band) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    return band == 0 ? Mb::kLowLimitSlot : band == 1 ? Mb::kMidLimitSlot : Mb::kHighLimitSlot;
}

int CompiledMultibandCurveView::bandForHandle(Handle h) {
    switch (h) {
        case Handle::LowThreshold:
        case Handle::LowLimit:
            return 0;
        case Handle::MidThreshold:
        case Handle::MidLimit:
            return 1;
        case Handle::HighThreshold:
        case Handle::HighLimit:
            return 2;
        default:
            return -1;
    }
}

bool CompiledMultibandCurveView::isLimitHandle(Handle h) {
    return h == Handle::LowLimit || h == Handle::MidLimit || h == Handle::HighLimit;
}

int CompiledMultibandCurveView::slotForHandle(Handle h) const {
    const int band = bandForHandle(h);
    if (band < 0)
        return -1;
    return isLimitHandle(h) ? limitSlotForBand(band) : thresholdSlotForBand(band);
}

CompiledMultibandCurveView::Handle CompiledMultibandCurveView::pickHandle(float x, float y) const {
    if (plotArea_.getWidth() <= 0.0f || plotArea_.getHeight() <= 0.0f)
        return Handle::None;

    const float lowX = freqToX(lowXoHz_);
    const float highX = freqToX(highXoHz_);
    const std::array<float, 4> bandEdges{{plotArea_.getX(), lowX, highX, plotArea_.getRight()}};
    const std::array<Handle, 3> thresholdHandles{
        {Handle::LowThreshold, Handle::MidThreshold, Handle::HighThreshold}};
    const std::array<Handle, 3> limitHandles{
        {Handle::LowLimit, Handle::MidLimit, Handle::HighLimit}};

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
        check(dbToY(thresholdDb_[static_cast<size_t>(band)]), thresholdHandles[band]);
        check(dbToY(limitDb_[static_cast<size_t>(band)]), limitHandles[band]);
    }
    if (nearest != Handle::None)
        return nearest;

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
        const bool vertical = bandForHandle(picked) >= 0;
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
}

void CompiledMultibandCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Mb = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    if (draggedHandle_ == Handle::None)
        return;

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
    const float db = juce::jlimit(kDbMin, kDbMax, yToDb(static_cast<float>(e.y)));
    auto& target = isLimitHandle(draggedHandle_) ? limitDb_[static_cast<size_t>(band)]
                                                 : thresholdDb_[static_cast<size_t>(band)];
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
    const int band = bandAtX(static_cast<float>(e.x));
    if (band < 0)
        return;

    const bool adjustRange = e.mods.isShiftDown();
    const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;
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

    const float step = e.mods.isAltDown() ? 0.1f : 0.5f;
    float next = juce::jlimit(kRatioMin, kRatioMax, ratio_[idx] + direction * step);
    if (std::abs(next) < 0.05f)
        next = direction > 0.0f ? 0.05f : -0.05f;
    if (std::fabs(next - ratio_[idx]) > 0.01f) {
        ratio_[idx] = next;
        ratioScrollBand_ = band;
        rangeScrollActive_ = false;
        if (onParameterChanged)
            onParameterChanged(ratioSlotForBand(band), next);
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
    const std::array<Handle, 3> thresholdHandles{
        {Handle::LowThreshold, Handle::MidThreshold, Handle::HighThreshold}};
    const std::array<Handle, 3> limitHandles{
        {Handle::LowLimit, Handle::MidLimit, Handle::HighLimit}};

    for (int band = 0; band < 3; ++band) {
        const auto idx = static_cast<size_t>(band);
        const float x0 = bandEdges[idx];
        const float x1 = bandEdges[idx + 1];
        if (x1 <= x0 + 2.0f)
            continue;

        const auto colour = bandColours[idx];
        const float yThreshold = dbToY(thresholdDb_[idx]);
        const float yRangeTop = dbToY(thresholdDb_[idx] + rangeDb_[idx]);
        const float yRangeBottom = dbToY(thresholdDb_[idx] - rangeDb_[idx]);
        const float yLimit = dbToY(limitDb_[idx]);
        const bool thresholdHot =
            hoveredHandle_ == thresholdHandles[idx] || draggedHandle_ == thresholdHandles[idx];
        const bool limitHot =
            hoveredHandle_ == limitHandles[idx] || draggedHandle_ == limitHandles[idx];

        g.setColour(colour.withAlpha(0.08f));
        g.fillRect(juce::Rectangle<float>(x0 + 1.0f, std::min(yRangeTop, yRangeBottom),
                                          x1 - x0 - 2.0f, std::fabs(yRangeBottom - yRangeTop)));

        g.setColour(colour.withAlpha(0.85f));
        g.drawLine(x0 + 2.0f, yThreshold, x1 - 2.0f, yThreshold, thresholdHot ? 2.4f : 1.6f);

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
        const juce::String ratioText =
            ratioActive ? "R " + ratioLabel(ratio_[idx]) : ratioLabel(ratio_[idx]);
        const juce::String rangeText =
            rangeActive ? "RNG " + juce::String(rangeDb_[idx], 0) : juce::String(rangeDb_[idx], 0);
        const float cx = (x0 + x1) * 0.5f;
        g.setColour(colour.withAlpha(ratioActive ? 0.95f : 0.45f));
        g.drawText(
            ratioText,
            juce::Rectangle<float>(cx - 25.0f, yThreshold - 17.0f, 50.0f, 12.0f).toNearestInt(),
            juce::Justification::centred);
        g.setColour(colour.withAlpha(rangeActive ? 0.95f : 0.35f));
        g.drawText(
            rangeText + " dB",
            juce::Rectangle<float>(cx - 28.0f, yThreshold + 5.0f, 56.0f, 12.0f).toNearestInt(),
            juce::Justification::centred);

        if (thresholdHot || limitHot) {
            const float valueDb = thresholdHot ? thresholdDb_[idx] : limitDb_[idx];
            const auto label =
                juce::String(thresholdHot ? "THR " : "LIM ") + juce::String(valueDb, 1) + " dB";
            const float yLabel = valueDb > -34.0f ? dbToY(valueDb) + 3.0f : dbToY(valueDb) - 15.0f;
            g.setColour((thresholdHot ? colour : juce::Colours::red).withAlpha(0.95f));
            g.drawText(
                label,
                juce::Rectangle<float>(x0 + 4.0f, yLabel, x1 - x0 - 8.0f, 13.0f).toNearestInt(),
                juce::Justification::centred);
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
