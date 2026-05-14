#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ValueLabelControl.hpp"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/ParameterInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief A text-based slider that displays value as editable text
 *
 * Drag to change value, double-click to reset, Shift+double-click or Shift+right-click to edit
 * text.
 * Supports dB and pan formatting.
 */
class TextSlider : public juce::Component, public magda::AutomationManagerListener {
  public:
    enum class Format { Decimal, Decibels, Pan };
    enum class Orientation { Horizontal, Vertical };

    TextSlider(Format format = Format::Decimal) : format_(format) {
        valueControl_.setFont(FontManager::getInstance().getUIFont(12.0f));
        valueControl_.setTextColour(DarkTheme::getTextColour());
        valueControl_.setJustification(juce::Justification::centred);
        valueControl_.onMouseDown = [this](const juce::MouseEvent& e) { mouseDown(e); };
        valueControl_.onMouseDrag = [this](const juce::MouseEvent& e) { mouseDrag(e); };
        valueControl_.onMouseUp = [this](const juce::MouseEvent& e) { mouseUp(e); };
        valueControl_.onMouseDoubleClick = [this](const juce::MouseEvent& e) {
            mouseDoubleClick(e);
        };
        valueControl_.onEditCommit = [this](const juce::String& text) { commitText(text); };
        addAndMakeVisible(valueControl_);

        updateLabel();
    }

    ~TextSlider() override {
        if (listeningToAutomation_) {
            magda::AutomationManager::getInstance().removeListener(this);
            listeningToAutomation_ = false;
        }
    }

    void setRange(double min, double max, double interval = 0.01) {
        minValue_ = min;
        maxValue_ = max;
        interval_ = interval;
        defaultValue_ = juce::jlimit(minValue_, maxValue_, defaultValue_);
        value_ = juce::jlimit(minValue_, maxValue_, value_);
        updateLabel();
    }

    /** Set a skew factor for logarithmic-feel drag behaviour.
        A centre value (e.g. 1000.0 for a 10–22000 Hz range) will be
        reached at the midpoint of the drag. This only affects drag
        sensitivity, not the stored value or display. */
    void setSkewForCentre(double centreValue) {
        if (maxValue_ > minValue_ && centreValue > minValue_ && centreValue < maxValue_) {
            skewFactor_ =
                std::log(0.5) / std::log((centreValue - minValue_) / (maxValue_ - minValue_));
        }
    }

