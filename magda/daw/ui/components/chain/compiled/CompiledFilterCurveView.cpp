#include "compiled/CompiledFilterCurveView.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/plugins/compiled/MagdaFilterCompiledPlugin.hpp"
#include "core/ParameterUtils.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kMinCutoffHz = 5.0f;
constexpr float kMaxFreq = 20000.0f;
constexpr float kMinDb = -60.0f;
constexpr float kBaseMaxDb = 18.0f;
constexpr float kHardMaxDb = 72.0f;
constexpr float kPlotPadX = 0.0f;  // The curve runs the graph's full width.
constexpr float kPlotPadY = 6.0f;
constexpr float kCurveSmoothing = 0.24f;
constexpr int kAnimationPollMs = 16;  // ~60 Hz when something's actually moving
constexpr int kIdlePollMs = 100;      // 10 Hz lazy poll to catch LFO retriggers
constexpr int kSpectrumPollMs = 50;   // The EQ's rate, while a device feeds the spectrum

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters) {
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    }
    return fallback;
}

const magda::ParameterInfo* paramForSlot(const magda::DeviceInfo& device, int slotIndex) {
    for (const auto& param : device.parameters) {
        if (param.paramIndex == slotIndex)
            return &param;
    }
    return nullptr;
}

float modulatedValueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback,
                            const ParamLinkContext* linkContext,
                            magda::daw::audio::compiled::MagdaFilterCompiledPlugin* plugin) {
    if (plugin != nullptr) {
        if (auto param = plugin->getSlotParameter(slotIndex))
            return plugin->normalizedToDisplay(slotIndex, param.currentValue());
    }

    const auto* param = paramForSlot(device, slotIndex);
    if (param == nullptr)
        return fallback;

    if (linkContext == nullptr)
        return param->currentValue;

    auto slotContext = *linkContext;
    slotContext.paramIndex = slotIndex;

    const float baseNorm = magda::ParameterUtils::realToNormalized(param->currentValue, *param);
    const float modNorm =
        computeTotalModModulation(slotContext) + computeTotalMacroModulation(slotContext);
    const float effectiveNorm = juce::jlimit(0.0f, 1.0f, baseNorm + modNorm);
    return magda::ParameterUtils::normalizedToReal(effectiveNorm, *param);
}

float freqToX(float freq, float width, float minFrequencyHz) {
    const float norm = std::log(freq / minFrequencyHz) / std::log(kMaxFreq / minFrequencyHz);
    return juce::jlimit(0.0f, 1.0f, norm) * width;
}

float dbToY(float db, float height, float maxDb) {
    const float norm = (juce::jlimit(kMinDb, maxDb, db) - kMinDb) / (maxDb - kMinDb);
    return height * (1.0f - norm);
}

float xToFreq(float x, float width, float minFrequencyHz) {
    const float norm = width > 0.0f ? juce::jlimit(0.0f, 1.0f, x / width) : 0.0f;
    return minFrequencyHz * std::pow(kMaxFreq / minFrequencyHz, norm);
}

float linearToDb(float linear) {
    return 20.0f * std::log10(std::max(linear, 1.0e-5f));
}

float nextSmoothedValue(float current, float target) {
    return current + (target - current) * kCurveSmoothing;
}

float nextSmoothedFrequency(float current, float target) {
    const float currentLog = std::log(juce::jlimit(kMinCutoffHz, kMaxFreq, current));
    const float targetLog = std::log(juce::jlimit(kMinCutoffHz, kMaxFreq, target));
    return std::exp(nextSmoothedValue(currentLog, targetLog));
}

float expandedMaxDb(float responseMaxDb) {
    if (responseMaxDb <= kBaseMaxDb - 3.0f)
        return kBaseMaxDb;

    const float stepped = std::ceil((responseMaxDb + 3.0f) / 6.0f) * 6.0f;
    return juce::jlimit(kBaseMaxDb, kHardMaxDb, stepped);
}

}  // namespace

CompiledFilterCurveView::CompiledFilterCurveView(juce::String pluginId) {
    // Poly Synth's filter cannot go below 50 Hz. Keep the shared filter view's
    // wider 20 Hz range without wasting a full invisible decade in the synth.
    if (pluginId == "magda_polysynth")
        minPlotFrequencyHz_ = 50.0f;

    // Family is read from the plugin's Engine parameter every refresh —
    // the unified MagdaFilterCompiledPlugin holds all five engines and
    // exposes which one is active via slot kEngineSlot.
    family_ = FilterFamily::SVF;
    // Read-only until a host hands it a parameter callback.
    setInterceptsMouseClicks(false, false);
}

