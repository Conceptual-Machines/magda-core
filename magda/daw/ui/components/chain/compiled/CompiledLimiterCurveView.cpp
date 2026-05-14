#include "compiled/CompiledLimiterCurveView.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaLimiterCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 6.0f;
constexpr float kPlotPadX = 8.0f;
constexpr float kPlotPadY = 8.0f;
constexpr int kPollMs = 33;
constexpr float kHandlePickPx = 8.0f;
constexpr float kBarWidth = 36.0f;
constexpr float kBarGap = 10.0f;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

juce::String formatDb(float db) {
    if (db <= -90.0f)
        return "-inf";
    return juce::String(db, db > -10.0f ? 1 : 0);
}

}  // namespace

CompiledLimiterCurveView::CompiledLimiterCurveView(juce::String /*pluginId*/) {
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    startTimer(kPollMs);
}

void CompiledLimiterCurveView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaLimiterCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledLimiterCurveView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaLimiterCompiledPlugin*>(plugin));
}

void CompiledLimiterCurveView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    resampleFromDevice();
    repaint();
}

void CompiledLimiterCurveView::timerCallback() {
    using Lim = magda::daw::audio::compiled::MagdaLimiterCompiledPlugin;

    auto readPluginSlot = [this](int slot, float fallback) {
        if (compiledPlugin_ == nullptr)
            return fallback;
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    float threshold = thresholdDb_;
    float attack = attackMs_;
    float release = releaseMs_;

    if (compiledPlugin_ != nullptr) {
        threshold = readPluginSlot(Lim::kThresholdSlot, threshold);
        attack = readPluginSlot(Lim::kAttackSlot, attack);
        release = readPluginSlot(Lim::kReleaseSlot, release);

        inputPeakDb_ = compiledPlugin_->getInputPeakDb();
        outputPeakDb_ = compiledPlugin_->getOutputPeakDb();
        gainReductionDb_ = compiledPlugin_->getGainReductionDb();
    } else {
        threshold = valueForSlot(deviceSnapshot_, Lim::kThresholdSlot, threshold);
        attack = valueForSlot(deviceSnapshot_, Lim::kAttackSlot, attack);
        release = valueForSlot(deviceSnapshot_, Lim::kReleaseSlot, release);
    }

    // Envelope-follower smoothing for visual stability — same shape used in
    // the compressor curve view. Fast attack, slow release.
    auto envelope = [](float& smoothed, float incoming, float attackCoeff, float releaseCoeff) {
        const float coeff = incoming > smoothed ? attackCoeff : releaseCoeff;
        smoothed = smoothed * coeff + incoming * (1.0f - coeff);
    };
    envelope(smoothedInputPeakDb_, juce::jmax(-120.0f, inputPeakDb_), 0.45f, 0.88f);
    envelope(smoothedOutputPeakDb_, juce::jmax(-120.0f, outputPeakDb_), 0.45f, 0.88f);
    envelope(smoothedGainReductionDb_, juce::jlimit(0.0f, 36.0f, gainReductionDb_), 0.4f, 0.85f);

    thresholdDb_ = threshold;
    attackMs_ = attack;
    releaseMs_ = release;

    repaint();
}

void CompiledLimiterCurveView::resampleFromDevice() {
    using Lim = magda::daw::audio::compiled::MagdaLimiterCompiledPlugin;
    thresholdDb_ = valueForSlot(deviceSnapshot_, Lim::kThresholdSlot, thresholdDb_);
    attackMs_ = valueForSlot(deviceSnapshot_, Lim::kAttackSlot, attackMs_);
    releaseMs_ = valueForSlot(deviceSnapshot_, Lim::kReleaseSlot, releaseMs_);
}

float CompiledLimiterCurveView::dbToY(float db) const {
    const float t = juce::jmap(juce::jlimit(kMinDb, kMaxDb, db), kMinDb, kMaxDb, 0.0f, 1.0f);
    return meterArea_.getBottom() - t * meterArea_.getHeight();
}

float CompiledLimiterCurveView::yToDb(float y) const {
    if (meterArea_.getHeight() <= 0.0f)
        return kMinDb;
    const float t = 1.0f - (y - meterArea_.getY()) / meterArea_.getHeight();
    return juce::jmap(juce::jlimit(0.0f, 1.0f, t), kMinDb, kMaxDb);
}

CompiledLimiterCurveView::Handle CompiledLimiterCurveView::pickHandle(float y) const {
    const float thresholdY = dbToY(thresholdDb_);
    return std::fabs(y - thresholdY) <= kHandlePickPx ? Handle::Threshold : Handle::None;
}

void CompiledLimiterCurveView::mouseMove(const juce::MouseEvent& e) {
    if (draggedHandle_ != Handle::None)
        return;
    const auto picked = pickHandle(static_cast<float>(e.y));
    if (picked != hoveredHandle_) {
        hoveredHandle_ = picked;
        setMouseCursor(picked == Handle::None ? juce::MouseCursor::NormalCursor
                                              : juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }
}

void CompiledLimiterCurveView::mouseExit(const juce::MouseEvent&) {
    if (draggedHandle_ == Handle::None) {
        hoveredHandle_ = Handle::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CompiledLimiterCurveView::mouseDown(const juce::MouseEvent& e) {
    const auto picked = pickHandle(static_cast<float>(e.y));
    if (picked == Handle::None)
        return;
    draggedHandle_ = picked;
    hoveredHandle_ = picked;
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void CompiledLimiterCurveView::mouseDrag(const juce::MouseEvent& e) {
    using Lim = magda::daw::audio::compiled::MagdaLimiterCompiledPlugin;
    if (draggedHandle_ != Handle::Threshold)
        return;
    const float threshold = juce::jlimit(-24.0f, 0.0f, yToDb(static_cast<float>(e.y)));
    if (std::fabs(threshold - thresholdDb_) > 0.05f) {
        thresholdDb_ = threshold;
        if (onParameterChanged)
            onParameterChanged(Lim::kThresholdSlot, threshold);
        repaint();
    }
}

void CompiledLimiterCurveView::mouseUp(const juce::MouseEvent& e) {
    draggedHandle_ = Handle::None;
    hoveredHandle_ = pickHandle(static_cast<float>(e.y));
    setMouseCursor(hoveredHandle_ == Handle::None ? juce::MouseCursor::NormalCursor
                                                  : juce::MouseCursor::UpDownResizeCursor);
    repaint();
}

void CompiledLimiterCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.06f));
    g.fillRect(bounds);

    auto area = bounds.toFloat().reduced(kPlotPadX, kPlotPadY);
    if (area.getWidth() < 60.0f || area.getHeight() < 32.0f)
        return;

    // Reserve the top strip for labels, bottom-most pixels carry the dB
    // readouts. The meter bars sit in the middle.
    auto labelStrip = area.removeFromTop(14.0f);
    auto readoutStrip = area.removeFromBottom(14.0f);
    meterArea_ = area;

    const auto border = DarkTheme::getColour(DarkTheme::BORDER);
    const auto text = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    const auto grColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto inColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

    g.setColour(border.withAlpha(0.55f));
    g.drawRect(meterArea_, 1.0f);

    // Three bars: Input | Output | GR. Lay them out from the left of the
    // plot area; the threshold ceiling line spans the IN+OUT pair.
    const float barH = meterArea_.getHeight();
    const float startX = meterArea_.getX() + 12.0f;
    juce::Rectangle<float> inBar(startX, meterArea_.getY(), kBarWidth, barH);
    juce::Rectangle<float> outBar(inBar.getRight() + kBarGap, meterArea_.getY(), kBarWidth, barH);
    juce::Rectangle<float> grBar(meterArea_.getRight() - kBarWidth - 12.0f, meterArea_.getY(),
                                 kBarWidth, barH);

    // Bar backgrounds.
    g.setColour(border.withAlpha(0.22f));
    g.fillRoundedRectangle(inBar, 2.0f);
    g.fillRoundedRectangle(outBar, 2.0f);
    g.fillRoundedRectangle(grBar, 2.0f);

    // Bar grid lines every 12 dB.
    g.setColour(border.withAlpha(0.18f));
    for (float db : {-48.0f, -36.0f, -24.0f, -12.0f, 0.0f}) {
        const float y = dbToY(db);
        g.drawLine(inBar.getX(), y, outBar.getRight(), y, 1.0f);
    }

    // Input + output bar fills.
    auto fillBar = [&](juce::Rectangle<float> bar, float db, juce::Colour fill) {
        const float yTop = dbToY(juce::jlimit(kMinDb, kMaxDb, db));
        const juce::Rectangle<float> level(bar.getX(), yTop, bar.getWidth(),
                                           bar.getBottom() - yTop);
        g.setColour(fill.withAlpha(0.85f));
        g.fillRoundedRectangle(level, 2.0f);
    };
    fillBar(inBar, smoothedInputPeakDb_, inColour);
    fillBar(outBar, smoothedOutputPeakDb_, accent);

    // Threshold ceiling line across input + output bars. Hover/drag thickens
    // it and brightens the colour so the user can see it's draggable.
    const float thresholdY = dbToY(thresholdDb_);
    const bool thresholdHot =
        hoveredHandle_ == Handle::Threshold || draggedHandle_ == Handle::Threshold;
    g.setColour(grColour.withAlpha(thresholdHot ? 0.95f : 0.7f));
    g.drawLine(inBar.getX() - 2.0f, thresholdY, outBar.getRight() + 2.0f, thresholdY,
               thresholdHot ? 2.0f : 1.2f);
    g.fillEllipse(outBar.getRight() + 4.0f, thresholdY - 3.5f, 7.0f, 7.0f);

    // Gain-reduction bar grows downward from the top. Range 0..24 dB.
    g.setColour(grColour.withAlpha(0.85f));
    const float gr01 = juce::jlimit(0.0f, 1.0f, smoothedGainReductionDb_ / 24.0f);
    const juce::Rectangle<float> grFill(grBar.getX(), grBar.getY(), grBar.getWidth(),
                                        grBar.getHeight() * gr01);
    g.fillRoundedRectangle(grFill, 2.0f);

    // Labels (top strip).
    auto font = FontManager::getInstance().getUIFont(9.0f);
    g.setFont(font);
    g.setColour(text.withAlpha(0.85f));
    g.drawFittedText(
        "IN", inBar.withY(labelStrip.getY()).withHeight(labelStrip.getHeight()).toNearestInt(),
        juce::Justification::centred, 1);
    g.drawFittedText(
        "OUT", outBar.withY(labelStrip.getY()).withHeight(labelStrip.getHeight()).toNearestInt(),
        juce::Justification::centred, 1);
    g.drawFittedText(
        "GR", grBar.withY(labelStrip.getY()).withHeight(labelStrip.getHeight()).toNearestInt(),
        juce::Justification::centred, 1);

    // Bottom dB readouts.
    g.setColour(text.withAlpha(0.95f));
    g.drawFittedText(
        formatDb(smoothedInputPeakDb_),
        inBar.withY(readoutStrip.getY()).withHeight(readoutStrip.getHeight()).toNearestInt(),
        juce::Justification::centred, 1);
    g.drawFittedText(
        formatDb(smoothedOutputPeakDb_),
        outBar.withY(readoutStrip.getY()).withHeight(readoutStrip.getHeight()).toNearestInt(),
        juce::Justification::centred, 1);
    g.drawFittedText(
        "-" + juce::String(smoothedGainReductionDb_, 1),
        grBar.withY(readoutStrip.getY()).withHeight(readoutStrip.getHeight()).toNearestInt(),
        juce::Justification::centred, 1);

    // Threshold readout, top-right corner.
    g.setColour(grColour.withAlpha(0.9f));
    g.drawFittedText("THR " + juce::String(thresholdDb_, 1) + " dB",
                     juce::Rectangle<int>(static_cast<int>(grBar.getRight()) -
                                              static_cast<int>(grBar.getWidth() * 2.0f) - 8,
                                          static_cast<int>(labelStrip.getY()),
                                          static_cast<int>(grBar.getWidth() * 2.0f),
                                          static_cast<int>(labelStrip.getHeight())),
                     juce::Justification::right, 1);
}

const CompiledPresentationSpec& getMagdaLimiterPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaLimiterCompiledPlugin::xmlTypeName,
        .layoutCellCount = 7,
        .layoutCellsPerRow = 7,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledLimiterCurveView>(pluginId);
        },
        .suppressLegacyUis = {},
    };
    return kSpec;
}

}  // namespace magda::daw::ui
