#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "PitchFoldMap.hpp"
#include "core/ClipTypes.hpp"

namespace magda::daw::ui {

/**
 * @brief Folded take-lane strip below the piano roll (#1466).
 *
 * One compact mini-roll per MIDI loop-record take, stacked vertically and
 * sharing the grid's beat/time axis (so columns line up with the main roll).
 * All lanes share one folded pitch axis (the union of every take's pitches via
 * PitchFoldMap) so notes line up across lanes. Velocity sets block opacity.
 *
 * Interaction mirrors the audio take lanes: click a lane to make it the active
 * take; swipe a lane over a beat range to assign that range of the comp to it;
 * right-click to clear the comp. Hovering a lane reports the take index so the
 * host can ghost that take's notes into the main roll.
 */
class MidiTakeLanesComponent : public juce::Component {
  public:
    MidiTakeLanesComponent();
    ~MidiTakeLanesComponent() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    /** Point at a clip and refresh from its takes/comp. */
    void setClip(magda::ClipId clipId);
    /** Re-read take/comp/colour state from the current clip and repaint. */
    void refresh();

    // Horizontal axis (kept in sync with the grid).
    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int offsetX);
    void setLeftPadding(int padding);
    void setRelativeMode(bool relative);

    /** Fixed left gutter (aligned with the keyboard column) for the take name. */
    void setLabelGutter(int gutter);

    // Swipe snapping.
    void setGridResolutionBeats(double beats) {
        gridResolutionBeats_ = beats;
    }
    void setSnapEnabled(bool enabled) {
        snapEnabled_ = enabled;
    }

    /** Number of take lanes to show (0 when the clip has fewer than two takes). */
    int laneCount() const;
    /** Preferred total height for `laneCount()` lanes (0 when nothing to show). */
    int preferredHeight() const;

    static constexpr int LANE_HEIGHT = 34;

    // Callbacks (wired by PianoRollContent).
    std::function<void(int takeIndex)> onTakeSelected;
    std::function<void(double startBeat, double endBeat, int takeIndex)> onCompSectionSet;
    std::function<void()> onCompClear;
    std::function<void(int takeIndex)> onDeleteTake;
    std::function<void(int takeIndex)> onTakeHovered;  // -1 = none

  private:
    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;

    double pixelsPerBeat_ = 50.0;
    int scrollOffsetX_ = 0;
    int leftPadding_ = 2;
    int labelGutter_ = 0;
    bool relativeMode_ = false;
    double clipStartBeats_ = 0.0;

    double gridResolutionBeats_ = 0.25;
    bool snapEnabled_ = true;

    PitchFoldMap foldMap_;  // union of all takes' pitches, always folded

    // Swipe state.
    int swipeLane_ = -1;
    int swipeStartX_ = 0;
    int swipeCurrentX_ = 0;
    bool swiping_ = false;
    int hoverLane_ = -1;

    int beatToPixel(double beat) const;
    double pixelToBeat(int x) const;
    double displayBeat(double noteBeat) const;
    int laneAtY(int y) const;
    void rebuildFoldMap();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTakeLanesComponent)
};

}  // namespace magda::daw::ui
