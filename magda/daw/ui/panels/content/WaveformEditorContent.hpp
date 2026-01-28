#pragma once

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Waveform editor for audio clips
 *
 * Displays audio waveform for editing:
 * - Waveform visualization from real audio data
 * - Time axis along the top
 * - Trim handles for adjusting source boundaries
 * - Drag to move audio source within clip
 */
class WaveformEditorContent : public PanelContent, public magda::ClipManagerListener {
  public:
    WaveformEditorContent();
    ~WaveformEditorContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::WaveformEditor;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::WaveformEditor, "Waveform", "Audio waveform editor", "Waveform"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    // Mouse interaction for trim/move
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);
    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    // Layout constants
    static constexpr int HEADER_HEIGHT = 24;
    static constexpr int SIDE_MARGIN = 20;
    static constexpr int EDGE_GRAB_DISTANCE = 10;

    // Zoom
    double horizontalZoom_ = 100.0;  // pixels per second

    // Drag state
    enum class DragMode { None, ResizeLeft, ResizeRight, Move };
    DragMode dragMode_ = DragMode::None;
    double dragStartPosition_ = 0.0;
    double dragStartAudioOffset_ = 0.0;
    double dragStartLength_ = 0.0;
    int dragStartX_ = 0;

    // Painting helpers
    void paintHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void paintWaveform(juce::Graphics& g, juce::Rectangle<int> area, const magda::ClipInfo& clip);
    void paintNoClipMessage(juce::Graphics& g, juce::Rectangle<int> area);

    // Hit testing helpers
    juce::Rectangle<int> getWaveformArea() const;
    bool isNearLeftEdge(int x, const magda::AudioSource& source) const;
    bool isNearRightEdge(int x, const magda::AudioSource& source) const;
    bool isInsideWaveform(int x, const magda::AudioSource& source) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformEditorContent)
};

}  // namespace magda::daw::ui
