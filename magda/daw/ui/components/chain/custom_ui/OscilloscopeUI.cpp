#include "OscilloscopeUI.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

#include "AnalyzerColours.hpp"
#include "AnalyzerWindow.hpp"
#include "core/Config.hpp"
#include "ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kControlRowH = 22;    // full-editor horizontal control row
constexpr int kStackRowH = 18;      // one row of the compact vertical control stack
constexpr int kStackLabelW = 34;    // label column width in the stacked layout
constexpr int kChevronStripH = 16;  // dedicated strip below the waveform for the chevron
}  // namespace

OscilloscopeUI::OscilloscopeUI() {
    window_.assign(static_cast<size_t>(kMaxWindow), 0.0f);

    timeLabel_.setText("Time", juce::dontSendNotification);
    timeLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    timeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    timeLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(timeLabel_);

    timeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    timeSlider_.setRange(1.0, 5000.0, 0.0);
    timeSlider_.setSkewFactorFromMidPoint(50.0);  // log-ish: low end usable across 1-5000 ms
    // Own value readout (themed font, clean formatting) instead of the slider's
    // built-in text box, which uses the default font and shows raw decimals.
    timeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    timeSlider_.setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    timeSlider_.setColour(juce::Slider::trackColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    timeSlider_.setColour(juce::Slider::thumbColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT));
    timeSlider_.onValueChange = [this] {
        updateTimeReadout();
        if (plugin_ != nullptr) {
            plugin_->setTimebaseMs(static_cast<float>(timeSlider_.getValue()));
            applyTimebase();
            repaint();
        }
    };
    // Persist as the global last-used default on release (not per drag tick).
    timeSlider_.onDragEnd = [this] {
        if (plugin_ == nullptr || !persistGlobalDefaults_)
            return;
        auto d = Config::getInstance().getOscilloscopeDefaults();
        d.timebaseMs = plugin_->getTimebaseMs();
        Config::getInstance().setOscilloscopeDefaults(d);
        Config::getInstance().save();
    };
    addAndMakeVisible(timeSlider_);

    timeValueLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    timeValueLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    timeValueLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(timeValueLabel_);
    updateTimeReadout();

    colourLabel_.setText("Color", juce::dontSendNotification);
    colourLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    colourLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    colourLabel_.setJustificationType(juce::Justification::centredRight);
    addChildComponent(colourLabel_);  // only shown in the stacked compact layout

    colourCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    colourCombo_.setColour(juce::ComboBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    colourCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    colourCombo_.setColour(juce::ComboBox::outlineColourId,
                           DarkTheme::getColour(DarkTheme::BORDER));
    for (int i = 0; i < kAnalyzerColourCount; ++i)
        colourCombo_.addItem(kAnalyzerColourNames[i], i + 1);
    colourCombo_.onChange = [this] {
        if (plugin_ == nullptr)
            return;
        plugin_->setTraceColourIndex(colourCombo_.getSelectedId() - 1);
    };
    addAndMakeVisible(colourCombo_);

    popoutButton_ = std::make_unique<magda::SvgButton>("Pop out", BinaryData::open_in_new_svg,
                                                       BinaryData::open_in_new_svgSize);
    daw::ui::node_header::applyHeaderIconStyle(*popoutButton_,
                                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    popoutButton_->onClick = [this] { openPopout(); };
    addChildComponent(*popoutButton_);  // shown only in compact mode

    updateTimerState();
}

OscilloscopeUI::~OscilloscopeUI() {
    stopTimer();
    colourCombo_.setLookAndFeel(nullptr);
}

void OscilloscopeUI::setPlugin(daw::audio::OscilloscopePlugin* plugin) {
    if (plugin_ == plugin)
        return;

    plugin_ = plugin;
    lastTapWritePosition_ = 0;
    if (popoutUI_ != nullptr)
        popoutUI_->setPlugin(plugin);  // keep the popped-out window live
    if (plugin_ == nullptr)
        return;
    timeSlider_.setValue(plugin_->getTimebaseMs(), juce::dontSendNotification);
    updateTimeReadout();
    applyTimebase();
    colourCombo_.setSelectedId(plugin_->getTraceColourIndex() + 1, juce::dontSendNotification);
}

void OscilloscopeUI::visibilityChanged() {
    updateTimerState();
}

void OscilloscopeUI::parentHierarchyChanged() {
    updateTimerState();
}

void OscilloscopeUI::setCompact(bool compact) {
    if (compact_ == compact)
        return;
    compact_ = compact;
    if (!compact_) {
        controlsExpanded_ = false;  // the full editor always shows controls; no toggle
        controlsFadeActive_ = false;
        controlsAlpha_ = 1.0f;
    }
    updateControlVisibility();
    resized();
    repaint();
}

void OscilloscopeUI::setPersistGlobalDefaults(bool persist) {
    persistGlobalDefaults_ = persist;
    if (popoutUI_ != nullptr)
        popoutUI_->setPersistGlobalDefaults(persist);
}

void OscilloscopeUI::setControlsExpanded(bool expanded) {
    if (!compact_ || controlsExpanded_ == expanded)
        return;
    controlsExpanded_ = expanded;
    startControlsFade(expanded);
    updateControlVisibility();
    resized();
    repaint();
    if (expanded && onControlsExpandedChanged)
        onControlsExpandedChanged();
}

int OscilloscopeUI::expandedControlsHeight() const {
    if (!compact_ || !showControls())
        return 0;
    constexpr int fullHeight = 2 * kStackRowH + 4;  // Time + Color rows, plus top padding
    // Reserve the full height for the whole fade (both directions) so the strip
    // relayouts once on open and once on close, not every frame. The controls
    // cross-fade their opacity over this stable area; animating the height each
    // frame (and relaying out the whole mixer with it) is what made the reveal
    // flicker.
    return (controlsExpanded_ || controlsFadeActive_) ? fullHeight : 0;
}

void OscilloscopeUI::updateControlVisibility() {
    const bool full = !compact_;
    const bool stacked = compact_ && showControls();
    timeLabel_.setVisible(full || stacked);
    timeSlider_.setVisible(full || stacked);
    timeValueLabel_.setVisible(full);  // numeric readout only fits the full editor
    colourCombo_.setVisible(full || stacked);
    colourLabel_.setVisible(stacked);  // the stacked layout labels the colour combo
    if (popoutButton_)
        popoutButton_->setVisible(compact_);  // lives in the strip, compact only
    applyControlsAlpha();
}

void OscilloscopeUI::startControlsFade(bool expanding) {
    controlsFadeActive_ = true;
    controlsFadeStartMs_ = juce::Time::getMillisecondCounterHiRes();
    controlsFadeStartAlpha_ = expanding ? 0.0f : controlsAlpha_;
    controlsFadeTargetAlpha_ = expanding ? 1.0f : 0.0f;
    controlsAlpha_ = controlsFadeStartAlpha_;
}

void OscilloscopeUI::advanceControlsFade() {
    if (!controlsFadeActive_)
        return;

    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - controlsFadeStartMs_;
    const auto progress =
        static_cast<float>(juce::jlimit(0.0, 1.0, elapsed / kCompactControlsFadeMs));
    controlsAlpha_ =
        controlsFadeStartAlpha_ + (controlsFadeTargetAlpha_ - controlsFadeStartAlpha_) * progress;
    // Opacity-only during the fade — height is already reserved, so no per-frame
    // relayout (that was the flicker). The strip relayouts once on completion.
    applyControlsAlpha();
    repaint();

    if (progress < 1.0f)
        return;

    controlsFadeActive_ = false;
    controlsAlpha_ = controlsExpanded_ ? 1.0f : 0.0f;
    updateControlVisibility();
    if (onControlsExpandedChanged)
        onControlsExpandedChanged();
    resized();
    repaint();
}

void OscilloscopeUI::applyControlsAlpha() {
    const float alpha = compact_ ? controlsAlpha_ : 1.0f;
    timeLabel_.setAlpha(alpha);
    timeSlider_.setAlpha(alpha);
    colourLabel_.setAlpha(alpha);
    colourCombo_.setAlpha(alpha);
}

void OscilloscopeUI::updateTimeReadout() {
    const double ms = timeSlider_.getValue();
    const juce::String text = ms >= 1000.0 ? juce::String(ms / 1000.0, 2) + " s"
                                           : juce::String(juce::roundToInt(ms)) + " ms";
    timeValueLabel_.setText(text, juce::dontSendNotification);
}

void OscilloscopeUI::applyTimebase() {
    const double sr = (plugin_ != nullptr) ? plugin_->getSampleRate() : 44100.0;
    const float ms = (plugin_ != nullptr) ? plugin_->getTimebaseMs() : 10.0f;
    const int samples = static_cast<int>(static_cast<double>(ms) * 0.001 * sr);
    displaySamples_ = juce::jlimit(64, kMaxWindow - kTriggerSearch, samples);
    readCount_ = juce::jmin(displaySamples_ + kTriggerSearch, kMaxWindow);
}

int OscilloscopeUI::compactExtraHeight() const {
    return compact_ ? kChevronStripH + expandedControlsHeight() : 0;
}

void OscilloscopeUI::resized() {
    if (compact_) {
        auto area = getLocalBounds();
        // Stacked controls (when expanded) sit at the very bottom.
        const int controlsHeight = expandedControlsHeight();
        auto controls =
            controlsHeight > 0 ? area.removeFromBottom(controlsHeight) : juce::Rectangle<int>();
        // Dedicated chevron/pop-out strip directly below the waveform.
        auto strip = area.removeFromBottom(kChevronStripH);
        chevronRect_ = juce::Rectangle<int>(strip.getCentreX() - 7, strip.getCentreY() - 7, 14, 14);
        popoutRect_ = juce::Rectangle<int>(strip.getRight() - 19, strip.getCentreY() - 7, 14, 14);
        if (popoutButton_)
            popoutButton_->setBounds(popoutRect_);
        if (controlsHeight > 0 && showControls()) {
            controls.removeFromTop(2);
            auto timeRow = controls.removeFromTop(kStackRowH);
            timeLabel_.setBounds(timeRow.removeFromLeft(kStackLabelW));
            timeSlider_.setBounds(timeRow.reduced(4, 2));
            auto colourRow = controls.removeFromTop(kStackRowH);
            colourLabel_.setBounds(colourRow.removeFromLeft(kStackLabelW));
            colourCombo_.setBounds(colourRow.reduced(2, 1));
        }
        return;
    }

    chevronRect_ = juce::Rectangle<int>();
    popoutRect_ = juce::Rectangle<int>();
    auto controls = getLocalBounds().removeFromBottom(kControlRowH);
    timeLabel_.setBounds(controls.removeFromLeft(40));
    colourCombo_.setBounds(controls.removeFromRight(72).reduced(2, 2));
    timeValueLabel_.setBounds(controls.removeFromRight(54));
    timeSlider_.setBounds(controls.reduced(4, 2));
}

void OscilloscopeUI::mouseDown(const juce::MouseEvent& e) {
    if (compact_ && chevronRect_.contains(e.getPosition()))
        setControlsExpanded(!controlsExpanded_);  // pop-out is handled by popoutButton_
}

void OscilloscopeUI::openPopout() {
    // popoutButton_ is a toggle: its state after the click is the desired open
    // state. This keeps the icon and the window in sync — clicking while open
    // hides it, and closing via the window X clears the toggle (onClose below).
    const bool wantOpen = (popoutButton_ == nullptr) || popoutButton_->getToggleState();

    if (popoutWindow_ == nullptr) {
        if (!wantOpen)
            return;
        auto content = std::make_unique<OscilloscopeUI>();  // full-size (not compact)
        popoutUI_ = content.get();
        popoutUI_->setPersistGlobalDefaults(persistGlobalDefaults_);
        popoutUI_->setPlugin(plugin_);
        popoutWindow_ = std::make_unique<AnalyzerWindow>("Oscilloscope", std::move(content));
        popoutWindow_->onClose = [this]() {
            if (popoutButton_)
                popoutButton_->setToggleState(false, juce::dontSendNotification);
        };
    } else {
        popoutWindow_->setVisible(wantOpen);
        if (wantOpen)
            popoutWindow_->toFront(true);
    }
}

void OscilloscopeUI::timerCallback() {
    const bool fadeWasActive = controlsFadeActive_;
    advanceControlsFade();
    bool needsRepaint = fadeWasActive || controlsFadeActive_;

    if (!isShowing())
        return;

    if (plugin_ != nullptr) {
        const auto writePosition = plugin_->getTapBuffer().writePosition();
        if (writePosition != lastTapWritePosition_) {
            lastTapWritePosition_ = plugin_->getTapBuffer().readLatest(window_.data(), readCount_);
            needsRepaint = true;
        }
    }

    if (needsRepaint)
        repaint();
}

void OscilloscopeUI::updateTimerState() {
    if (isVisible() && getParentComponent() != nullptr)
        startTimerHz(kTimerHz);
    else
        stopTimer();
}

void OscilloscopeUI::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    if (!compact_) {
        bounds.removeFromBottom(kControlRowH);
    } else {
        bounds.removeFromBottom(expandedControlsHeight());
        bounds.removeFromBottom(kChevronStripH);  // reserve the chevron/pop-out strip
    }
    auto area = bounds.toFloat().reduced(4.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRoundedRectangle(area, 4.0f);

    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;

    // dBFS amplitude reference lines (symmetric about the centre) + labels on the
    // left. The trace stays linear; these just mark levels (0 dBFS = full scale).
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    for (float dbfs : {0.0f, -6.0f, -12.0f, -18.0f}) {
        const float amp = std::pow(10.0f, dbfs / 20.0f) * 0.9f;  // 0.9 = trace headroom
        const float yTop = midY - amp * halfH;
        const float yBot = midY + amp * halfH;
        g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE));
        g.drawHorizontalLine(static_cast<int>(yTop), area.getX(), area.getRight());
        g.drawHorizontalLine(static_cast<int>(yBot), area.getX(), area.getRight());
        if (!compact_) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
            g.drawText(juce::String(static_cast<int>(dbfs)),
                       juce::Rectangle<float>(area.getX() + 2.0f, yTop - 6.0f, 28.0f, 12.0f),
                       juce::Justification::centredLeft);
        }
    }
    // Centre line (0 reference / silence).
    g.setColour(DarkTheme::getColour(DarkTheme::GRID_LINE));
    g.drawHorizontalLine(static_cast<int>(midY), area.getX(), area.getRight());

    if (static_cast<int>(window_.size()) < readCount_ || displaySamples_ < 2)
        return;

    // Rising zero-crossing trigger, searched in the head of the read so a full
    // displaySamples_ span is available after it.
    int trigger = 0;
    const int searchEnd = readCount_ - displaySamples_;
    for (int i = 1; i <= searchEnd; ++i) {
        if (window_[static_cast<size_t>(i - 1)] < 0.0f && window_[static_cast<size_t>(i)] >= 0.0f) {
            trigger = i;
            break;
        }
    }

    g.setColour(analyzerTraceColour(plugin_ != nullptr ? plugin_->getTraceColourIndex() : 0));

    const float w = area.getWidth();
    const int cols = juce::jmax(1, static_cast<int>(w));
    auto yOf = [&](float s) { return midY - juce::jlimit(-1.0f, 1.0f, s) * halfH * 0.9f; };

    if (displaySamples_ <= cols) {
        // Zoomed in (<= 1 sample per pixel): smooth interpolated line.
        juce::Path path;
        for (int i = 0; i < displaySamples_; ++i) {
            const float x =
                area.getX() + w * (static_cast<float>(i) / static_cast<float>(displaySamples_ - 1));
            const float y = yOf(window_[static_cast<size_t>(trigger + i)]);
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.strokePath(path, juce::PathStrokeType(1.5f));
    } else {
        // Zoomed out (many samples per pixel): draw a min/max envelope per column.
        // This stays stable frame to frame instead of crawling/aliasing the way a
        // per-sample line does when far more samples than pixels are drawn.
        for (int x = 0; x < cols; ++x) {
            const long long i0 = trigger + static_cast<long long>(x) * displaySamples_ / cols;
            long long i1 = trigger + static_cast<long long>(x + 1) * displaySamples_ / cols;
            if (i1 <= i0)
                i1 = i0 + 1;
            float mn = 1.0f;
            float mx = -1.0f;
            for (long long i = i0; i < i1; ++i) {
                const float s = window_[static_cast<size_t>(i)];
                mn = std::min(mn, s);
                mx = std::max(mx, s);
            }
            const float xPos = area.getX() + static_cast<float>(x);
            float yTop = yOf(mx);
            float yBot = yOf(mn);
            if (yBot - yTop < 1.0f)
                yBot = yTop + 1.0f;
            g.drawLine(xPos, yTop, xPos, yBot, 1.0f);
        }
    }

    // Chevron/pop-out strip directly below the waveform.
    if (compact_) {
        auto strip = getLocalBounds();
        strip.removeFromBottom(expandedControlsHeight());
        strip = strip.removeFromBottom(kChevronStripH);
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(strip);
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.drawHorizontalLine(strip.getY(), static_cast<float>(strip.getX()),
                             static_cast<float>(strip.getRight()));
        // Chevron points down to open (controls below) and up to collapse.
        drawAnalyzerExpandChevron(g, chevronRect_, controlsExpanded_,
                                  DarkTheme::getColour(DarkTheme::TEXT_DIM));
        // Pop-out is the SvgButton (open_in_new) positioned in the strip.
    }
}

}  // namespace magda::daw::ui
