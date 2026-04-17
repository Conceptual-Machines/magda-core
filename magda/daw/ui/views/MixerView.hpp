#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "../components/common/MixerDebugPanel.hpp"
#include "../components/common/TextSlider.hpp"
#include "../components/mixer/MasterChannelStrip.hpp"
#include "../components/mixer/RoutingSelector.hpp"
#include "../themes/MixerLookAndFeel.hpp"
#include "../themes/MixerMetrics.hpp"
#include "audio/MidiBridge.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// Forward declarations
class AudioEngine;

/**
 * @brief Mixer view - channel strip mixer interface
 *
 * Shows:
 * - Channel strips for each track with fader, pan, meters
 * - Mute/Solo/Record arm buttons per channel
 * - Master channel on the right
 */
class MixerView : public juce::Component,
                  public juce::DragAndDropTarget,
                  public juce::Timer,
                  public TrackManagerListener,
                  public ViewModeListener,
                  public MidiBridge::Listener {
  public:
    explicit MixerView(AudioEngine* audioEngine = nullptr);
    ~MixerView() override;

    void setAudioEngine(AudioEngine* audioEngine) {
        audioEngine_ = audioEngine;
    }

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
    void midiDeviceListChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(TrackId trackId) override;
    void masterChannelChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

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
        ChannelStrip(const TrackInfo& track, AudioEngine* audioEngine, bool isMaster = false);
        ~ChannelStrip() override;

        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& event) override;

        void setMeterLevel(float level);
        void setMeterLevels(float leftLevel, float rightLevel);
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

        // Callback when send area is resized (triggers relayout of all strips)
        std::function<void()> onSendAreaResized;

      private:
        int trackId_;
        TrackType trackType_;
        bool isMaster_;
        bool isChildTrack_ = false;
        bool selected = false;
        float meterLevel = 0.0f;
        juce::Colour trackColour_;
        juce::String trackName_;

        std::unique_ptr<juce::Label> trackLabel;
        std::unique_ptr<daw::ui::TextSlider> panSlider;
        std::unique_ptr<daw::ui::TextSlider> volumeSlider;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> recordButton;
        std::unique_ptr<juce::TextButton> monitorButton;

        // Routing selectors (toggle + dropdown)
        std::unique_ptr<RoutingSelector> audioInSelector;
        std::unique_ptr<RoutingSelector> audioOutSelector;
        std::unique_ptr<RoutingSelector> midiInSelector;
        std::unique_ptr<RoutingSelector> midiOutSelector;

        // Meter component
        class LevelMeter;
        std::unique_ptr<LevelMeter> levelMeter;
        std::unique_ptr<juce::Label> peakLabel;
        float peakValue_ = 0.0f;

        // Stored bounds for layout regions
        juce::Rectangle<int> faderRegion_;  // Entire fader area (for border)
        juce::Rectangle<int> faderArea_;
        juce::Rectangle<int> meterArea_;

        // dB scale component (ticks + labels between fader and meter)
        class DbScale;
        std::unique_ptr<DbScale> dbScale_;

        // Send slots (dynamic: one per active send on this track)
        struct SendSlot {
            int busIndex;
            std::unique_ptr<juce::Label> nameLabel;
            std::unique_ptr<daw::ui::TextSlider> levelSlider;
            std::unique_ptr<juce::TextButton> removeButton;
        };
        std::vector<std::unique_ptr<SendSlot>> sendSlots_;
        std::unique_ptr<juce::Viewport> sendViewport_;
        std::unique_ptr<juce::Component> sendContainer_;

        // Send area resize handle
        class SendResizeHandle;
        std::unique_ptr<SendResizeHandle> sendResizeHandle_;

        // Expand/collapse toggle (for group tracks with children)
        std::unique_ptr<juce::TextButton> expandToggle_;

        // Group envelope: child strips nested inside this group strip
        std::vector<juce::Component*> groupChildren_;

        friend class MixerView;

        // Audio engine for routing (not owned)
        AudioEngine* audioEngine_ = nullptr;

        // Routing option-to-track mappings (rebuilt when options are populated)
        std::map<int, TrackId> outputTrackMapping_;
        std::map<int, TrackId> midiOutputTrackMapping_;
        std::map<int, TrackId> inputTrackMapping_;

        void setupControls();
        void setupRoutingCallbacks();
        void rebuildSendSlots(const std::vector<SendInfo>& sends);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
    };

    // Channel strips (dynamic based on TrackManager)
    std::vector<std::unique_ptr<ChannelStrip>> channelStrips;
    std::vector<std::unique_ptr<ChannelStrip>> auxChannelStrips;
    std::vector<juce::Component*> orderedStrips_;  // flat layout order for channel container
    std::unique_ptr<MasterChannelStrip> masterStrip;

    // Scrollable area for channels
    std::unique_ptr<juce::Viewport> channelViewport;
    std::unique_ptr<juce::Component> channelContainer;

    // Aux channel container (fixed, not scrollable, between channels and master)
    std::unique_ptr<juce::Component> auxContainer;

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
        bool hasConfirmedHorizontalDrag_ = false;
        int dragStartX_ = 0;
    };
    std::unique_ptr<ChannelResizeHandle> channelResizeHandle_;

    void rebuildChannelStrips();
    void updateStripWidths();
    void relayoutAllStrips();
    bool isResizeDragging_ = false;
    bool pendingResizeUpdate_ = false;
    bool pendingSendResizeUpdate_ = false;

    // Selection state
    int selectedChannelIndex = 0;  // Track index, -1 for no selection
    bool selectedIsMaster = false;

    // View mode state
    ViewMode currentViewMode_ = ViewMode::Mix;

    // Custom look and feel for faders
    MixerLookAndFeel mixerLookAndFeel_;

    // Debug panel for tweaking metrics (F12 to toggle)
    std::unique_ptr<MixerDebugPanel> debugPanel_;

    // Channel resize state
    static constexpr int resizeZoneWidth_ = 6;
    static constexpr int minChannelWidth_ = 80;
    static constexpr int maxChannelWidth_ = 160;
    bool isResizingChannel_ = false;
    int resizeStartX_ = 0;
    int resizeStartWidth_ = 0;

    // Audio engine for metering
    AudioEngine* audioEngine_ = nullptr;

    bool isInChannelResizeZone(const juce::Point<int>& pos) const;

    // Plugin drag-and-drop state
    bool showPluginDropOverlay_ = false;
    int dropTargetStripIndex_ = -1;  // -1 = empty area (new track)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

}  // namespace magda