void CompiledFilterCurveView::setCompiledPlugin(
    std::shared_ptr<magda::daw::audio::compiled::MagdaFilterCompiledPlugin> plugin) {
    // The slot rebinds on every modulation refresh; only a different device restarts the traces.
    if (plugin == compiledPlugin_)
        return;
    compiledPlugin_ = std::move(plugin);
    spectrum_.reset();
}

void CompiledFilterCurveView::setRawState(int engine, int modeIndex, float cutoffHz,
                                          float resonance01, float drive01, bool doubleSlope) {
    compiledPlugin_ = nullptr;  // raw path: never read from a plugin / snapshot
    if (doublePole_ != doubleSlope) {
        doublePole_ = doubleSlope;
        repaint();
    }
    family_ = static_cast<FilterFamily>(juce::jlimit(0, 4, engine));
    targetModeIndex_ = juce::jlimit(0, 3, modeIndex);
    targetCutoffHz_ = juce::jlimit(kMinCutoffHz, kMaxFreq, cutoffHz);
    targetResonance_ = juce::jlimit(0.0f, 1.0f, resonance01);
    targetDrive_ = juce::jlimit(0.0f, 1.0f, drive01);

    if (!initialised_) {
        cutoffHz_ = targetCutoffHz_;
        resonance_ = targetResonance_;
        drive_ = targetDrive_;
        modeIndex_ = targetModeIndex_;
        initialised_ = true;
        repaint();
        return;
    }

    modeIndex_ = targetModeIndex_;
    const bool needsAnimation = std::abs(std::log(cutoffHz_ / targetCutoffHz_)) > 0.0005f ||
                                std::abs(resonance_ - targetResonance_) > 0.0005f ||
                                std::abs(drive_ - targetDrive_) > 0.0005f;
    if (needsAnimation && !isTimerRunning())
        startTimerHz(60);
    else
        repaint();
}

void CompiledFilterCurveView::updateFromDevice(const magda::DeviceInfo& device,
                                               const ParamLinkContext* linkContext) {
    deviceSnapshot_ = device;
    juce::ignoreUnused(linkContext);  // Read fresh through linkContextProvider_.

    updateTargetValues();

    if (!initialised_) {
        cutoffHz_ = targetCutoffHz_;
        resonance_ = targetResonance_;
        drive_ = targetDrive_;
        modeIndex_ = targetModeIndex_;
        initialised_ = true;
        repaint();
        return;
    }

    if (targetModeIndex_ != modeIndex_) {
        modeIndex_ = targetModeIndex_;
        repaint();
    }

    const bool needsAnimation = std::abs(std::log(cutoffHz_ / targetCutoffHz_)) > 0.0005f ||
                                std::abs(resonance_ - targetResonance_) > 0.0005f ||
                                std::abs(drive_ - targetDrive_) > 0.0005f;
    if ((needsAnimation || hasActiveCurveLinks() || compiledPlugin_ != nullptr) &&
        !isTimerRunning())
        startTimerHz(60);
}

void CompiledFilterCurveView::updateTargetValues() {
    using FilterFamily = CompiledFilterCurveView::FilterFamily;
    const auto resolved = linkContextProvider_ ? linkContextProvider_() : std::nullopt;
    const ParamLinkContext* linkContext = resolved ? &*resolved : nullptr;
    const float cutoff =
        modulatedValueForSlot(deviceSnapshot_, 0, cutoffHz_, linkContext, compiledPlugin_.get());
    const float resonance =
        modulatedValueForSlot(deviceSnapshot_, 1, resonance_, linkContext, compiledPlugin_.get());
    const float drive =
        modulatedValueForSlot(deviceSnapshot_, 2, drive_, linkContext, compiledPlugin_.get());

    using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
    const int engine = static_cast<int>(std::round(valueForSlot(
        deviceSnapshot_, Filter::kEngineSlot, static_cast<float>(static_cast<int>(family_)))));
    const int mode = static_cast<int>(std::round(
        valueForSlot(deviceSnapshot_, Filter::kModeSlot, static_cast<float>(modeIndex_))));

    targetCutoffHz_ = juce::jlimit(kMinCutoffHz, kMaxFreq, cutoff);
    targetResonance_ = juce::jlimit(0.0f, 1.0f, resonance);
    targetDrive_ = juce::jlimit(0.0f, 1.0f, drive);
    targetModeIndex_ = mode;
    family_ = static_cast<FilterFamily>(juce::jlimit(0, 4, engine));
}

