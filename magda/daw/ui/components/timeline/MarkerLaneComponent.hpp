#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../utils/ScopedListener.hpp"
#include "../../state/TimelineController.hpp"

namespace magda {

class MarkerLaneComponent : public juce::Component, public TimelineStateListener {
  public:
    MarkerLaneComponent();
    ~MarkerLaneComponent() override;

    void setController(TimelineController* controller);

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

  private:
    ScopedListener<TimelineController, TimelineStateListener> timelineListener_{this};

    std::vector<TimelineMarker> markers_;
    double pixelsPerBeat_ = 10.0;
    double timelineLengthBeats_ = 0.0;
    int selectedMarkerId_ = 0;
    int hoveredMarkerId_ = 0;

    int markerToX(const TimelineMarker& marker) const;
    int markerAt(juce::Point<int> point) const;
    const TimelineMarker* findMarker(int markerId) const;
    void showMarkerMenu(int markerId, juce::Point<int> screenPosition);
    void showLaneMenu(juce::Point<int> screenPosition);
    void showRenameMarkerDialog(int markerId, const TimelineMarker& marker);
    void showEditPositionDialog(int markerId, const TimelineMarker& marker);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerLaneComponent)
};

}  // namespace magda
