#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ModInfo.hpp"
#include "core/ModulatorEngine.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Animated waveform display component
 */
class WaveformDisplay : public juce::Component, private juce::Timer {
  public:
    WaveformDisplay() {
        startTimer(33);  // 30 FPS animation
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

        // Draw phase offset indicator line (vertical dashed line at offset position)
        if (mod_->phaseOffset > 0.001f) {
            float offsetX = bounds.getX() + mod_->phaseOffset * width;
            g.setColour(juce::Colours::orange.withAlpha(0.3f));
            // Draw dashed line
            const float dashLength = 3.0f;
            for (float y = bounds.getY(); y < bounds.getBottom(); y += dashLength * 2) {
                g.drawLine(offsetX, y, offsetX, juce::jmin(y + dashLength, bounds.getBottom()),
                           1.0f);
            }
        }

        // Draw waveform path (shifted by phase offset for visual representation)
        juce::Path waveformPath;
        const int numPoints = 100;

        for (int i = 0; i < numPoints; ++i) {
            float displayPhase = static_cast<float>(i) / static_cast<float>(numPoints - 1);
            // Apply phase offset to show how waveform is shifted
            float effectivePhase = std::fmod(displayPhase + mod_->phaseOffset, 1.0f);
            float value = magda::ModulatorEngine::generateWaveform(mod_->waveform, effectivePhase);

            // Invert value so high values are at top
            float y = centerY + (0.5f - value) * (height - 8.0f);
            float x = bounds.getX() + displayPhase * width;

            if (i == 0) {
                waveformPath.startNewSubPath(x, y);
            } else {
                waveformPath.lineTo(x, y);
            }
        }

        // Draw the waveform line
        g.setColour(juce::Colours::orange.withAlpha(0.7f));
        g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

        // Draw current phase indicator (dot) - use actual phase position
        float displayX = bounds.getX() + mod_->phase * width;
        float currentValue = mod_->value;
        float currentY = centerY + (0.5f - currentValue) * (height - 8.0f);

        g.setColour(juce::Colours::orange);
        g.fillEllipse(displayX - 4.0f, currentY - 4.0f, 8.0f, 8.0f);

        // Draw trigger indicator dot in top-right corner
        const float triggerDotRadius = 3.0f;
        auto triggerDotBounds = juce::Rectangle<float>(
            bounds.getRight() - triggerDotRadius * 2 - 4.0f, bounds.getY() + 4.0f,
            triggerDotRadius * 2, triggerDotRadius * 2);

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

  private:
    void timerCallback() override {
        repaint();
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
class ModulatorEditorPanel : public juce::Component {
  public:
    ModulatorEditorPanel();
    ~ModulatorEditorPanel() override = default;

    // Set the mod to edit
    void setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod = nullptr);

    // Set the selected mod index (-1 for none)
    void setSelectedModIndex(int index);
    int getSelectedModIndex() const {
        return selectedModIndex_;
    }

    // Callbacks
    std::function<void(magda::ModType type)> onTypeChanged;
    std::function<void(float rate)> onRateChanged;
    std::function<void(magda::LFOWaveform waveform)> onWaveformChanged;
    std::function<void(float phaseOffset)> onPhaseOffsetChanged;
    std::function<void(bool tempoSync)> onTempoSyncChanged;
    std::function<void(magda::SyncDivision division)> onSyncDivisionChanged;
    std::function<void(magda::LFOTriggerMode mode)> onTriggerModeChanged;

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
    juce::ComboBox typeSelector_;
    juce::ComboBox waveformCombo_;
    WaveformDisplay waveformDisplay_;
    TextSlider phaseSlider_{TextSlider::Format::Decimal};
    juce::TextButton syncToggle_;
    juce::ComboBox syncDivisionCombo_;
    TextSlider rateSlider_{TextSlider::Format::Decimal};
    juce::ComboBox triggerModeCombo_;
    std::unique_ptr<magda::SvgButton> advancedButton_;
    juce::Label targetLabel_;

    void updateFromMod();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorEditorPanel)
};

}  // namespace magda::daw::ui
