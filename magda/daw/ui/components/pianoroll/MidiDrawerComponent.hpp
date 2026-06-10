#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/ClipTypes.hpp"

namespace magda {

class VelocityLaneComponent;
class CCLaneComponent;

/**
 * @brief Drawer container for MIDI editor lanes (velocity, CC, pitchbend)
 *
 * Lanes are stacked vertically (like automation lanes), all visible at once:
 * - Permanent velocity lane at the top (VelocityLaneComponent)
 * - Added CC/Pitchbend lanes below it
 * Each lane is identified by its control name in the left margin (keyboard
 * column), with a close button on removable lanes and a "+" button at the
 * bottom to add CC or Pitchbend lanes.
 * Forwards clip/zoom/scroll to all lanes.
 */
class MidiDrawerComponent : public juce::Component {
  public:
    MidiDrawerComponent();
    ~MidiDrawerComponent() override;

    // Set the clip to display/edit
    void setClip(ClipId clipId);
    void setClipIds(const std::vector<ClipId>& clipIds);
    ClipId getClipId() const {
        return clipId_;
    }

    // Zoom and scroll settings
    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int offsetX);
    void setLeftPadding(int padding);

    // Display mode
    void setRelativeMode(bool relative);
    void setClipStartBeats(double startBeats);
    void setClipLengthBeats(double lengthBeats);

    // Loop region
    void setLoopRegion(double offsetBeats, double lengthBeats, bool enabled);

    // Refresh all lanes
    void refreshAll();

    // Access to velocity lane for note preview/selection sync
    VelocityLaneComponent* getVelocityLane() const {
        return velocityLane_.get();
    }

    // Left margin (for keyboard/sidebar column that's part of our bounds)
    void setLeftMargin(int margin) {
        leftMargin_ = margin;
    }

    // Layout
    static constexpr int ADD_BUTTON_HEIGHT = 20;
    static constexpr int PREFERRED_LANE_HEIGHT = 80;

    // Show the add-lane menu (also reachable from the editor sidebar's CC button)
    void showAddLaneMenu();

    // True when any CC/pitchbend lane is open (beyond the permanent velocity lane)
    bool hasExtraLanes() const {
        return !ccTabs_.empty();
    }

    // Notified whenever CC/pitchbend lanes are added or removed
    std::function<void()> onLanesChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Callback when the user drags the top resize handle (desired new height)
    std::function<void(int newHeight)> onResizeDrag;

    // Callbacks for undo integration (set by MidiEditorContent)
    // Velocity callbacks are set directly on the VelocityLaneComponent
    // CC/PB callbacks set on each CCLaneComponent instance

  private:
    // Tab info
    struct TabInfo {
        juce::String name;
        bool isPitchBend = false;
        int ccNumber = -1;  // -1 for velocity, >=0 for CC
        bool isVelocity = false;
        std::unique_ptr<CCLaneComponent> ccLane;
    };

    int leftMargin_ = 0;

    ClipId clipId_ = INVALID_CLIP_ID;
    std::vector<ClipId> clipIds_;
    double pixelsPerBeat_ = 50.0;
    int scrollOffsetX_ = 0;
    int leftPadding_ = 2;
    bool relativeMode_ = true;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;
    double loopOffsetBeats_ = 0.0;
    double loopLengthBeats_ = 0.0;
    bool loopEnabled_ = false;

    // Components
    std::unique_ptr<VelocityLaneComponent> velocityLane_;
    std::vector<TabInfo> ccTabs_;  // Additional CC/PB lanes

    // Lane header column (left margin: control name, close button, "+" button)
    void paintLaneHeaders(juce::Graphics& g);
    void mouseDown(const juce::MouseEvent& e) override;

    // Stacked lane rows (full component coords)
    int getLaneCount() const {
        return 1 + static_cast<int>(ccTabs_.size());
    }
    juce::Rectangle<int> getLaneRowBounds(int laneIndex) const;

    // Lane management
    void addCCTab(int ccNumber);
    void addPitchBendTab();
    void removeTab(int tabIndex);
    void growDrawerForLanes();

    // Resize handle
    static constexpr int RESIZE_HANDLE_HEIGHT = 4;
    bool isResizing_ = false;
    int resizeStartHeight_ = 0;

    // Forward settings to a CC lane
    void syncSettingsToCCLane(CCLaneComponent* lane);

    void paintOverChildren(juce::Graphics& g) override;

    // Pitch bend range editor
    std::unique_ptr<juce::Label> pbRangeLabel_;
    void updatePbRangeVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDrawerComponent)
};

}  // namespace magda