bool CompiledFilterCurveView::hasActiveCurveLinks() const {
    const auto resolved = linkContextProvider_ ? linkContextProvider_() : std::nullopt;
    if (!resolved)
        return false;

    for (int slotIndex : {0, 1, 2}) {
        auto slotContext = *resolved;
        slotContext.paramIndex = slotIndex;
        if (hasActiveLinks(slotContext))
            return true;
    }
    return false;
}

void CompiledFilterCurveView::timerCallback() {
    if (compiledPlugin_ != nullptr || hasActiveCurveLinks())
        updateTargetValues();
    if (compiledPlugin_ != nullptr)
        spectrum_.update(compiledPlugin_->getPreSpectrumTapBuffer(),
                         compiledPlugin_->getPostSpectrumTapBuffer());

    cutoffHz_ = nextSmoothedFrequency(cutoffHz_, targetCutoffHz_);
    resonance_ = nextSmoothedValue(resonance_, targetResonance_);
    drive_ = nextSmoothedValue(drive_, targetDrive_);

    const bool settled = std::abs(cutoffHz_ - targetCutoffHz_) < 0.25f &&
                         std::abs(resonance_ - targetResonance_) < 0.0005f &&
                         std::abs(drive_ - targetDrive_) < 0.0005f;

    if (settled) {
        cutoffHz_ = targetCutoffHz_;
        resonance_ = targetResonance_;
        drive_ = targetDrive_;
        // Drop to a lazy poll rate so the next visual-sim tick (LFO retrigger,
        // macro change, automation movement) wakes us back up promptly, but we
        // don't keep redrawing at 60 Hz with nothing to show.
        if (compiledPlugin_ != nullptr) {
            if (getTimerInterval() != kSpectrumPollMs)
                startTimer(kSpectrumPollMs);
        } else if (!hasActiveCurveLinks()) {
            stopTimer();
        }
    } else {
        if (getTimerInterval() != kAnimationPollMs)
            startTimer(kAnimationPollMs);
    }

    repaint();
}

CompiledFilterCurveView::FilterMode CompiledFilterCurveView::modeForIndex() const {
    if (family_ == FilterFamily::Ladder)
        return FilterMode::LowPass;
    if (family_ == FilterFamily::Korg35)
        return modeIndex_ == 1 ? FilterMode::HighPass : FilterMode::LowPass;
    if (family_ == FilterFamily::SallenKey) {
        if (modeIndex_ == 1)
            return FilterMode::BandPass;
        if (modeIndex_ == 2)
            return FilterMode::HighPass;
        return FilterMode::LowPass;
    }
    if (modeIndex_ == 1)
        return FilterMode::BandPass;
    if (modeIndex_ == 2)
        return FilterMode::HighPass;
    if (modeIndex_ == 3)
        return FilterMode::Notch;
    return FilterMode::LowPass;
}

float CompiledFilterCurveView::qValue() const {
    // The engines' own exponential ranges (faust_dsp/compiled/filter).
    const auto exponential = [this](float low, float high) {
        return low * std::pow(high / low, resonance_);
    };
    switch (family_) {
        case FilterFamily::SVF:
            return exponential(0.5f, 40.0f);
        case FilterFamily::Ladder:
            return 0.6f + resonance_ * 9.4f;
        case FilterFamily::Oberheim:
            return exponential(0.5f, 30.0f);
        case FilterFamily::Korg35:
        case FilterFamily::SallenKey:
            return exponential(0.7f, 10.0f);
    }
    return 1.0f;
}

float CompiledFilterCurveView::responseDbAt(float frequencyHz) const {
    const float r = juce::jlimit(0.001f, 1000.0f, frequencyHz / cutoffHz_);
    const float q = qValue();
    const float denom = std::sqrt(std::pow(1.0f - r * r, 2.0f) + std::pow(r / q, 2.0f));

    float magnitude = 1.0f;
    switch (modeForIndex()) {
        case FilterMode::LowPass:
            magnitude = 1.0f / denom;
            if (family_ == FilterFamily::Ladder)
                magnitude *= magnitude;
            break;
        case FilterMode::BandPass:
            magnitude = (r / q) / denom;
            break;
        case FilterMode::HighPass:
            magnitude = (r * r) / denom;
            break;
        case FilterMode::Notch:
            magnitude = std::abs(1.0f - r * r) / denom;
            break;
    }

    if (doublePole_)
        magnitude *= magnitude;  // 24 dB/oct: two cascaded stages

    const float driveTrimDb = -drive_ * 2.5f;
    return linearToDb(magnitude) + driveTrimDb;
}

