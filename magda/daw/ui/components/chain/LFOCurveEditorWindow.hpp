#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "LFOCurveEditor.hpp"
#include "core/ModInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Content component containing curve editor and toolbar
 */
class LFOCurveEditorContent : public juce::Component {
  public:
    LFOCurveEditorContent(magda::ModInfo* modInfo, std::function<void()> onWaveformChanged,
                          std::function<void()> onDragPreview);

    void resized() override;
    void paint(juce::Graphics& g) override;

    magda::LFOCurveEditor& getCurveEditor() {
        return curveEditor_;
    }

    // Callbacks for rate/sync changes (passed through to parent)
    std::function<void(float)> onRateChanged;
    std::function<void(bool)> onTempoSyncChanged;
    std::function<void(magda::SyncDivision)> onSyncDivisionChanged;
    std::function<void(bool)> onOneShotChanged;
    std::function<void(bool)> onLoopRegionChanged;

  private:
    magda::ModInfo* modInfo_;
    magda::LFOCurveEditor curveEditor_;

    // Toolbar controls
    juce::TextButton syncToggle_;
    TextSlider rateSlider_{TextSlider::Format::Decimal};
    juce::ComboBox syncDivisionCombo_;
    juce::TextButton loopOneShotToggle_;
    juce::TextButton msegToggle_;

    // Grid controls
    juce::Label gridLabel_;
    juce::ComboBox gridXCombo_;
    juce::ComboBox gridYCombo_;
    juce::TextButton snapXToggle_;
    juce::TextButton snapYToggle_;

    void setupControls();
    void updateControlsFromModInfo();

    static constexpr int FOOTER_HEIGHT = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LFOCurveEditorContent)
};

/**
 * @brief Popup window for larger LFO curve editing
 *
 * Provides a resizable window with a larger curve editor for detailed waveform editing.
 * Includes toolbar with rate/sync, loop/one-shot, MSEG, grid, and snap controls.
 */
class LFOCurveEditorWindow : public juce::DocumentWindow {
  public:
    LFOCurveEditorWindow(magda::ModInfo* modInfo, std::function<void()> onWaveformChanged,
                         std::function<void()> onDragPreview = nullptr);
    ~LFOCurveEditorWindow() override = default;

    void closeButtonPressed() override;

    // Get the curve editor for syncing
    magda::LFOCurveEditor& getCurveEditor() {
        return content_.getCurveEditor();
    }

    // Callbacks for rate/sync changes
    std::function<void(float)> onRateChanged;
    std::function<void(bool)> onTempoSyncChanged;
    std::function<void(magda::SyncDivision)> onSyncDivisionChanged;
    std::function<void(bool)> onOneShotChanged;
    std::function<void(bool)> onLoopRegionChanged;

  private:
    LFOCurveEditorContent content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LFOCurveEditorWindow)
};

}  // namespace magda::daw::ui
