#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ModInfo.hpp"
#include "core/ModulatorEngine.hpp"
#include "ui/components/chain/LFOCurveEditor.hpp"
#include "ui/components/chain/LFOCurveEditorWindow.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Animated waveform display component
 */
class WaveformDisplay : public juce::Component, private juce::Timer {
  public:
    // Animation and rendering constants
    static constexpr int TIMER_INTERVAL_MS = 33;  // ~30 FPS animation
    static constexpr int WAVEFORM_POINTS = 100;   // Number of points for waveform path
    static constexpr float PHASE_INDICATOR_RADIUS = 4.0f;
    static constexpr float TRIGGER_INDICATOR_RADIUS = 3.0f;
    static constexpr float DASH_LENGTH = 3.0f;
    static constexpr float WAVEFORM_STROKE_WIDTH = 1.5f;
    static constexpr float WAVEFORM_MARGIN = 8.0f;  // Vertical margin for waveform

    WaveformDisplay() {
        startTimer(TIMER_INTERVAL_MS);
    }

    ~WaveformDisplay() override {
        stopTimer();
    }

    void setModInfo(const magda::ModInfo* mod) {
        mod_ = mod;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        if (!mod_) {
            return;
        }

        auto bounds = getLocalBounds().toFloat();
        const float width = bounds.getWidth();
        const float height = bounds.getHeight();
        const float centerY = height * 0.5f;

        drawPhaseOffsetIndicator(g, bounds, width, height);
        drawWaveformPath(g, bounds, width, height, centerY);
        drawCurrentPhaseIndicator(g, bounds, width, height, centerY);
        drawTriggerIndicator(g, bounds);
    }

  private:
    void timerCallback() override {
        repaint();
    }

    /**
     * @brief Draw vertical dashed line showing phase offset position
     */
    void drawPhaseOffsetIndicator(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                                   float width, float /*height*/) {
        if (mod_->phaseOffset <= 0.001f) {
            return;
        }

        float offsetX = bounds.getX() + mod_->phaseOffset * width;
        g.setColour(juce::Colours::orange.withAlpha(0.3f));

        // Draw dashed line
        for (float y = bounds.getY(); y < bounds.getBottom(); y += DASH_LENGTH * 2) {
            g.drawLine(offsetX, y, offsetX, juce::jmin(y + DASH_LENGTH, bounds.getBottom()),
                       1.0f);
        }
    }

    /**
     * @brief Draw the waveform curve path
     */
    void drawWaveformPath(juce::Graphics& g, const juce::Rectangle<float>& bounds, float width,
                          float height, float centerY) {
        juce::Path waveformPath;

        for (int i = 0; i < WAVEFORM_POINTS; ++i) {
            float displayPhase = static_cast<float>(i) / static_cast<float>(WAVEFORM_POINTS - 1);
            // Apply phase offset to show how waveform is shifted
            float effectivePhase = std::fmod(displayPhase + mod_->phaseOffset, 1.0f);
            float value = magda::ModulatorEngine::generateWaveformForMod(*mod_, effectivePhase);

            // Invert value so high values are at top
            float y = centerY + (0.5f - value) * (height - WAVEFORM_MARGIN);
            float x = bounds.getX() + displayPhase * width;

            if (i == 0) {
                waveformPath.startNewSubPath(x, y);
            } else {
                waveformPath.lineTo(x, y);
            }
        }

        // Draw the waveform line
        g.setColour(juce::Colours::orange.withAlpha(0.7f));
        g.strokePath(waveformPath, juce::PathStrokeType(WAVEFORM_STROKE_WIDTH));
    }

    /**
     * @brief Draw dot showing current phase position on waveform
     */
    void drawCurrentPhaseIndicator(juce::Graphics& g, const juce::Rectangle<float>& bounds,
                                    float width, float height, float centerY) {
        float displayX = bounds.getX() + mod_->phase * width;
        float currentValue = mod_->value;
        float currentY = centerY + (0.5f - currentValue) * (height - WAVEFORM_MARGIN);

        g.setColour(juce::Colours::orange);
        g.fillEllipse(displayX - PHASE_INDICATOR_RADIUS, currentY - PHASE_INDICATOR_RADIUS,
                      PHASE_INDICATOR_RADIUS * 2, PHASE_INDICATOR_RADIUS * 2);
    }