    /**
     * Single-call configuration from ParameterInfo. This is the path consumers
     * should use — range/skew/formatter/parser are all derived from the
     * parameter's metadata so the slider, automation lane, playback engine,
     * and UI echo all compute values via the same ParameterUtils helpers.
     *
     * Sets: range, skew (from scaleAnchor if set), valueFormatter and
     * valueParser (delegating to ParameterUtils::formatValue/parseValue).
     */
    void setParameterInfo(const magda::ParameterInfo& info) {
        double interval = 0.01;
        if (info.displayFormat == magda::DisplayFormat::Percent && info.minValue >= -1.0e-6f &&
            info.maxValue <= 1.0f + 1.0e-6f) {
            interval = 0.001;
        }
        setRange(static_cast<double>(info.minValue), static_cast<double>(info.maxValue), interval);
        setDefaultValue(static_cast<double>(info.defaultValue));

        // Map scaleAnchor into TextSlider's drag-skew so the slider
        // matches ParameterUtils' anchor handling. The actual
        // projection (linear vs log) is selected by `useLogProjection_`
        // below — without it, scale=Logarithmic + scaleAnchor would
        // compute a log anchor-ratio and then project linearly, which
        // lands the anchor at the wrong pixel position.
        useLogProjection_ =
            (info.scale == magda::ParameterScale::Logarithmic && info.minValue > 0.0f);
        skewFactor_ = 1.0;
        if (info.scaleAnchor > info.minValue && info.scaleAnchor < info.maxValue &&
            info.maxValue > info.minValue) {
            // The skew is solved so that pow(0.5, 1/skew) = anchorRatio,
            // i.e. the slider's pixel midpoint maps to the anchor's
            // position in whichever projected space (linear or log) the
            // drag handler then uses.
            const double anchorRatio =
                useLogProjection_
                    ? (std::log(static_cast<double>(info.scaleAnchor) / info.minValue) /
                       std::log(static_cast<double>(info.maxValue) / info.minValue))
                    : (info.scaleAnchor - info.minValue) / (info.maxValue - info.minValue);
            if (anchorRatio > 0.0 && anchorRatio < 1.0)
                skewFactor_ = std::log(0.5) / std::log(anchorRatio);
        }

        paramInfoCopy_ = info;
        hasParamInfo_ = true;

        // ParameterInfo is the value-space contract for this slider. Reset
        // any previous parser installed for another parameter so a reused
        // ParamSlot cannot keep a stale normalized/display assumption after
        // pagination or chain rebuilds. The formatter is preserved if the
        // host called setValueFormatter() — see hasExplicitFormatter_.
        if (!hasExplicitFormatter_) {
            valueFormatter_ = [this](double real) {
                return magda::ParameterUtils::formatValue(static_cast<float>(real), paramInfoCopy_);
            };
        }
        valueParser_ = [this](const juce::String& text) {
            auto parsed = magda::ParameterUtils::parseValue(text, paramInfoCopy_);
            return parsed.has_value() ? static_cast<double>(*parsed) : value_;  // keep on failure
        };

        updateLabel();
    }

    void setValue(double newValue, juce::NotificationType notification = juce::sendNotification) {
        setValueWithInterval(newValue, interval_, notification);
    }

    /** Like setValue() but snaps to a caller-supplied interval instead of the
     *  configured `interval_`. Used by the drag path to honour fine-tune
     *  modifiers (Shift/Cmd) — the default interval is coarser than cent-level
     *  for most plugin parameters. Pass interval <= 0 to skip quantization. */
    void setValueWithInterval(double newValue, double interval,
                              juce::NotificationType notification = juce::sendNotification) {
        newValue = juce::jlimit(minValue_, maxValue_, newValue);
        if (interval > 0) {
            newValue = minValue_ + interval * std::round((newValue - minValue_) / interval);
        }

        if (!hasExplicitDefaultValue_ && !hasCapturedDefaultValue_ &&
            notification == juce::dontSendNotification) {
            defaultValue_ = newValue;
            hasCapturedDefaultValue_ = true;
        }

        if (std::abs(value_ - newValue) > 1.0e-9) {
            value_ = newValue;
            updateLabel();
            if (notification != juce::dontSendNotification && onValueChanged) {
                onValueChanged(value_);
            }
        }
    }

    double getValue() const {
        return value_;
    }

    void setDefaultValue(double defaultValue) {
        defaultValue_ = juce::jlimit(minValue_, maxValue_, defaultValue);
        hasExplicitDefaultValue_ = true;
        hasCapturedDefaultValue_ = true;
    }

    void setFormat(Format format) {
        format_ = format;
        updateLabel();
    }

    void setFont(const juce::Font& font) {
        valueControl_.setFont(font);
    }

    void setTextColour(const juce::Colour& colour) {
        valueControl_.setTextColour(colour);
    }

    void setBackgroundColour(const juce::Colour& colour) {
        juce::ignoreUnused(colour);
        valueControl_.clearBackgroundColour();
    }

    void setRightClickEditsText(bool shouldEdit) {
        rightClickEditsText_ = shouldEdit;
    }

    void setShowFillIndicator(bool show) {
        showFillIndicator_ = show;
        updateLabel();
    }

    void setEmptyText(const juce::String& text) {
        emptyText_ = text;
        updateLabel();
    }

    void setShowEmptyText(bool show) {
        showEmptyText_ = show;
        updateLabel();
    }

