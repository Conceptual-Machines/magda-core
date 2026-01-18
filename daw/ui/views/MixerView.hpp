#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace magica {

/**
 * @brief Mixer view - channel strip mixer interface
 *
 * Shows:
 * - Channel strips for each track with fader, pan, meters
 * - Mute/Solo/Record arm buttons per channel
 * - Master channel on the right
 */
class MixerView : public juce::Component, public juce::Timer {
  public:
    MixerView();
    ~MixerView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Timer callback for meter animation
    void timerCallback() override;

  private:
    static constexpr int NUM_CHANNELS = 8;
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
        ChannelStrip(int index, bool isMaster = false);

        void paint(juce::Graphics& g) override;
        void resized() override;

        void setMeterLevel(float level);
        float getMeterLevel() const {
            return meterLevel;
        }

        int getChannelIndex() const {
            return channelIndex;
        }
        bool isMasterChannel() const {
            return isMaster_;
        }

      private:
        int channelIndex;
        bool isMaster_;
        float meterLevel = 0.0f;
        float meterDecay = 0.0f;

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

    // Channel strips
    std::array<std::unique_ptr<ChannelStrip>, NUM_CHANNELS> channelStrips;
    std::unique_ptr<ChannelStrip> masterStrip;

    // Scrollable area for channels
    std::unique_ptr<juce::Viewport> channelViewport;
    std::unique_ptr<juce::Component> channelContainer;

    void setupChannels();
    void simulateMeterLevels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

}  // namespace magica
