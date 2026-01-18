#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/TrackManager.hpp"

namespace magica {

/**
 * @brief Mixer view - channel strip mixer interface
 *
 * Shows:
 * - Channel strips for each track with fader, pan, meters
 * - Mute/Solo/Record arm buttons per channel
 * - Master channel on the right
 */
class MixerView : public juce::Component, public juce::Timer, public TrackManagerListener {
  public:
    MixerView();
    ~MixerView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Timer callback for meter animation
    void timerCallback() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;

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
    static constexpr int CHANNEL_WIDTH = 80;
    static constexpr int MASTER_WIDTH = 100;
    static constexpr int FADER_HEIGHT = 200;
    static constexpr int METER_WIDTH = 12;
    static constexpr int BUTTON_SIZE = 24;
    static constexpr int KNOB_SIZE = 40;
    static constexpr int HEADER_HEIGHT = 30;

    // Channel strip component
    class ChannelStrip : public juce::Component {
      public:
        ChannelStrip(const TrackInfo& track, bool isMaster = false);

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

        std::unique_ptr<juce::Label> trackLabel;
        std::unique_ptr<juce::Slider> panKnob;
        std::unique_ptr<juce::Slider> volumeFader;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> recordButton;

        // Meter component
        class LevelMeter;
        std::unique_ptr<LevelMeter> levelMeter;

        void setupControls();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
    };

    // Channel strips (dynamic based on TrackManager)
    std::vector<std::unique_ptr<ChannelStrip>> channelStrips;
    std::unique_ptr<ChannelStrip> masterStrip;

    // Scrollable area for channels
    std::unique_ptr<juce::Viewport> channelViewport;
    std::unique_ptr<juce::Component> channelContainer;

    void rebuildChannelStrips();
    void simulateMeterLevels();

    // Selection state
    int selectedChannelIndex = 0;  // Track index, -1 for no selection
    bool selectedIsMaster = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

}  // namespace magica