    // Custom value formatter — takes the slider's real value, returns
    // display string. Sticky against setParameterInfo() so custom UIs
    // (e.g. FourOscUI's "L50"/"R50" pan label) survive the refresh cycle
    // DeviceSlotComponent runs whenever the device's ParameterInfo
    // republishes.
    void setValueFormatter(std::function<juce::String(double)> formatter) {
        valueFormatter_ = std::move(formatter);
        hasExplicitFormatter_ = static_cast<bool>(valueFormatter_);
        updateLabel();
    }

    // Custom value parser - takes user input string, returns the slider's real value.
    void setValueParser(std::function<double(const juce::String&)> parser) {
        valueParser_ = std::move(parser);
    }

    void setOrientation(Orientation o) {
        orientation_ = o;
        updateLabel();
    }

    Orientation getOrientation() const {
        return orientation_;
    }

    void setShiftDragStartValue(float value) {
        shiftDragStartValue_ = value;
    }

    /** Set peak meter levels to display behind the text (0.0 = silence, 1.0 = 0dB) */
    void setMeterLevels(float peakL, float peakR) {
        if (std::abs(meterPeakL_ - peakL) > 0.001f || std::abs(meterPeakR_ - peakR) > 0.001f) {
            meterPeakL_ = peakL;
            meterPeakR_ = peakR;
            updateLabel();
            repaint();
        }
    }

    bool isBeingDragged() const {
        return isLeftButtonDrag_;
    }

    void cancelGesture() {
        const bool wasLeftDrag = isLeftButtonDrag_;
        isLeftButtonDrag_ = false;
        isShiftDrag_ = false;
        hasDragged_ = false;
        overrideLatchedThisGesture_ = false;
        valueControl_.setDragging(false);
        if (wasLeftDrag && hasAutomationTarget_) {
            auto& mgr = magda::AutomationManager::getInstance();
            mgr.setTargetUserTouched(automationTarget_, false);
            mgr.setTargetTouchSuppressed(automationTarget_, false);
            mgr.clearTouchBaseline(automationTarget_);
        }
    }

    // Bind this slider to an automation target so mouseDown/mouseUp automatically
    // pause the lane's baking for the duration of a gesture (kills fader-vs-curve
    // fighting during playback) and so we can paint the "automated" state.
    void setAutomationTarget(const magda::AutomationTarget& target) {
        automationTarget_ = target;
        const bool nowHas = target.isValid();
        if (nowHas && !listeningToAutomation_) {
            magda::AutomationManager::getInstance().addListener(this);
            listeningToAutomation_ = true;
        } else if (!nowHas && listeningToAutomation_) {
            magda::AutomationManager::getInstance().removeListener(this);
            listeningToAutomation_ = false;
        }
        hasAutomationTarget_ = nowHas;
        refreshAutomationVisualState();
    }
    void clearAutomationTarget() {
        setAutomationTarget({});
    }
    magda::AutomationVisualState automationVisualState() const {
        return automationVisualState_;
    }
    bool isAutomated() const {
        return automationVisualState_ != magda::AutomationVisualState::None;
    }

    // AutomationManagerListener
    void automationLanesChanged() override {
        refreshAutomationVisualState();
    }
    void automationLanePropertyChanged(magda::AutomationLaneId /*laneId*/) override {
        refreshAutomationVisualState();
    }

    double getNormalizedValue() const {
        if (maxValue_ <= minValue_)
            return 0.0;
        return (value_ - minValue_) / (maxValue_ - minValue_);
    }

