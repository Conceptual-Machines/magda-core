#pragma once

#include <memory>

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"

namespace magda {
class PianoRollGridComponent;
class PianoRollKeyboard;
class TimeRuler;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief Piano roll editor for MIDI clips
 *
 * Displays MIDI notes in a piano roll grid layout:
 * - Keyboard on the left showing note names
 * - Note rectangles in the grid representing MIDI notes (interactive)
 * - Time ruler along the top (switchable between absolute/relative)
 */
class PianoRollContent : public PanelContent, public magda::ClipManagerListener {
  public:
    PianoRollContent();
    ~PianoRollContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::PianoRoll;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::PianoRoll, "Piano Roll", "MIDI note editor", "PianoRoll"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);
    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

    // Timeline mode
    void setRelativeTimeMode(bool relative);
    bool isRelativeTimeMode() const {
        return relativeTimeMode_;
    }

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    // Layout constants
    static constexpr int KEYBOARD_WIDTH = 60;
    static constexpr int NOTE_HEIGHT = 12;
    static constexpr int HEADER_HEIGHT = 24;
    static constexpr int MIN_NOTE = 21;          // A0
    static constexpr int MAX_NOTE = 108;         // C8
    static constexpr int GRID_LEFT_PADDING = 2;  // Small padding for timeline label visibility

    // Zoom
    double horizontalZoom_ = 50.0;  // pixels per beat

    // Timeline mode (absolute vs relative)
    bool relativeTimeMode_ = true;  // Default to relative (1, 2, 3...)

    // Components
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<magda::PianoRollGridComponent> gridComponent_;
    std::unique_ptr<magda::PianoRollKeyboard> keyboard_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    std::unique_ptr<juce::TextButton> timeModeButton_;

    // Grid component management
    void setupGridCallbacks();
    void updateGridSize();
    void updateTimeRuler();
    void syncTempoFromEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollContent)
};

}  // namespace magda::daw::ui
