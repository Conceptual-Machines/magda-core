#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>

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

        // ADSR envelopes draw their shape instead of a periodic waveform.
        if (mod_->type == magda::ModType::Envelope) {
            paintEnvelope(g, bounds);
            return;
        }

        // Random and envelope-follower modulators draw a scrolling history of
        // their output instead of a periodic waveform.
        if (mod_->type == magda::ModType::Random || mod_->type == magda::ModType::Follower) {
            paintRandom(g, bounds);
            return;
        }

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
    // Compact ADSR shape with a dot at the current value (overlaid from the
    // live TE modifier via mod_->value / mod_->envStage).
    void paintEnvelope(juce::Graphics& g, juce::Rectangle<float> bounds) {
        const float top = bounds.getY() + 1.0f;
        const float bottom = bounds.getBottom() - 1.0f;
        const float h = bottom - top;
        const float w = bounds.getWidth();

        constexpr float kSustainFrac = 0.22f;
        const float timed = w * (1.0f - kSustainFrac);
        const float a = juce::jmax(mod_->envAttackMs, 1.0f);
        const float d = juce::jmax(mod_->envDecayMs, 1.0f);
        const float r = juce::jmax(mod_->envReleaseMs, 1.0f);
        const float sum = a + d + r;
        const float xA = bounds.getX() + timed * (a / sum);
        const float xD = xA + timed * (d / sum);
        const float xS = xD + w * kSustainFrac;
        const float xR = xS + timed * (r / sum);
        const float sustainY = bottom - mod_->envSustain * h;

        juce::Path path;
        path.startNewSubPath(bounds.getX(), bottom);
        path.lineTo(xA, top);
        path.lineTo(xD, sustainY);
        path.lineTo(xS, sustainY);
        path.lineTo(xR, bottom);

        g.setColour(juce::Colours::orange.withAlpha(0.5f));
        g.strokePath(path, juce::PathStrokeType(1.0f));

        if (mod_->envStage != 0) {
            const float v = juce::jlimit(0.0f, 1.0f, mod_->value);
            float dotX = bounds.getX();
            switch (mod_->envStage) {
                case 1:
                    dotX = bounds.getX() + (xA - bounds.getX()) * v;
                    break;
                case 2:
                    dotX = (xA + xD) * 0.5f;
                    break;
                case 3:
                    dotX = (xD + xS) * 0.5f;
                    break;
                case 4:
                    dotX = (xS + xR) * 0.5f;
                    break;
                default:
                    break;
            }
            g.setColour(juce::Colours::orange);
            g.fillEllipse(dotX - 2.0f, (bottom - v * h) - 2.0f, 4.0f, 4.0f);
        }
    }

    // Compact scrolling trace of the random output (oldest -> newest), fed
    // from mod_->value (overlaid from the live TE modifier) each tick.
    void paintRandom(juce::Graphics& g, juce::Rectangle<float> bounds) {
        const float w = bounds.getWidth();
        const float h = bounds.getHeight() - 2.0f;
        const float x0 = bounds.getX();
        const float bottom = bounds.getBottom() - 1.0f;

        juce::Path path;
        const int n = static_cast<int>(randomHistory_.size());
        for (int i = 0; i < n; ++i) {
            const int idx = (randomWritePos_ + i) % n;  // randomWritePos_ = oldest slot
            const float v = juce::jlimit(0.0f, 1.0f, randomHistory_[static_cast<size_t>(idx)]);
            const float x = x0 + (static_cast<float>(i) / static_cast<float>(n - 1)) * w;
            const float y = bottom - v * h;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(juce::Colours::orange.withAlpha(0.5f));
        g.strokePath(path, juce::PathStrokeType(1.0f));

        const float v = juce::jlimit(0.0f, 1.0f, mod_->value);
        g.setColour(juce::Colours::orange);
        g.fillEllipse(bounds.getRight() - 3.0f, bottom - v * h - 2.0f, 4.0f, 4.0f);
    }

    void timerCallback() override {
        if (mod_ &&
            (mod_->type == magda::ModType::Random || mod_->type == magda::ModType::Follower)) {
            randomHistory_[static_cast<size_t>(randomWritePos_)] = mod_->value;
            randomWritePos_ = (randomWritePos_ + 1) % static_cast<int>(randomHistory_.size());
        }
        if (getWidth() > 0 && getHeight() > 0)
            repaint();
    }

    const magda::ModInfo* mod_ = nullptr;
    std::array<float, 48> randomHistory_{};
    int randomWritePos_ = 0;
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

    // Set parameter names per device (for the link menu)
    void setDeviceParamNames(
        const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames);

    // Set available modifiers in the same scope so the right-click menu can
    // expose mod->mod-rate links. The parent should filter out THIS mod
    // (same id) before passing to avoid offering a self-target.
    void setAvailableModifiers(
        const std::vector<std::pair<magda::ModId, juce::String>>& modifiers) {
        availableModifiers_ = modifiers;
    }

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
    std::function<void(magda::ControlTarget)> onTargetChanged;
    std::function<void(magda::ControlTarget)> onLinkRemoved;
    std::function<void()> onAllLinksCleared;
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
    MiniWaveformDisplay waveformDisplay_;
    std::unique_ptr<magda::SvgButton> linkButton_;
    magda::ModInfo currentMod_;
    const magda::ModInfo* liveModPtr_ = nullptr;  // Pointer to live mod for animation
    std::vector<std::pair<magda::DeviceId, juce::String>> availableTargets_;
    std::map<magda::DeviceId, std::vector<juce::String>> deviceParamNames_;
    std::vector<std::pair<magda::ModId, juce::String>> availableModifiers_;
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
