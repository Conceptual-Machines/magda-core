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
     * @brief Set vertical zoom level (amplitude scaling)
     * @param zoom Multiplier for waveform height (1.0 = normal)
     */
    void setVerticalZoom(double zoom);

    /**
     * @brief Set scroll offset for coordinate calculations
     */
    void setScrollOffset(int x, int y);

    /**
     * @brief Update grid size based on clip and zoom
     * Called when clip changes or zoom changes
     */
    void updateGridSize();

    /**
     * @brief Set the minimum height for the grid (typically the viewport height)
     * The grid will be at least this tall so the waveform fills available space
     */
    void setMinimumHeight(int height);

    /**
     * @brief Update clip position and length without full reload
     * Used when clip is moved on timeline to avoid feedback loops
     */
    void updateClipPosition(double startTime, double length);

    /**
     * @brief Set the loop end boundary in seconds (clip-relative)
     * When set (> 0), audio past the loop point is dimmed.
     * Pass 0 or negative to clear.
     */
    void setLoopEndSeconds(double loopEndSeconds);

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
    double clipStartTime_ = 0.0;   // Clip start position on timeline (seconds)
    double clipLength_ = 0.0;      // Clip length (seconds)
    double loopEndSeconds_ = 0.0;  // Loop end boundary in seconds (0 = no loop)

    // Zoom and scroll
    double horizontalZoom_ = 100.0;  // pixels per second
    double verticalZoom_ = 1.0;      // amplitude multiplier
    int scrollOffsetX_ = 0;
    int scrollOffsetY_ = 0;

    // Layout constants
    static constexpr int LEFT_PADDING = 10;
    static constexpr int RIGHT_PADDING = 10;
    static constexpr int TOP_PADDING = 10;
    static constexpr int BOTTOM_PADDING = 10;
    static constexpr int EDGE_GRAB_DISTANCE = 10;
    int minimumHeight_ = 400;

    // Drag state
    enum class DragMode { None, ResizeLeft, ResizeRight, Move, StretchLeft, StretchRight };
    DragMode dragMode_ = DragMode::None;
    double dragStartPosition_ = 0.0;
    double dragStartAudioOffset_ = 0.0;
    double dragStartLength_ = 0.0;
    int dragStartX_ = 0;
    double dragStartStretchFactor_ = 1.0;
    double dragStartFileDuration_ = 0.0;

    // Throttled update for live preview
    static constexpr int DRAG_UPDATE_INTERVAL_MS = 50;  // Update arrangement view every 50ms
    juce::int64 lastDragUpdateTime_ = 0;

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
