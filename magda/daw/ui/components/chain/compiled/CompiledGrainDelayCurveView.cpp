#include "compiled/CompiledGrainDelayCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaGrainDelayCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kAudibleFloor = 0.01f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

}  // namespace

CompiledGrainDelayCurveView::CompiledGrainDelayCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(false, false);
    startTimer(kPollMs);
}

void CompiledGrainDelayCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledGrainDelayCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromPlugin();
    repaint();
}

float CompiledGrainDelayCurveView::effectiveDelaySeconds() const {
    if (sync_) {
        const float safeBpm = std::max(1.0f, bpm_);
        return divisionValue_ * 60.0f / safeBpm;
    }
    return std::max(0.001f, timeMs_ * 0.001f);
}

void CompiledGrainDelayCurveView::timerCallback() {
    using GrainDelay = magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin;

    float time = timeMs_, size = sizeMs_, pitch = pitchSt_, spray = spray_, fb = feedback_;
    float mix = mix_, divVal = divisionValue_;
    bool sync = sync_;
    int divIdx = divisionIndex_;

    float bpm = bpm_;

    if (compiledPlugin_ != nullptr) {
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kTimeSlot))
            time = compiledPlugin_->nativeValueToDisplayValue(GrainDelay::kTimeSlot,
                                                              p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kSizeSlot))
            size = compiledPlugin_->nativeValueToDisplayValue(GrainDelay::kSizeSlot,
                                                              p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kPitchSlot))
            pitch = compiledPlugin_->nativeValueToDisplayValue(GrainDelay::kPitchSlot,
                                                               p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kSpraySlot))
            spray = compiledPlugin_->nativeValueToDisplayValue(GrainDelay::kSpraySlot,
                                                               p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kFeedbackSlot))
            fb = compiledPlugin_->nativeValueToDisplayValue(GrainDelay::kFeedbackSlot,
                                                            p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kMixSlot))
            mix = compiledPlugin_->nativeValueToDisplayValue(GrainDelay::kMixSlot,
                                                             p->getCurrentValue());
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kSyncSlot))
            sync = p->getCurrentValue() >= 0.5f;
        if (auto* p = compiledPlugin_->getSlotParameter(GrainDelay::kDivisionSlot)) {
            divIdx = static_cast<int>(std::round(compiledPlugin_->nativeValueToDisplayValue(
                GrainDelay::kDivisionSlot, p->getCurrentValue())));
        }
        bpm = compiledPlugin_->currentBpm();
    } else {
        time = valueForSlot(deviceSnapshot_, GrainDelay::kTimeSlot, time);
        size = valueForSlot(deviceSnapshot_, GrainDelay::kSizeSlot, size);
        pitch = valueForSlot(deviceSnapshot_, GrainDelay::kPitchSlot, pitch);
        spray = valueForSlot(deviceSnapshot_, GrainDelay::kSpraySlot, spray);
        fb = valueForSlot(deviceSnapshot_, GrainDelay::kFeedbackSlot, fb);
        mix = valueForSlot(deviceSnapshot_, GrainDelay::kMixSlot, mix);
        sync = valueForSlot(deviceSnapshot_, GrainDelay::kSyncSlot, sync ? 1.0f : 0.0f) >= 0.5f;
        divIdx = static_cast<int>(std::round(
            valueForSlot(deviceSnapshot_, GrainDelay::kDivisionSlot, static_cast<float>(divIdx))));
    }

    if (compiledPlugin_ != nullptr)
        divVal = compiledPlugin_->divisionFaustValueForIndex(divIdx);

    const bool moved = std::fabs(time - timeMs_) > 0.5f || std::fabs(size - sizeMs_) > 0.5f ||
                       std::fabs(pitch - pitchSt_) > 0.01f || std::fabs(spray - spray_) > 0.001f ||
                       std::fabs(fb - feedback_) > 0.001f || std::fabs(mix - mix_) > 0.001f ||
                       sync != sync_ || divIdx != divisionIndex_ ||
                       std::fabs(divVal - divisionValue_) > 1e-4f || std::fabs(bpm - bpm_) > 0.01f;
    if (moved) {
        timeMs_ = time;
        sizeMs_ = size;
        pitchSt_ = pitch;
        spray_ = spray;
        feedback_ = fb;
        mix_ = mix;
        sync_ = sync;
        divisionIndex_ = divIdx;
        divisionValue_ = divVal;
        bpm_ = bpm;
        repaint();
    }
}

