#include "compiled/CompiledDelayCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaDelayCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;             // ~30 Hz poll for cosmetic updates
constexpr float kAudibleFloor = 0.01f;  // ≈ -40 dB — tap below this is invisible

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

}  // namespace

CompiledDelayCurveView::CompiledDelayCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledDelayCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaDelayCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledDelayCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

float CompiledDelayCurveView::effectiveDelaySeconds() const {
    if (sync_) {
        // Faust DSP: syncedSamples = division × 60 / bpm × SR  →
        // seconds = division × 60 / bpm. Mirror that here so the timeline
        // matches the audio period exactly.
        const float safeBpm = std::max(1.0f, bpm_);
        return divisionValue_ * 60.0f / safeBpm;
    }
    return std::max(0.001f, timeMs_ * 0.001f);
}

void CompiledDelayCurveView::timerCallback() {
    using Delay = magda::daw::audio::compiled::MagdaDelayCompiledPlugin;

    float time = timeMs_, fb = feedback_, cross = cross_, divVal = divisionValue_;
    bool sync = sync_;
    int divIdx = divisionIndex_;
    float bpm = bpm_;

    if (compiledPlugin_ != nullptr) {
        if (auto* p = compiledPlugin_->getSlotParameter(Delay::kTimeSlot))
            time =
                compiledPlugin_->nativeValueToDisplayValue(Delay::kTimeSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Delay::kFeedbackSlot))
            fb = compiledPlugin_->nativeValueToDisplayValue(Delay::kFeedbackSlot,
                                                            p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Delay::kCrossSlot))
            cross =
                compiledPlugin_->nativeValueToDisplayValue(Delay::kCrossSlot, p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(Delay::kSyncSlot))
            sync = p->getCurrentValue() >= 0.5f;
        if (auto* p = compiledPlugin_->getSlotParameter(Delay::kDivisionSlot)) {
            divIdx = static_cast<int>(std::round(compiledPlugin_->nativeValueToDisplayValue(
                Delay::kDivisionSlot, p->getCurrentValue())));
        }
        bpm = compiledPlugin_->currentBpm();
    } else {
        time = valueForSlot(deviceSnapshot_, Delay::kTimeSlot, time);
        fb = valueForSlot(deviceSnapshot_, Delay::kFeedbackSlot, fb);
        cross = valueForSlot(deviceSnapshot_, Delay::kCrossSlot, cross);
        sync = valueForSlot(deviceSnapshot_, Delay::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        divIdx = static_cast<int>(std::round(
            valueForSlot(deviceSnapshot_, Delay::kDivisionSlot, static_cast<float>(divIdx))));
    }

    // Translate division index → underlying Faust value (the quarter-note
    // multiplier the DSP uses) via the plugin. Choice labels are musical
    // ("1/8.", "1/16T") and not parseable as floats — only the plugin
    // knows the real Faust values.
    if (compiledPlugin_ != nullptr)
        divVal = compiledPlugin_->divisionFaustValueForIndex(divIdx);

    const bool moved = std::fabs(time - timeMs_) > 0.5f || std::fabs(fb - feedback_) > 0.001f ||
                       std::fabs(cross - cross_) > 0.001f || sync != sync_ ||
                       divIdx != divisionIndex_ || std::fabs(divVal - divisionValue_) > 1e-4f ||
                       std::fabs(bpm - bpm_) > 0.01f;
    if (moved) {
        timeMs_ = time;
        feedback_ = fb;
        cross_ = cross;
        sync_ = sync;
        divisionIndex_ = divIdx;
        divisionValue_ = divVal;
        bpm_ = bpm;
        repaint();
    }
}

void CompiledDelayCurveView::resampleFromPlugin() {
    using Delay = magda::daw::audio::compiled::MagdaDelayCompiledPlugin;
    timeMs_ = valueForSlot(deviceSnapshot_, Delay::kTimeSlot, timeMs_);
    feedback_ = valueForSlot(deviceSnapshot_, Delay::kFeedbackSlot, feedback_);
    cross_ = valueForSlot(deviceSnapshot_, Delay::kCrossSlot, cross_);
    sync_ = valueForSlot(deviceSnapshot_, Delay::kSyncSlot, sync_ ? 1.0f : 0.0f) >= 0.5f;
    divisionIndex_ = static_cast<int>(std::round(
        valueForSlot(deviceSnapshot_, Delay::kDivisionSlot, static_cast<float>(divisionIndex_))));
    if (compiledPlugin_ != nullptr)
        divisionValue_ = compiledPlugin_->divisionFaustValueForIndex(divisionIndex_);
}

void CompiledDelayCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    const float midY = plot.getCentreY();

    // How many taps fit before we drop below the audible floor.
    const float fb = juce::jlimit(0.0f, 0.999f, feedback_);
    int maxTaps = 16;
    if (fb > 0.0f) {
        const float floorTaps = std::log(kAudibleFloor) / std::log(std::max(0.01f, fb));
        maxTaps = juce::jlimit(4, 96, static_cast<int>(std::ceil(floorTaps)));
    } else {
        maxTaps = 1;  // no feedback → single dry tap
    }

    const float delaySec = effectiveDelaySeconds();
    const float windowSec = delaySec * static_cast<float>(maxTaps + 1);
    if (windowSec <= 1.0e-6f)
        return;

    // Defer drawing the sync-mode tempo grid until after the bar-width is
    // known so it shares the same left margin as the taps; placeholder
    // here keeps the centre line / frame ordering clear.

    // Centre line — separates "L" half (above) from "R" half (below) when
    // ping-pong is engaged. Always visible so the timeline reads as
    // bipolar even at cross = 0.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.35f));
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), plot.getX(), plot.getRight());

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    const float halfH = plot.getHeight() * 0.5f;
    constexpr float kBarMinPx = 1.5f;
    const float barWidthPx =
        juce::jmax(kBarMinPx, plot.getWidth() / static_cast<float>(maxTaps) * 0.35f);

    // Indent the timeline so the dry tap (t = 0) doesn't slam against the
    // left edge of the plot frame — without this the first bar's left half
    // gets clipped by the border. Reserve a bar's worth of margin and map
    // t ∈ [0, windowSec] to the remaining width.
    const float kLeftMargin = barWidthPx;
    const float plotXOffset = plot.getX() + kLeftMargin;
    const float plotWidthInner = plot.getWidth() - kLeftMargin;

    // Sync-mode tempo grid sits behind the taps. Use the same x-mapping the
    // bars use so beats line up with the taps that fall on them.
    if (sync_ && bpm_ > 1.0f) {
        const float beatSec = 60.0f / bpm_;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
        for (float t = beatSec; t <= windowSec; t += beatSec) {
            const float x = plotXOffset + (t / windowSec) * plotWidthInner;
            g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
        }
    }

    // Tap 0 = dry signal (full amplitude on both sides at cross = 0).
    // Subsequent taps decay by Feedback^N. Cross splits successive taps
    // alternately into L (above midline) and R (below) — at cross = 1 the
    // bars stripe perfectly; at cross = 0 both sides get full bars; at
    // intermediate values they crossfade.
    for (int n = 0; n <= maxTaps; ++n) {
        // Tap 0 is the dry — full amplitude on both sides regardless of
        // cross. Subsequent taps decay by Feedback^N and split between
        // sides per the ping-pong amount: cross=0 → parallel (both sides
        // get every tap full), cross=1 → strict alternation, intermediate
        // values crossfade.
        float ampL = 0.0f;
        float ampR = 0.0f;
        if (n == 0) {
            ampL = 1.0f;
            ampR = 1.0f;
        } else {
            const float decay = std::pow(fb, static_cast<float>(n));
            const bool even = (n % 2 == 0);
            ampL = decay * (even ? 1.0f : (1.0f - cross_));
            ampR = decay * (even ? (1.0f - cross_) : 1.0f);
        }
        if (ampL < kAudibleFloor && ampR < kAudibleFloor)
            continue;

        const float t = static_cast<float>(n) * delaySec;
        const float x = plotXOffset + (t / windowSec) * plotWidthInner;

        // Upward bar (L): height ∝ ampL; Downward bar (R): height ∝ ampR.
        const float topY = midY - ampL * halfH;
        const float botY = midY + ampR * halfH;
        const float left = x - barWidthPx * 0.5f;

        // Dry tap (n = 0) draws a touch brighter so users can spot t=0.
        const float alpha = (n == 0) ? 1.0f : 0.85f;
        g.setColour(accent.withAlpha(alpha));
        g.fillRect(juce::Rectangle<float>(left, topY, barWidthPx, midY - topY));
        g.fillRect(juce::Rectangle<float>(left, midY, barWidthPx, botY - midY));
    }
}

const CompiledPresentationSpec& getMagdaDelayPresentation() {
    static const LegacyUiKind kSuppress[] = {LegacyUiKind::Delay};
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaDelayCompiledPlugin::xmlTypeName,
        .layoutCellCount = 7,
        .layoutCellsPerRow = 7,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledDelayCurveView>(pluginId);
        },
        .suppressLegacyUis = kSuppress,
    };
    return kSpec;
}

void CompiledDelayCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(dynamic_cast<magda::daw::audio::compiled::MagdaDelayCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
