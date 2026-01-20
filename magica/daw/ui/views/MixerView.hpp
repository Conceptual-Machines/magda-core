#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "../components/common/MixerDebugPanel.hpp"
#include "../components/mixer/MasterChannelStrip.hpp"
#include "../themes/MixerLookAndFeel.hpp"
#include "../themes/MixerMetrics.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magica {

/**
 * @brief Mixer view - channel strip mixer interface
 *
 * Shows:
 * - Channel strips for each track with fader, pan, meters
 * - Mute/Solo/Record arm buttons per channel
 * - Master channel on the right
 */
class MixerView : public juce::Component,
                  public juce::Timer,
                  public TrackManagerListener,
                  public ViewModeListener {
  public:
    MixerView();
    ~MixerView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // Timer callback for meter animation
    void timerCallback() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void masterChannelChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // Selection
    void selectChannel(int index, bool isMaster = false);
    int getSelectedChannel() const {
        return selectedChannelIndex;
    }
    bool isSelectedMaster() const {
        return selectedIsMaster;
    }

    // Callback when channel selection changes (index, isMaster)
    std::function<void(int, bool)> onChannelSelected;

  private:
    // Channel strip component
    class ChannelStrip : public juce::Component {
      public:
        ChannelStrip(const TrackInfo& track, juce::LookAndFeel* faderLookAndFeel,
                     bool isMaster = false);
        ~ChannelStrip() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& event) override;

        void setMeterLevel(float level);
        float getMeterLevel() const {
            return meterLevel;
        }

        void setSelected(bool shouldBeSelected);
        bool isSelected() const {
            return selected;
        }

        int getTrackId() const {
            return trackId_;
        }
        bool isMasterChannel() const {
            return isMaster_;
        }

        // Update from track info
        void updateFromTrack(const TrackInfo& track);

        // Callback when channel is clicked
        std::function<void(int trackId, bool isMaster)> onClicked;

      private:
        int trackId_;
        bool isMaster_;
        bool selected = false;
        float meterLevel = 0.0f;
        juce::Colour trackColour_;
        juce::String trackName_;
        juce::LookAndFeel* faderLookAndFeel_ = nullptr;

        std::unique_ptr<juce::Label> trackLabel;
        std::unique_ptr<juce::Slider> panKnob;
        std::unique_ptr<juce::Label> panValueLabel;
        std::unique_ptr<juce::Slider> volumeFader;
        std::unique_ptr<juce::Label> faderValueLabel;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> recordButton;

        // Meter component
        class LevelMeter;
        std::unique_ptr<LevelMeter> levelMeter;
        std::unique_ptr<juce::Label> peakLabel;
        float peakValue_ = 0.0f;

        // Stored bounds for layout regions
        // Layout: [fader] [leftTicks] [labels] [rightTicks] [meter]
        juce::Rectangle<int> faderRegion_;  // Entire fader area (for border)
        juce::Rectangle<int> faderArea_;
        juce::Rectangle<int> leftTickArea_;
        juce::Rectangle<int> labelArea_;
        juce::Rectangle<int> rightTickArea_;
        juce::Rectangle<int> meterArea_;

        void setupControls();
        void drawDbLabels(juce::Graphics& g);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
    };

    // Channel strips (dynamic based on TrackManager)
    std::vector<std::unique_ptr<ChannelStrip>> channelStrips;
    std::unique_ptr<MasterChannelStrip> masterStrip;

    // Scrollable area for channels
    std::unique_ptr<juce::Viewport> channelViewport;
    std::unique_ptr<juce::Component> channelContainer;

    // Resize handle for channel width
    class ChannelResizeHandle : public juce::Component {
      public:
        ChannelResizeHandle();
        void paint(juce::Graphics& g) override;
        void mouseEnter(const juce::MouseEvent& event) override;
        void mouseExit(const juce::MouseEvent& event) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

        std::function<void(int deltaX)> onResize;
        std::function<void()> onResizeEnd;

      private:
        bool isHovering_ = false;
        bool isDragging_ = false;
        int dragStartX_ = 0;
    };
    std::unique_ptr<ChannelResizeHandle> channelResizeHandle_;

    void rebuildChannelStrips();

    // Selection state
    int selectedChannelIndex = 0;  // Track index, -1 for no selection
    bool selectedIsMaster = false;

    // View mode state
    ViewMode currentViewMode_ = ViewMode::Mix;

    // Custom look and feel for faders
    MixerLookAndFeel mixerLookAndFeel_;

    // Debug panel for tweaking metrics
    std::unique_ptr<MixerDebugPanel> debugPanel_;

    // Channel resize state
    static constexpr int resizeZoneWidth_ = 6;
    static constexpr int minChannelWidth_ = 80;
    static constexpr int maxChannelWidth_ = 200;
    bool isResizingChannel_ = false;
    int resizeStartX_ = 0;
    int resizeStartWidth_ = 0;

    bool isInChannelResizeZone(const juce::Point<int>& pos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

}  // namespace magica