void CompiledGrainDelayCurveView::resampleFromPlugin() {
    using GrainDelay = magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin;
    timeMs_ = valueForSlot(deviceSnapshot_, GrainDelay::kTimeSlot, timeMs_);
    sizeMs_ = valueForSlot(deviceSnapshot_, GrainDelay::kSizeSlot, sizeMs_);
    pitchSt_ = valueForSlot(deviceSnapshot_, GrainDelay::kPitchSlot, pitchSt_);
    spray_ = valueForSlot(deviceSnapshot_, GrainDelay::kSpraySlot, spray_);
    feedback_ = valueForSlot(deviceSnapshot_, GrainDelay::kFeedbackSlot, feedback_);
    mix_ = valueForSlot(deviceSnapshot_, GrainDelay::kMixSlot, mix_);
    sync_ = valueForSlot(deviceSnapshot_, GrainDelay::kSyncSlot, sync_ ? 1.0f : 0.0f) >= 0.5f;
    divisionIndex_ = static_cast<int>(std::round(valueForSlot(
        deviceSnapshot_, GrainDelay::kDivisionSlot, static_cast<float>(divisionIndex_))));
    if (compiledPlugin_ != nullptr)
        divisionValue_ = compiledPlugin_->divisionFaustValueForIndex(divisionIndex_);
}

void CompiledGrainDelayCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    const auto plotBg = DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f);
    g.setColour(plotBg);
    g.fillRect(bounds);

    auto plot = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
    g.drawRect(plot, 1.0f);

    // Clip everything that follows to the plot's interior so wet rects
    // at extreme settings (Time near 0, Size large, lots of taps) can't
    // bleed through the frame.
    juce::Graphics::ScopedSaveState clipGuard(g);
    g.reduceClipRegion(plot.toNearestInt());

    // Midline doubles as the pitch=0 reference. Grains float above when
    // pitched up, below when pitched down — so the bipolar axis carries
    // direction, not just amplitude.
    const float midY = plot.getCentreY();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.35f));
    g.drawHorizontalLine(static_cast<int>(std::round(midY)), plot.getX(), plot.getRight());

    const float fb = juce::jlimit(0.0f, 0.999f, feedback_);
    int maxTaps = 16;
    if (fb > 0.0f) {
        const float floorTaps = std::log(kAudibleFloor) / std::log(std::max(0.01f, fb));
        maxTaps = juce::jlimit(4, 96, static_cast<int>(std::ceil(floorTaps)));
    } else {
        maxTaps = 1;
    }

    const float delaySec = effectiveDelaySeconds();
    const float grainSec = std::max(0.001f, sizeMs_ * 0.001f);
    const float windowSec = delaySec * static_cast<float>(maxTaps + 1) + grainSec;
    if (windowSec <= 1.0e-6f)
        return;

    const float halfH = plot.getHeight() * 0.5f;
    constexpr float kBarMinPx = 1.5f;
    const float barWidthPx =
        juce::jmax(kBarMinPx, plot.getWidth() / static_cast<float>(maxTaps) * 0.35f);
    const float plotXOffset = plot.getX() + barWidthPx;
    const float plotWidthInner = plot.getWidth() - barWidthPx * 1.5f;

    if (sync_ && bpm_ > 1.0f) {
        const float beatSec = 60.0f / bpm_;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.18f));
        for (float t = beatSec; t <= windowSec; t += beatSec) {
            const float x = plotXOffset + (t / windowSec) * plotWidthInner;
            g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
        }
    }

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    const auto grainColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

    // Pitch shifts the wet rectangles vertically — grains float above the
    // midline at +st and below at -st. ±0.7·halfH at ±24 st leaves room
    // for the rect's own half-height without overshooting the plot.
    const float pitchY = midY - juce::jlimit(-1.0f, 1.0f, pitchSt_ / 24.0f) * halfH * 0.7f;

    const float spray01 = juce::jlimit(0.0f, 1.0f, spray_);
    const float mix01 = juce::jlimit(0.0f, 1.0f, mix_);
    const float maxJitterSec = grainSec * spray01;

    for (int n = 0; n <= maxTaps; ++n) {
        const float decay = (n == 0) ? 1.0f : std::pow(fb, static_cast<float>(n));
        if (decay < kAudibleFloor)
            continue;

        const float t = static_cast<float>(n) * delaySec;
        const float x = plotXOffset + (t / windowSec) * plotWidthInner;

        if (n == 0) {
            // Dry tap: thin green bar at the midline. Same idiom as the
            // regular compiled delay so the dry hit anchors the timeline.
            const float left = x - barWidthPx * 0.5f;
            g.setColour(accent);
            g.fillRect(juce::Rectangle<float>(left, midY - halfH, barWidthPx, halfH));
            g.fillRect(juce::Rectangle<float>(left, midY, barWidthPx, halfH));
            continue;
        }

        // Wet tap: blue rectangle whose width tracks the grain duration
        // (Size visibly fattens each block) and whose vertical centre
        // sits at pitchY (Pitch slides the trail up/down). Half-height
        // decays geometrically by feedback × Mix; clamped so the rect
        // never overruns the pitch headroom (±0.3·halfH from pitchY).
        const float grainPx = juce::jmax(barWidthPx, (grainSec / windowSec) * plotWidthInner);
        const float maxHalfBar = juce::jmax(2.0f, halfH - std::fabs(pitchY - midY));
        const float halfBarH = juce::jmin(maxHalfBar, halfH * decay * mix01 * 0.45f);
        if (halfBarH < 0.5f)
            continue;

        // Right edge of the dry bar — wet rects must not intrude past
        // this so the green source tap stays visible at extreme settings
        // (e.g. Time = 1 ms with Size = 500 ms, where the grain is
        // wider than the entire delay window and would otherwise paint
        // straight across the dry hit).
        const float wetLeftLimit = plotXOffset + barWidthPx * 0.5f;

        auto drawWetRect = [&](float cx, float widthPx, juce::Colour colour) {
            float left = cx - widthPx * 0.5f;
            const float right = cx + widthPx * 0.5f;
            left = juce::jmax(left, wetLeftLimit);
            if (right - left < 0.5f)
                return;
            g.setColour(colour);
            g.fillRect(
                juce::Rectangle<float>(left, pitchY - halfBarH, right - left, halfBarH * 2.0f));
        };

        // Spray = horizontal blur. Stack 6 stepped rects, each wider
        // than the last, with the colour interpolated toward the plot
        // background — outer steps fade to dark so the cluster reads
        // as a soft halo around the primary. No alpha needed: the
        // interpolation produces solid colours that blend visually.
        if (spray01 > 0.001f) {
            constexpr int kSteps = 6;
            const float maxJitterPx = (maxJitterSec / windowSec) * plotWidthInner;
            for (int s = kSteps; s >= 1; --s) {
                const float frac = static_cast<float>(s) / static_cast<float>(kSteps);
                const float widthPx = grainPx + 2.0f * frac * maxJitterPx;
                const float fade = 0.30f + frac * 0.55f;
                const auto stepColour = grainColour.interpolatedWith(plotBg, fade);
                drawWetRect(x, widthPx, stepColour);
            }
        }

        drawWetRect(x, grainPx, grainColour);
    }
}

const CompiledPresentationSpec& getMagdaGrainDelayPresentation() {
    static const LegacyUiKind kSuppress[] = {LegacyUiKind::Delay};
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin::xmlTypeName,
        .layoutCellCount = 8,
        .layoutCellsPerRow = 8,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledGrainDelayCurveView>(pluginId);
        },
        .suppressLegacyUis = kSuppress,
    };
    return kSpec;
}

void CompiledGrainDelayCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin*>(plugin));
}

}  // namespace magda::daw::ui