    std::function<void(double)> onValueChanged;
    std::function<void()> onDragEnd;       // Called when a drag gesture ends (mouseUp after drag)
    std::function<void()> onClicked;       // Called on single left-click (no drag)
    std::function<void()> onShiftClicked;  // Called on Shift+click (no drag)
    std::function<void(float)>
        onShiftDragStart;  // Called when Shift+drag starts, param is start value (0-1)
    std::function<void(float)> onShiftDrag;  // Called during Shift+drag with new value (0-1)
    // Predicate consulted on mouseDown with Shift held. If provided and returns
    // false, the Shift+drag path is skipped and the gesture runs through the
    // normal drag (which still honours Shift as a fine-tune modifier).
    // Callers (e.g. ParamSlotComponent) use this to keep Shift+drag reserved
    // for mod-amount editing only when a mod is actually selected.
    std::function<bool()> canStartShiftDrag;
    std::function<void()> onShiftDragEnd;  // Called when Shift+drag ends
    std::function<void()>
        onRightClicked;  // Called on right-click (when rightClickEditsText_ is false)

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        if (orientation_ == Orientation::Vertical) {
            // Vertical fader mode: fill from bottom to current position + handle
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.fillRect(bounds);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(bounds);

            // Fill from bottom up to current value position
            float norm = static_cast<float>(getNormalizedValue());
            int fillHeight = static_cast<int>(bounds.getHeight() * norm);
            auto fillRect = bounds.withTop(bounds.getBottom() - fillHeight);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
            g.fillRect(fillRect);

            // Draw handle at current position
            int handleY = bounds.getBottom() - fillHeight;
            const int handleH = 6;
            auto handleRect = juce::Rectangle<int>(bounds.getX() + 1, handleY - handleH / 2,
                                                   bounds.getWidth() - 2, handleH);
            g.setColour(juce::Colour(0xFF888888));
            g.fillRect(handleRect);
            // Center line on handle
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.drawHorizontalLine(handleY, static_cast<float>(handleRect.getX() + 2),
                                 static_cast<float>(handleRect.getRight() - 2));
        } else if (meterPeakL_ > 0.001f || meterPeakR_ > 0.001f) {
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.fillRoundedRectangle(bounds.toFloat(), 2.0f);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.0f, 1.0f);

