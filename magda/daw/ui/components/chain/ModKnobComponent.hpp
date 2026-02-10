#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/LinkModeManager.hpp"
#include "core/ModInfo.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Mini waveform display for mod knob
 */
class MiniWaveformDisplay : public juce::Component, private juce::Timer {
  public:
    MiniWaveformDisplay() {
        startTimer(33);  // 30 FPS animation
    }

    ~MiniWaveformDisplay() {
        stopTimer();
    }

    void setModInfo(const magda::ModInfo* mod) {
        mod_ = mod;
        if (getWidth() > 0 && getHeight() > 0)
            repaint();
    }

    void paint(juce::Graphics& g) override {
        if (!mod_) {
            return;
        }

        auto bounds = getLocalBounds().toFloat();
        const float width = bounds.getWidth();
        const float height = bounds.getHeight();

        if (width < 1.0f || height < 1.0f)
            return;
        const float centerY = height * 0.5f;

        // Draw waveform path
        juce::Path waveformPath;
        const int numPoints = 50;  // Fewer points for mini display

        for (int i = 0; i < numPoints; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(numPoints - 1);
            float value = magda::ModulatorEngine::generateWaveformForMod(*mod_, phase);

            // Invert value so high values are at top
            float y = centerY + (0.5f - value) * (height - 2.0f);
            float x = bounds.getX() + phase * width;

            if (i == 0) {
                waveformPath.startNewSubPath(x, y);
            } else {
                waveformPath.lineTo(x, y);
            }
        }

        // Draw the waveform line (thinner for mini display)
        g.setColour(juce::Colours::orange.withAlpha(0.5f));
        g.strokePath(waveformPath, juce::PathStrokeType(1.0f));

        // Draw current phase indicator (smaller dot)
        float currentX = bounds.getX() + mod_->phase * width;
        float currentValue = mod_->value;
        float currentY = centerY + (0.5f - currentValue) * (height - 2.0f);

        g.setColour(juce::Colours::orange);
        g.fillEllipse(currentX - 2.0f, currentY - 2.0f, 4.0f, 4.0f);
    }

  private:
    void timerCallback() override {
        if (getWidth() > 0 && getHeight() > 0)
            repaint();
    }

    const magda::ModInfo* mod_ = nullptr;
};

/**
 * @brief A single mod cell with type icon, name, amount slider, and link button
 *
 * Supports drag-and-drop: drag from this knob onto a ParamSlotComponent to create a link.
 *
 * Layout (vertical, ~60px wide):
 * +-----------+
 * | LFO 1     |  <- type + name label
 * |   0.50    |  <- amount slider
 * |   [Link]  |  <- link button (toggle link mode)
 * +-----------+
 *
 * Clicking the main area opens the modulator editor side panel.
 * Clicking the link button enters link mode for this mod.
 */
class ModKnobComponent : public juce::Component, public magda::LinkModeManagerListener {
  public:
    explicit ModKnobComponent(int modIndex);
    ~ModKnobComponent() override;

    // Set mod info from data model
    void setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod = nullptr);

    // Set available devices for linking (name and deviceId pairs)
    void setAvailableTargets(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Set parent path for drag-and-drop identification
    void setParentPath(const magda::ChainNodePath& path) {
        parentPath_ = path;
    }
    const magda::ChainNodePath& getParentPath() const {
        return parentPath_;
    }
    int getModIndex() const {
        return modIndex_;
    }

    // Selection state (this mod cell is selected)
    void setSelected(bool selected);
    bool isSelected() const {
        return selected_;
    }

    // Force repaint of the waveform display (for curve editor sync)
    void repaintWaveform() {
        waveformDisplay_.repaint();
    }

    // Callbacks
    std::function<void(float)> onAmountChanged;
    std::function<void(magda::ModTarget)> onTargetChanged;
    std::function<void(juce::String)> onNameChanged;
    std::function<void()> onClicked;            // Opens modulator editor panel
    std::function<void()> onRemoveRequested;    // Remove this mod
    std::function<void(bool)> onEnableToggled;  // Enable/disable this mod

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Drag-and-drop description prefix
    static constexpr const char* DRAG_PREFIX = "mod_drag:";

  private:
    // LinkModeManagerListener implementation
    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;

    void showContextMenu();
    void paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area);
    void onNameLabelEdited();
    void onLinkButtonClicked();

    int modIndex_;
    juce::Label nameLabel_;
    TextSlider amountSlider_{TextSlider::Format::Decimal};
    MiniWaveformDisplay waveformDisplay_;
    std::unique_ptr<magda::SvgButton> linkButton_;
    magda::ModInfo currentMod_;
    const magda::ModInfo* liveModPtr_ = nullptr;  // Pointer to live mod for animation
    std::vector<std::pair<magda::DeviceId, juce::String>> availableTargets_;
    bool selected_ = false;
    magda::ChainNodePath parentPath_;  // For drag-and-drop identification

    // Drag state
    juce::Point<int> dragStartPos_;
    bool isDragging_ = false;
    static constexpr int DRAG_THRESHOLD = 5;

    static constexpr int KNOB_PADDING = 4;
    static constexpr int NAME_LABEL_HEIGHT = 11;
    static constexpr int AMOUNT_SLIDER_HEIGHT = 14;
    static constexpr int LINK_BUTTON_HEIGHT = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModKnobComponent)
};

}  // namespace magda::daw::ui