void CompiledFilterCurveView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    const auto plot = plotBounds();
    plotArea_ = plot;
    if (plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    auto& fonts = FontManager::getInstance();
    g.setFont(fonts.getMonoFont(9.0f));

    struct FreqLine {
        float freq;
        const char* label;
    };
    const FreqLine freqLines[] = {{50.0f, "50"},   {100.0f, "100"},    {500.0f, nullptr},
                                  {1000.0f, "1k"}, {5000.0f, nullptr}, {10000.0f, "10k"}};

    for (const auto& line : freqLines) {
        const float x = plot.getX() + freqToX(line.freq, plot.getWidth(), minPlotFrequencyHz_);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
        g.drawVerticalLine(static_cast<int>(std::round(x)), plot.getY(), plot.getBottom());
        if (line.label != nullptr) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
            g.drawText(line.label, static_cast<int>(x) + 4, static_cast<int>(plot.getBottom()) - 14,
                       40, 12, juce::Justification::centredLeft);
        }
    }

    const int samples = juce::jmax(64, static_cast<int>(std::ceil(plot.getWidth())));
    std::vector<float> responseDbs;
    responseDbs.reserve(static_cast<size_t>(samples));
    float maxResponseDb = kMinDb;
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
        const float freq = xToFreq(t * plot.getWidth(), plot.getWidth(), minPlotFrequencyHz_);
        const float db = responseDbAt(freq);
        responseDbs.push_back(db);
        maxResponseDb = std::max(maxResponseDb, db);
    }

    const float maxDb = expandedMaxDb(maxResponseDb);

    for (float db : {-24.0f, -12.0f, 0.0f, 12.0f}) {
        const float y = plot.getY() + dbToY(db, plot.getHeight(), maxDb);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
        g.drawHorizontalLine(static_cast<int>(std::round(y)), plot.getX(), plot.getRight());
    }

    juce::Path fillPath;
    juce::Path curvePath;

    if (compiledPlugin_ != nullptr)
        spectrum_.draw(g, plot, compiledPlugin_->getSampleRate(), minPlotFrequencyHz_, kMaxFreq,
                       [&plot, this](float hz) {
                           return plot.getX() + freqToX(hz, plot.getWidth(), minPlotFrequencyHz_);
                       });

    const float zeroY = plot.getY() + dbToY(0.0f, plot.getHeight(), maxDb);
    bool curveBroken = true;
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
        const float x = plot.getX() + t * plot.getWidth();
        const float y =
            plot.getY() + dbToY(responseDbs[static_cast<size_t>(i)], plot.getHeight(), maxDb);

        if (i == 0) {
            fillPath.startNewSubPath(x, zeroY);
            fillPath.lineTo(x, y);
        } else {
            fillPath.lineTo(x, y);
        }
        // Below the floor the response is clamped to the bottom edge; the line breaks there.
        if (responseDbs[static_cast<size_t>(i)] <= kMinDb) {
            curveBroken = true;
        } else if (curveBroken) {
            curvePath.startNewSubPath(x, y);
            curveBroken = false;
        } else {
            curvePath.lineTo(x, y);
        }
    }
    fillPath.lineTo(plot.getRight(), zeroY);
    fillPath.closeSubPath();

    const auto accent =
        hasCurveColour_ ? curveColour_ : ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE);
    g.setColour(accent.withAlpha(0.13f + drive_ * 0.08f));
    g.fillPath(fillPath);
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(curvePath, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    const float cutoffX = plot.getX() + freqToX(cutoffHz_, plot.getWidth(), minPlotFrequencyHz_);
    g.setColour(accent.withAlpha(0.45f));
    g.drawVerticalLine(static_cast<int>(std::round(cutoffX)), plot.getY(), plot.getBottom());

    if (onParameterChanged_ == nullptr)
        return;

    // The handle rides the curve at the cutoff.
    const float handleY =
        juce::jlimit(plot.getY(), plot.getBottom(),
                     plot.getY() + dbToY(responseDbAt(cutoffHz_), plot.getHeight(), maxDb));
    const auto handle = juce::Rectangle<float>(12.0f, 12.0f).withCentre({cutoffX, handleY});
    g.setColour(dragging_ ? accent : ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillEllipse(handle);
    g.setColour(accent);
    g.drawEllipse(handle, 2.0f);

    const auto cutoffText = cutoffHz_ >= 1000.0f
                                ? juce::String(cutoffHz_ / 1000.0f, 1) + " kHz"
                                : juce::String(juce::roundToInt(cutoffHz_)) + " Hz";
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.setFont(fonts.getMonoFont(11.0f));
    g.drawText("CUTOFF " + cutoffText + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 RES ")) +
                   juce::String(resonance_ * 100.0f, 1) + "%",
               plot.withTrimmedTop(6.0f).withTrimmedRight(10.0f).withHeight(14.0f),
               juce::Justification::centredRight, false);
}

juce::Rectangle<float> CompiledFilterCurveView::plotBounds() const {
    return getLocalBounds().toFloat().reduced(kPlotPadX, kPlotPadY);
}

void CompiledFilterCurveView::setFromHandle(float x, float resonance) {
    using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
    const auto plot = plotBounds();
    const float cutoff = juce::jlimit(kMinCutoffHz, kMaxFreq,
                                      xToFreq(juce::jlimit(0.0f, plot.getWidth(), x - plot.getX()),
                                              plot.getWidth(), minPlotFrequencyHz_));
    resonance = juce::jlimit(0.0f, 1.0f, resonance);
    cutoffHz_ = targetCutoffHz_ = cutoff;
    resonance_ = targetResonance_ = resonance;
    onParameterChanged_(Filter::kCutoffSlot, cutoff);
    onParameterChanged_(Filter::kResonanceSlot, resonance);
    repaint();
}

void CompiledFilterCurveView::mouseDown(const juce::MouseEvent& e) {
    if (onParameterChanged_ == nullptr || !plotBounds().contains(e.position))
        return;
    dragging_ = true;
    dragStartResonance_ = resonance_;
    setFromHandle(e.position.x, resonance_);
}

void CompiledFilterCurveView::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    // Resonance follows the drag up and down, across the plot's height.
    const float rise = -static_cast<float>(e.getDistanceFromDragStartY()) /
                       juce::jmax(1.0f, plotBounds().getHeight());
    setFromHandle(e.position.x, dragStartResonance_ + rise);
}

