#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "../../../core/ClipManager.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../state/TimelineController.hpp"

namespace magda {

/**
 * @brief Whole-song navigator / minimap shown in the master content strip.
 *
 * Renders every track's clips compressed to fit the strip width (the entire
 * song at a glance, independent of the main scroll/zoom) and draws a draggable
 * viewport rectangle marking the currently-visible window. Click/drag the strip
 * to recentre the arrangement, drag the box to pan, drag its edges to zoom.
 *
 * Implemented as a standalone panel swapped into masterContentArea so the
 * previous "Master Output" placeholder is easy to restore.
 */
class SongNavigatorPanel : public juce::Component,
                           public TimelineStateListener,
                           public TrackManagerListener,
                           public ClipManagerListener {
  public:
    SongNavigatorPanel();
    ~SongNavigatorPanel() override;

    /** Connect to the centralized timeline state. */
    void setController(TimelineController* controller);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    // TimelineStateListener
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

    // TrackManagerListener
    void tracksChanged() override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipPropertiesChanged(const std::vector<ClipId>& clipIds) override;

  private:
    enum class DragMode { None, MoveViewport, ResizeLeft, ResizeRight };

    double totalBeats() const;
    int beatToX(double beat) const;
    double xToBeat(int x) const;

    // Visible window (in beats) derived from the controller's zoom/scroll state.
    double visibleStartBeats() const;
    double visibleLengthBeats() const;
    juce::Rectangle<float> getViewportBox() const;

    DragMode viewportHitTest(juce::Point<int> pos) const;
    void panToStartBeat(double startBeats);
    void zoomToVisibleRange(double startBeats, double lengthBeats);

    TimelineController* controller_ = nullptr;

    DragMode dragMode_ = DragMode::None;
    double dragGrabOffsetBeats_ = 0.0;  // mouse-beat minus window-start-beat at grab

    static constexpr int kEdgeGrabPx = 5;
    static constexpr int kVInset = 3;
    static constexpr int kRulerHeight = 11;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongNavigatorPanel)
};

}  // namespace magda