    /**
     * @brief Draw trigger indicator in top-right corner
     */
    void drawTriggerIndicator(juce::Graphics& g, const juce::Rectangle<float>& bounds) {
        auto triggerDotBounds = juce::Rectangle<float>(
            bounds.getRight() - TRIGGER_INDICATOR_RADIUS * 2 - 4.0f, bounds.getY() + 4.0f,
            TRIGGER_INDICATOR_RADIUS * 2, TRIGGER_INDICATOR_RADIUS * 2);

        if (mod_->triggered) {
            // Lit up when triggered
            g.setColour(juce::Colours::orange);
            g.fillEllipse(triggerDotBounds);
        } else {
            // Outline only when not triggered
            g.setColour(juce::Colours::orange.withAlpha(0.3f));
            g.drawEllipse(triggerDotBounds, 1.0f);
        }
    }

    const magda::ModInfo* mod_ = nullptr;
};

/**
 * @brief Panel for editing modulator settings
 *
 * Shows when a mod is selected from the mods panel.
 * Displays type selector, rate control, and target info.
 *
 * Layout:
 * +------------------+
 * |    MOD NAME      |  <- Header with mod name
 * +------------------+
 * | Type: [LFO   v]  |  <- Type selector
 * +------------------+
 * |   Rate: 1.0 Hz   |  <- Rate slider
 * +------------------+
 * | Target: Device   |  <- Target info
 * |   Param Name     |
 * +------------------+
 */
class ModulatorEditorPanel : public juce::Component, private juce::Timer {
  public:
    ModulatorEditorPanel();
    ~ModulatorEditorPanel() override;

    // Set the mod to edit
    void setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod = nullptr);

    // Set the selected mod index (-1 for none)
    void setSelectedModIndex(int index);
    int getSelectedModIndex() const {
        return selectedModIndex_;
    }

    // Callbacks
    std::function<void(float rate)> onRateChanged;
    std::function<void(magda::LFOWaveform waveform)> onWaveformChanged;
    std::function<void(bool tempoSync)> onTempoSyncChanged;
    std::function<void(magda::SyncDivision division)> onSyncDivisionChanged;
    std::function<void(magda::LFOTriggerMode mode)> onTriggerModeChanged;
    std::function<void()> onCurveChanged;  // Fires when curve points are edited

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Preferred width for this panel
    static constexpr int PREFERRED_WIDTH = 150;

  private:
    int selectedModIndex_ = -1;
    magda::ModInfo currentMod_;
    const magda::ModInfo* liveModPtr_ = nullptr;  // Pointer to live mod for waveform animation

    // UI Components
    juce::Label nameLabel_;
    juce::ComboBox waveformCombo_;  // LFO shape selector (Sine, Triangle, etc.)
    WaveformDisplay waveformDisplay_;
    magda::LFOCurveEditor curveEditor_;                        // Custom waveform editor
    std::unique_ptr<magda::SvgButton> curveEditorButton_;      // Button to open external editor
    std::unique_ptr<LFOCurveEditorWindow> curveEditorWindow_;  // External editor window
    bool isCurveMode_ = false;                                 // True when waveform is Custom
    juce::ComboBox curvePresetCombo_;                          // Preset selector for curve mode
    std::unique_ptr<magda::SvgButton> savePresetButton_;       // Save preset button
    juce::TextButton syncToggle_;
    juce::ComboBox syncDivisionCombo_;
    TextSlider rateSlider_{TextSlider::Format::Decimal};
    juce::ComboBox triggerModeCombo_;
    std::unique_ptr<magda::SvgButton> advancedButton_;

    void updateFromMod();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorEditorPanel)
};

}  // namespace magda::daw::ui