void CompiledFilterCurveView::mouseUp(const juce::MouseEvent&) {
    dragging_ = false;
    repaint();
}

void CompiledFilterCurveView::mouseDoubleClick(const juce::MouseEvent&) {
    using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
    if (onParameterChanged_ == nullptr)
        return;
    for (const int slot : {Filter::kCutoffSlot, Filter::kResonanceSlot})
        if (const auto* param = deviceSnapshot_.findParameterByIndex(slot))
            onParameterChanged_(slot, param->defaultValue);
}

void CompiledFilterCurveView::mouseWheelMove(const juce::MouseEvent& e,
                                             const juce::MouseWheelDetails& wheel) {
    using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
    if (onParameterChanged_ == nullptr) {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }
    drive_ = targetDrive_ = juce::jlimit(0.0f, 1.0f, drive_ + wheel.deltaY * 0.25f);
    onParameterChanged_(Filter::kDriveSlot, drive_);
    repaint();
}

const CompiledPresentationSpec& getMagdaFilterPresentation() {
    using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
    // Cutoff and resonance keep knobs beside the handle, where modulation links them;
    // mode and engine sit in the header.
    static constexpr int kKnobSlots[] = {Filter::kCutoffSlot, Filter::kResonanceSlot,
                                         Filter::kDriveSlot, Filter::kLimitSlot};
    static constexpr int kHeaderSlots[] = {Filter::kModeSlot, Filter::kEngineSlot};
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaFilterCompiledPlugin::xmlTypeName,
        .layoutCellCount = 6,
        .layoutCellsPerRow = 6,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledFilterCurveView>(pluginId);
        },
        .knobSlots = kKnobSlots,
        .faceplateWidth = 460,
        .headerSlots = kHeaderSlots,
    };
    return kSpec;
}

void CompiledFilterCurveView::bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    setCompiledPlugin(
        std::dynamic_pointer_cast<magda::daw::audio::compiled::MagdaFilterCompiledPlugin>(
            std::move(device)));
}

}  // namespace magda::daw::ui
