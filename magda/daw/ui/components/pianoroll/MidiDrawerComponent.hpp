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
 * @brief Tabbed drawer container for MIDI editor lanes (velocity, CC, pitchbend)
 *
 * Provides a tab bar with:
 * - Permanent "Vel" tab (VelocityLaneComponent)
 * - "+" button to add CC or Pitchbend tabs
 * - Close button on added tabs
 * - Forwards clip/zoom/scroll to the active lane
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

    // Get the active lane name for header display
    juce::String getActiveTabName() const;

    // Left margin (for keyboard/sidebar column that's part of our bounds)
    void setLeftMargin(int margin) {
        leftMargin_ = margin;
    }

    // Layout
    static constexpr int TAB_BAR_HEIGHT = 26;

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
    std::vector<TabInfo> ccTabs_;  // Additional CC/PB tabs
    int activeTabIndex_ = 0;       // 0 = velocity, 1+ = ccTabs_[index-1]

    // Tab bar
    void paintTabBar(juce::Graphics& g, juce::Rectangle<int> area);
    void mouseDown(const juce::MouseEvent& e) override;

    // Tab management
    void addCCTab(int ccNumber);
    void addPitchBendTab();
    void removeTab(int tabIndex);
    void setActiveTab(int tabIndex);
    void showAddTabMenu();

    // Resize handle
    static constexpr int RESIZE_HANDLE_HEIGHT = 4;
    bool isResizing_ = false;
    int resizeStartHeight_ = 0;

    // Update visibility of lanes based on active tab
    void updateLaneVisibility();
    // Forward settings to a CC lane
    void syncSettingsToCCLane(CCLaneComponent* lane);

    // Pitch bend range editor
    std::unique_ptr<juce::Label> pbRangeLabel_;
    void updatePbRangeVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDrawerComponent)
};

}  // namespace magda
