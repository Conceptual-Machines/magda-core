#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Scrollable waveform grid component
 *
 * Handles waveform drawing and interaction (trim, move, stretch).
 * Designed to be placed inside a Viewport for scrolling.
 * Similar to PianoRollGridComponent architecture.
 */
class WaveformGridComponent : public juce::Component {
  public:
    WaveformGridComponent();
    ~WaveformGridComponent() override = default;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set the clip to display
     */
    void setClip(magda::ClipId clipId);

    /**
     * @brief Set timeline mode (absolute vs relative)
     * @param relative true = clip-relative (bar 1 at clip start)
     *                 false = timeline-absolute (show actual bar numbers)
     */
    void setRelativeMode(bool relative);

    /**
     * @brief Set horizontal zoom level
     * @param pixelsPerSecond Zoom level in pixels per second of audio
     */
    void setHorizontalZoom(double pixelsPerSecond);

    /**
     * @brief Set scroll offset for coordinate calculations
     */
    void setScrollOffset(int x, int y);

    /**
     * @brief Update grid size based on clip and zoom
     * Called when clip changes or zoom changes
     */
    void updateGridSize();

    // ========================================================================
    // Coordinate Conversion
    // ========================================================================

    /**
     * @brief Convert time (seconds) to pixel position
     * @param time Time in seconds (absolute timeline or clip-relative depending on mode)
     * @return Pixel X position in grid coordinate space
     */
    int timeToPixel(double time) const;

    /**
     * @brief Convert pixel position to time (seconds)
     * @param x Pixel X position in grid coordinate space
     * @return Time in seconds (absolute timeline or clip-relative depending on mode)
     */
    double pixelToTime(int x) const;

    // ========================================================================
    // Callbacks
    // ========================================================================

    std::function<void()> onWaveformChanged;

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    // Timeline mode
    bool relativeMode_ = false;
    double clipStartTime_ = 0.0;  // Clip start position on timeline (seconds)
    double clipLength_ = 0.0;     // Clip length (seconds)

    // Zoom and scroll
    double horizontalZoom_ = 100.0;  // pixels per second
    int scrollOffsetX_ = 0;
    int scrollOffsetY_ = 0;

    // Layout constants
    static constexpr int LEFT_PADDING = 10;
    static constexpr int RIGHT_PADDING = 10;
    static constexpr int TOP_PADDING = 10;
    static constexpr int BOTTOM_PADDING = 10;
    static constexpr int EDGE_GRAB_DISTANCE = 10;

    // Drag state
    enum class DragMode { None, ResizeLeft, ResizeRight, Move, StretchLeft, StretchRight };
    DragMode dragMode_ = DragMode::None;
    double dragStartPosition_ = 0.0;
    double dragStartAudioOffset_ = 0.0;
    double dragStartLength_ = 0.0;
    int dragStartX_ = 0;
    double dragStartStretchFactor_ = 1.0;
    double dragStartFileDuration_ = 0.0;

    // Painting helpers
    void paintWaveform(juce::Graphics& g, const magda::ClipInfo& clip);
    void paintClipBoundaries(juce::Graphics& g);
    void paintNoClipMessage(juce::Graphics& g);

    // Hit testing helpers
    bool isNearLeftEdge(int x, const magda::AudioSource& source) const;
    bool isNearRightEdge(int x, const magda::AudioSource& source) const;
    bool isInsideWaveform(int x, const magda::AudioSource& source) const;

    // Get current clip
    const magda::ClipInfo* getClip() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformGridComponent)
};

}  // namespace magda::daw::ui
