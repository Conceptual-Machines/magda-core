#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipTypes.hpp"

namespace magda {

/**
 * @brief Modal editor for session view clips
 *
 * Provides waveform viewing and editing for clips in session view.
 * - Always uses relative (beat-based) positioning
 * - Shows waveform with zoom/scroll controls
 * - Loop enable/disable toggle in header
 * - Trim start/end handles
 * - Audio offset adjustment
 */
class SessionClipEditor : public juce::Component, public ClipManagerListener {
  public:
    explicit SessionClipEditor(ClipId clipId);
    ~SessionClipEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;

    // Callbacks
    std::function<void()> onCloseRequested;

  private:
    ClipId clipId_;
    ClipInfo cachedClip_;  // Local cache for faster rendering

    // Header controls
    std::unique_ptr<juce::TextButton> closeButton_;
    std::unique_ptr<juce::ToggleButton> loopToggle_;
    std::unique_ptr<juce::Label> clipNameLabel_;
    std::unique_ptr<juce::Label> lengthLabel_;

    // Waveform display area
    class WaveformDisplay;
    std::unique_ptr<WaveformDisplay> waveformDisplay_;

    // Footer controls
    std::unique_ptr<juce::Slider> offsetSlider_;
    std::unique_ptr<juce::Label> offsetLabel_;

    void setupHeader();
    void setupWaveform();
    void setupFooter();
    void updateClipCache();
    void updateControls();

    // Layout constants
    static constexpr int HEADER_HEIGHT = 50;
    static constexpr int FOOTER_HEIGHT = 60;
    static constexpr int MARGIN = 10;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipEditor)
};

/**
 * @brief Modal window wrapper for SessionClipEditor
 */
class SessionClipEditorWindow : public juce::DocumentWindow {
  public:
    SessionClipEditorWindow(ClipId clipId, const juce::String& clipName);
    ~SessionClipEditorWindow() override;

    void closeButtonPressed() override;

  private:
    std::unique_ptr<SessionClipEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipEditorWindow)
};

}  // namespace magda