            if (meterPeakL_ > 0.001f || meterPeakR_ > 0.001f) {
                float w = static_cast<float>(bounds.getWidth());

                auto gainToWidth = [w](float gain) -> float {
                    float db = juce::Decibels::gainToDecibels(gain, -60.0f);
                    float norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
                    return w * std::pow(norm, 3.0f);
                };

                constexpr float zeroDbNorm = (60.0f / 66.0f) * (60.0f / 66.0f) * (60.0f / 66.0f);

                auto drawHorizontalBar = [&](int y, int barH, float gain) {
                    int barW = static_cast<int>(gainToWidth(gain));
                    if (barW < 1)
                        return;

                    auto barArea = juce::Rectangle<int>(bounds.getX(), y, barW, barH);
                    float barNorm = static_cast<float>(barW) / w;

                    if (barNorm <= zeroDbNorm * 0.7f) {
                        g.setColour(juce::Colour(0xff4CAF50).withAlpha(0.5f));
                        g.fillRect(barArea);
                    } else {
                        juce::ColourGradient gradient(
                            juce::Colour(0xff4CAF50).withAlpha(0.5f), 0.0f, 0.0f,
                            juce::Colour(0xffF44336).withAlpha(0.5f), w, 0.0f, false);
                        gradient.addColour(zeroDbNorm, juce::Colour(0xffFFC107).withAlpha(0.5f));
                        g.setGradientFill(gradient);
                        g.fillRect(barArea);
                    }
                };

                drawHorizontalBar(bounds.getY(), bounds.getHeight() / 2, meterPeakL_);
                drawHorizontalBar(bounds.getY() + bounds.getHeight() / 2, bounds.getHeight() / 2,
                                  meterPeakR_);
            }
        }

        if ((orientation_ == Orientation::Vertical || meterPeakL_ > 0.001f ||
             meterPeakR_ > 0.001f) &&
            automationVisualState_ != magda::AutomationVisualState::None) {
            auto boundsF = getLocalBounds().toFloat();
            const juce::Colour tint =
                automationVisualState_ == magda::AutomationVisualState::Overridden
                    ? juce::Colour(DarkTheme::TEXT_DISABLED)
                    : juce::Colour(DarkTheme::ACCENT_PURPLE);
            g.setColour(tint.withAlpha(0.18f));
            g.fillRect(boundsF);
            g.setColour(tint);
            g.drawRect(boundsF, 1.5f);
        }
    }

    void resized() override {
        valueControl_.setBounds(getLocalBounds());
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!valueControl_.isEditing() && e.mods.isLeftButtonDown()) {
            dragStartValue_ = value_;
            dragStartY_ = e.y;
            dragStartX_ = e.x;
            hasDragged_ = false;
            overrideLatchedThisGesture_ = false;
            isLeftButtonDrag_ = true;
            valueControl_.setDragging(true);
            // Only enter Shift+drag mode if the owner actually wants to take
            // the gesture (e.g. a mod is currently selected for amount edit).
            // Otherwise Shift falls through to the normal drag path so its
            // fine-tune behaviour still applies.
            bool ownerTakesShiftDrag = e.mods.isShiftDown() && onShiftDragStart &&
                                       (!canStartShiftDrag || canStartShiftDrag());
            isShiftDrag_ = ownerTakesShiftDrag;

            if (isShiftDrag_) {
                shiftDragStartValue_ = 0.5f;  // Default start value for new links
                onShiftDragStart(shiftDragStartValue_);
            }

            // Transient touch-suppression so playback doesn't fight the
            // gesture. The persistent override/bypass only latches once the
            // drag is confirmed as a real edit (see latchAutomationOverride).
            if (hasAutomationTarget_ && isAutomated()) {
                magda::AutomationManager::getInstance().setTargetTouchSuppressed(automationTarget_,
                                                                                 true);
            }
            // Mark the user gesture even when no lane exists yet, so the
            // recording engine can distinguish real touches from playback
            // engine echo-backs.
            if (hasAutomationTarget_) {
                magda::AutomationManager::getInstance().setTargetUserTouched(automationTarget_,
                                                                             true);
                // Capture the pre-drag value as a Touch-mode bounce-back
                // baseline. dragStartValue_ is the slider's value before any
                // motion, in the slider's display units; convert to the
                // normalized 0..1 form the lane stores via the target's
                // ParameterInfo. Track volume / pan have their own engine-
                // internal baseline path and don't strictly need this, but
                // it's harmless — the engine prefers its own when both exist.
                magda::ParameterInfo info = magda::getParameterInfoForTarget(automationTarget_);
                double normalized = static_cast<double>(magda::ParameterUtils::realToNormalized(
                    static_cast<float>(dragStartValue_), info));
                magda::AutomationManager::getInstance().setTouchBaseline(automationTarget_,
                                                                         normalized);
            }
        } else {
            isLeftButtonDrag_ = false;
            isShiftDrag_ = false;
            valueControl_.setDragging(false);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (valueControl_.isEditing() || !isLeftButtonDrag_)
            return;

        // Check if we've moved enough to count as a drag
        int dx = std::abs(e.x - dragStartX_);
        int dy = std::abs(e.y - dragStartY_);
        if (dx > 3 || dy > 3) {
            hasDragged_ = true;
            // Drag confirmed — latch persistent override on first real motion.
            // Skipped when write mode is on: the gesture is being recorded
            // into the lane, not taking over from it.
            if (!isShiftDrag_ && !overrideLatchedThisGesture_ && hasAutomationTarget_ &&
                isAutomated() && !magda::AutomationManager::getInstance().isWriteModeEnabled()) {
                magda::AutomationManager::getInstance().setTargetOverridden(automationTarget_,
                                                                            true);
                setAutomationVisualState(magda::AutomationVisualState::Overridden);
                overrideLatchedThisGesture_ = true;
            }
        }

        if (hasDragged_) {
            if (isShiftDrag_ && onShiftDrag) {
                // Shift+drag: call the callback with normalized value (0-1)
                // Used for macro/modulation linking
                float dragSensitivity = 1.0f / 100.0f;  // 100 pixels for full range
                float delta = static_cast<float>(dragStartY_ - e.y) * dragSensitivity;
                float newValue = juce::jlimit(-1.0f, 1.0f, shiftDragStartValue_ + delta);
                onShiftDrag(newValue);
            } else {
                // Normal drag: change the slider value with modifier-based sensitivity
                // Vertical: component height = full range (fader tracks mouse 1:1)
                // Horizontal: 200 pixels = full range
                // Shift: 10x finer, Ctrl/Cmd: 100x finer (both pixel range AND
                // snap interval, so fine-tune actually lands on finer values —
                // the default interval is coarser than cent-level for most VST
                // parameters, so without scaling it here Cmd-drag only slowed
                // the mouse without changing the reachable value grid).
                double pixelRange = (orientation_ == Orientation::Vertical)
                                        ? static_cast<double>(getHeight())
                                        : 200.0;
                double effectiveInterval = interval_;

                if (e.mods.isShiftDown()) {
                    pixelRange *= 10.0;  // Fine control
                    effectiveInterval *= 0.1;
                } else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
                    pixelRange *= 100.0;  // Very fine control
                    effectiveInterval *= 0.01;
                }

                double pixelDelta;
                if (orientation_ == Orientation::Horizontal) {
                    pixelDelta = e.x - dragStartX_;
                } else {
                    pixelDelta = dragStartY_ - e.y;
                }

                double newValue;
                if (useLogProjection_) {
                    // Log slider: drag operates in log-normalised space
                    // [0,1] = log(val/min) / log(max/min). Equal pixel
                    // movements give equal RATIO changes on the value;
                    // an optional skew keeps an anchor at the midpoint.
                    const double logRange = std::log(maxValue_ / minValue_);
                    const double startNorm = std::log(dragStartValue_ / minValue_) / logRange;
                    if (skewFactor_ != 1.0) {
                        const double startSkewed = std::pow(startNorm, skewFactor_);
                        const double skewedNorm =
                            juce::jlimit(0.0, 1.0, startSkewed + pixelDelta / pixelRange);
                        const double unskewed = std::pow(skewedNorm, 1.0 / skewFactor_);
                        newValue = minValue_ * std::exp(unskewed * logRange);
                    } else {
                        const double newNorm =
                            juce::jlimit(0.0, 1.0, startNorm + pixelDelta / pixelRange);
                        newValue = minValue_ * std::exp(newNorm * logRange);
                    }
                } else if (skewFactor_ != 1.0) {
                    // Skewed drag: work in normalised (0-1) space with skew applied
                    double startNorm = (dragStartValue_ - minValue_) / (maxValue_ - minValue_);
                    double startSkewed = std::pow(startNorm, skewFactor_);
                    double skewedNorm =
                        juce::jlimit(0.0, 1.0, startSkewed + pixelDelta / pixelRange);
                    double unskewed = std::pow(skewedNorm, 1.0 / skewFactor_);
                    newValue = minValue_ + unskewed * (maxValue_ - minValue_);
                } else {
                    double sensitivity = (maxValue_ - minValue_) / pixelRange;
                    newValue = dragStartValue_ + pixelDelta * sensitivity;
                }
                setValueWithInterval(newValue, effectiveInterval);
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        // Capture and clear the drag flag up front. mouseUp has several early-
        // return paths (shift-drag end, click, etc.); previously isLeftButtonDrag_
        // was never reset, which left isBeingDragged() permanently true after
        // the first drag and made every consumer that gates value updates on
        // !isBeingDragged() — e.g. mixer fader sync from trackPropertyChanged
        // — stop refreshing. (#1108)
        const bool wasLeftDrag = isLeftButtonDrag_;
        isLeftButtonDrag_ = false;
        valueControl_.setDragging(false);

        // Release the transient flags; the lane stays in bypass (override)
        // state until the user explicitly re-enables it from the header.
        if (wasLeftDrag && hasAutomationTarget_) {
            auto& mgr = magda::AutomationManager::getInstance();
            mgr.setTargetUserTouched(automationTarget_, false);
            mgr.setTargetTouchSuppressed(automationTarget_, false);
            // Recording engine consumes the baseline on the release
            // transition; clear it here so a stale value doesn't leak into
            // the next gesture if the engine wasn't recording.
            mgr.clearTouchBaseline(automationTarget_);
        }

        // Handle Shift+drag end
        if (isShiftDrag_) {
            if (hasDragged_ && onShiftDragEnd) {
                onShiftDragEnd();
            } else if (!hasDragged_ && onShiftClicked) {
                // Shift+click (no drag)
                onShiftClicked();
            }
            hasDragged_ = false;
            isShiftDrag_ = false;
            return;
        }

        if (!hasDragged_) {
            if (e.mods.isPopupMenu()) {
                if (rightClickEditsText_ || e.mods.isShiftDown()) {
                    // Right-click to edit text directly
                    valueControl_.showEditor(currentDisplayText());
                } else if (onRightClicked) {
                    // Right-click callback (for context menus, etc.)
                    onRightClicked();
                }
            } else if (onClicked) {
                // Single left-click callback
                onClicked();
            }
        } else if (onDragEnd) {
            onDragEnd();
        }
        hasDragged_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        cancelGesture();

        if (e.mods.isShiftDown())
            valueControl_.showEditor(currentDisplayText());
        else
            resetToDefaultValue();
    }

  private:
    void commitText(const juce::String& committedText) {
        auto text = committedText.trim();

        // Use custom parser if provided
        if (valueParser_) {
            setValueFromUser(valueParser_(text));
            return;
        }

        // Default parsing - remove common suffixes
        if (text.endsWithIgnoreCase("db")) {
            text = text.dropLastCharacters(2).trim();
        } else if (text.endsWithIgnoreCase("l") || text.endsWithIgnoreCase("r")) {
            text = text.dropLastCharacters(1).trim();
        } else if (text.equalsIgnoreCase("c") || text.equalsIgnoreCase("center")) {
            setValueFromUser(0.0);
            return;
        }

        setValueFromUser(text.getDoubleValue());
    }

    void setValueFromUser(double newValue) {
        if (std::abs(value_ - juce::jlimit(minValue_, maxValue_, newValue)) > 1.0e-9)
            latchAutomationOverride();
        setValue(newValue);
    }

    void resetToDefaultValue() {
        setValueFromUser(defaultValue_);
    }

    void latchAutomationOverride() {
        if (overrideLatchedThisGesture_)
            return;
        if (!hasAutomationTarget_ || !isAutomated())
            return;
        if (magda::AutomationManager::getInstance().isWriteModeEnabled())
            return;

        magda::AutomationManager::getInstance().setTargetOverridden(automationTarget_, true);
        setAutomationVisualState(magda::AutomationVisualState::Overridden);
        overrideLatchedThisGesture_ = true;
    }

    ValueLabelControl valueControl_;
    Format format_;
    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double defaultValue_ = 0.0;
    double interval_ = 0.01;
    double skewFactor_ = 1.0;
    bool useLogProjection_ = false;
    bool hasExplicitDefaultValue_ = false;
    bool hasCapturedDefaultValue_ = false;
    double dragStartValue_ = 0.0;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    bool hasDragged_ = false;
    bool overrideLatchedThisGesture_ = false;
    bool isLeftButtonDrag_ = false;
    bool isShiftDrag_ = false;
    float shiftDragStartValue_ = 0.5f;
    Orientation orientation_ = Orientation::Horizontal;
    bool rightClickEditsText_ = true;
    bool showFillIndicator_ = true;
    juce::String emptyText_ = "-";
    bool showEmptyText_ = false;
    std::function<juce::String(double)>
        valueFormatter_;  // Custom value formatting (real value -> string)
    std::function<double(const juce::String&)>
        valueParser_;                     // Custom value parsing (string -> real value)
    magda::ParameterInfo paramInfoCopy_;  // Populated by setParameterInfo
    bool hasParamInfo_ = false;
    // True if a custom formatter was installed via setValueFormatter().
    // Stops setParameterInfo() from clobbering it on every refresh —
    // FourOscUI installs format-specific labels (e.g. "L50"/"R50" for pan)
    // at construction that the generic ParameterUtils formatter cannot
    // produce. The parser is still replaced — only the display side is
    // sticky.
    bool hasExplicitFormatter_ = false;

    juce::String currentDisplayText() const {
        // Show empty text instead of value when disabled/empty
        if (showEmptyText_) {
            return emptyText_;
        }

        // Use custom formatter if provided
        if (valueFormatter_) {
            return valueFormatter_(value_);
        }

        juce::String text;

        switch (format_) {
            case Format::Decibels:
                if (value_ <= -60.0) {
                    text = "-inf";
                } else {
                    text = juce::String(value_, 1);
                }
                break;

            case Format::Pan:
                if (std::abs(value_) < 0.01) {
                    text = "C";
                } else if (value_ < 0) {
                    text = juce::String(static_cast<int>(-value_ * 100)) + "L";
                } else {
                    text = juce::String(static_cast<int>(value_ * 100)) + "R";
                }
                break;

            case Format::Decimal:
            default:
                text = juce::String(value_, 2);
                break;
        }

        return text;
    }

    void updateLabel() {
        valueControl_.setRange(minValue_, maxValue_);
        valueControl_.setValue(value_);
        valueControl_.setDisplayText(currentDisplayText());
        valueControl_.setFillMode(format_ == Format::Pan
                                      ? ValueLabelControl::FillMode::PanCentre
                                      : ValueLabelControl::FillMode::LeftToRight);
        const bool hasMeter = meterPeakL_ > 0.001f || meterPeakR_ > 0.001f;
        valueControl_.setShowFillIndicator(showFillIndicator_ &&
                                           orientation_ == Orientation::Horizontal && !hasMeter);
        valueControl_.setDrawBackground(orientation_ == Orientation::Horizontal && !hasMeter);
        valueControl_.setDrawBorder(orientation_ == Orientation::Horizontal && !hasMeter);
        valueControl_.setDragging(isLeftButtonDrag_);
        valueControl_.setTintState(toControlTintState(automationVisualState_));
    }

    float meterPeakL_ = 0.f;
    float meterPeakR_ = 0.f;

    // Automation state
    magda::AutomationTarget automationTarget_;
    bool hasAutomationTarget_ = false;
    magda::AutomationVisualState automationVisualState_ = magda::AutomationVisualState::None;
    bool listeningToAutomation_ = false;

    void refreshAutomationVisualState() {
        auto newState =
            hasAutomationTarget_
                ? magda::AutomationManager::getInstance().getVisualState(automationTarget_)
                : magda::AutomationVisualState::None;
        if (automationVisualState_ == newState)
            return;
        automationVisualState_ = newState;
        valueControl_.setTintState(toControlTintState(automationVisualState_));
        repaint();
    }

    void setAutomationVisualState(magda::AutomationVisualState state) {
        if (automationVisualState_ == state)
            return;
        automationVisualState_ = state;
        valueControl_.setTintState(toControlTintState(automationVisualState_));
        repaint();
    }

    static ValueLabelControl::TintState toControlTintState(magda::AutomationVisualState state) {
        switch (state) {
            case magda::AutomationVisualState::Overridden:
                return ValueLabelControl::TintState::Overridden;
            case magda::AutomationVisualState::Active:
                return ValueLabelControl::TintState::Automated;
            case magda::AutomationVisualState::None:
            default:
                return ValueLabelControl::TintState::None;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextSlider)
};

}  // namespace magda::daw::ui
