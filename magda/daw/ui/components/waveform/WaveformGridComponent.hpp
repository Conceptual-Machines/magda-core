#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "audio/AudioBridge.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"

namespace magda {
class TimeRuler;  // Forward declaration
}

namespace magda::daw::ui {

/** Beat grid resolution for waveform overlay */
enum class GridResolution { Off, Bar, Beat, Eighth, Sixteenth, ThirtySecond };

/**
 * @brief Scrollable waveform grid component
 *
 * Handles waveform drawing and interaction (trim, stretch).
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
    void mouseDoubleClick(const juce::MouseEvent& event) override;

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
     * @brief Set pre-computed display info (loop boundaries, source-file ranges, etc.)
     * @param info ClipDisplayInfo built from ClipInfo + BPM
     */
    void setDisplayInfo(const magda::ClipDisplayInfo& info);

    /**
     * @brief Set detected transient times (in source file seconds)
     * @param times Array of transient times in source-file seconds
     */
    void setTransientTimes(const juce::Array<double>& times);

    // ========================================================================
    // Beat Grid
    // ========================================================================

    /** Set the beat grid resolution (Off disables the grid) */
    void setGridResolution(GridResolution resolution);

    /** Get current grid resolution */
    GridResolution getGridResolution() const;

    /** Set the TimeRuler to read tempo/time-signature from (not owned) */
    void setTimeRuler(magda::TimeRuler* ruler);

    /** Set grid resolution directly in beats (overrides enum; 0 = use enum) */
    void setGridResolutionBeats(double beats);

    /** Get grid interval in beats for the current resolution */
    double getGridResolutionBeats() const;

    /** Snap a source-file time to the nearest grid line */
    double snapTimeToGrid(double time) const;

    /** Enable/disable snap-to-grid for drag operations */
    void setSnapEnabled(bool enabled);

    /** Get snap state */
    bool isSnapEnabled() const;

    // ========================================================================
    // Warp Mode
    // ========================================================================

    /** Enable/disable warp marker display and interaction */
    void setWarpMode(bool enabled);

    /** Update warp markers for display */
    void setWarpMarkers(const std::vector<magda::AudioBridge::WarpMarkerInfo>& markers);

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

    // Warp marker callbacks
    std::function<void(double sourceTime, double warpTime)> onWarpMarkerAdd;
    std::function<void(int index, double newWarpTime)> onWarpMarkerMove;
    std::function<void(int index)> onWarpMarkerRemove;

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    // Timeline mode
    bool relativeMode_ = false;
    double clipStartTime_ = 0.0;            // Clip start position on timeline (seconds)
    double clipLength_ = 0.0;               // Clip length (seconds)
    magda::ClipDisplayInfo displayInfo_{};  // Pre-computed display values

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
    enum class DragMode {
        None,
        ResizeLeft,
        ResizeRight,
        StretchLeft,
        StretchRight,
        MoveWarpMarker
    };
    DragMode dragMode_ = DragMode::None;
    double dragStartAudioOffset_ = 0.0;
    double dragStartLength_ = 0.0;
    double dragStartStartTime_ = 0.0;
    int dragStartX_ = 0;
    double dragStartSpeedRatio_ = 1.0;
    double dragStartFileDuration_ = 0.0;
    double dragStartClipLength_ = 0.0;  // Original clip.length at drag start (for stretch)

    // Throttled update for live preview
    static constexpr int DRAG_UPDATE_INTERVAL_MS = 50;  // Update arrangement view every 50ms
    juce::int64 lastDragUpdateTime_ = 0;

    // Transient markers (source file seconds)
    juce::Array<double> transientTimes_;

    // Warp mode state
    bool warpMode_ = false;
    std::vector<magda::AudioBridge::WarpMarkerInfo> warpMarkers_;
    int hoveredMarkerIndex_ = -1;
    int draggingMarkerIndex_ = -1;
    double dragStartWarpTime_ = 0.0;

    // Pre/post loop visibility
    bool showPreLoop_ = true;
    bool showPostLoop_ = true;

    // Beat grid state
    GridResolution gridResolution_ = GridResolution::Off;
    double customGridBeats_ = 0.0;           // When > 0, overrides enum-based resolution
    bool snapEnabled_ = false;               // Snap drag operations to grid
    magda::TimeRuler* timeRuler_ = nullptr;  // not owned — reads tempo/timeSig + bar origin

    // Layout info shared between paint helpers
    struct WaveformLayout {
        juce::Rectangle<int> rect;  // full waveform rect (position + size)
        int clipEndPixel;           // pixel X of the effective clip/loop end
    };

    // Painting helpers
    void paintWaveform(juce::Graphics& g, const magda::ClipInfo& clip);
    WaveformLayout computeWaveformLayout(const magda::ClipInfo& clip) const;
    void paintWaveformBackground(juce::Graphics& g, const magda::ClipInfo& clip,
                                 const WaveformLayout& layout);
    void paintWaveformThumbnail(juce::Graphics& g, const magda::ClipInfo& clip,
                                const WaveformLayout& layout);
    void paintWaveformOverlays(juce::Graphics& g, const magda::ClipInfo& clip,
                               const WaveformLayout& layout);
    void paintBeatGrid(juce::Graphics& g, const magda::ClipInfo& clip);
    void paintWarpedWaveform(juce::Graphics& g, const magda::ClipInfo& clip,
                             juce::Rectangle<int> waveformRect, juce::Colour waveColour,
                             float vertZoom);
    void paintTransientMarkers(juce::Graphics& g, const magda::ClipInfo& clip);
    void paintWarpMarkers(juce::Graphics& g, const magda::ClipInfo& clip);
    void paintClipBoundaries(juce::Graphics& g);
    void paintNoClipMessage(juce::Graphics& g);
    void showContextMenu(const juce::MouseEvent& event);

    // Hit testing helpers
    bool isNearLeftEdge(int x, const magda::ClipInfo& clip) const;
    bool isNearRightEdge(int x, const magda::ClipInfo& clip) const;
    bool isInsideWaveform(int x, const magda::ClipInfo& clip) const;

    // Warp marker helpers
    int findMarkerAtPixel(int x) const;
    double snapToNearestTransient(double time) const;
    static constexpr int WARP_MARKER_HIT_DISTANCE = 5;

    // Display start time (0.0 in relative mode, clipStartTime_ in absolute)
    double getDisplayStartTime() const {
        return relativeMode_ ? 0.0 : clipStartTime_;
    }

    // Get current clip
    const magda::ClipInfo* getClip() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformGridComponent)
};

}  // namespace magda::daw::ui
