#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace magica {

class TrackHeadersPanel : public juce::Component {
public:
    static constexpr int TRACK_HEADER_WIDTH = 200;
    static constexpr int DEFAULT_TRACK_HEIGHT = 80;
    static constexpr int MIN_TRACK_HEIGHT = 75;
    static constexpr int MAX_TRACK_HEIGHT = 200;

    TrackHeadersPanel();
    ~TrackHeadersPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Track management
    void addTrack();
    void removeTrack(int index);
    void selectTrack(int index);
    int getNumTracks() const;
    void setTrackHeight(int trackIndex, int height);
    int getTrackHeight(int trackIndex) const;
    
    // Track properties
    void setTrackName(int trackIndex, const juce::String& name);
    void setTrackMuted(int trackIndex, bool muted);
    void setTrackSolo(int trackIndex, bool solo);
    void setTrackVolume(int trackIndex, float volume);
    void setTrackPan(int trackIndex, float pan);
    
    // Get total height of all tracks
    int getTotalTracksHeight() const;
    
    // Get track Y position
    int getTrackYPosition(int trackIndex) const;
    
    // Callbacks
    std::function<void(int, int)> onTrackHeightChanged;
    std::function<void(int)> onTrackSelected;
    std::function<void(int, const juce::String&)> onTrackNameChanged;
    std::function<void(int, bool)> onTrackMutedChanged;
    std::function<void(int, bool)> onTrackSoloChanged;
    std::function<void(int, float)> onTrackVolumeChanged;
    std::function<void(int, float)> onTrackPanChanged;

private:
    struct TrackHeader {
        juce::String name;
        bool selected = false;
        bool muted = false;
        bool solo = false;
        float volume = 0.8f;
        float pan = 0.0f;
        int height = DEFAULT_TRACK_HEIGHT;
        
        // UI components
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::ToggleButton> muteButton;
        std::unique_ptr<juce::ToggleButton> soloButton;
        std::unique_ptr<juce::Slider> volumeSlider;
        std::unique_ptr<juce::Slider> panSlider;
        
        TrackHeader(const juce::String& trackName);
        ~TrackHeader() = default;
    };
    
    std::vector<std::unique_ptr<TrackHeader>> trackHeaders;
    int selectedTrackIndex = -1;
    
    // Resize functionality
    bool isResizing = false;
    int resizingTrackIndex = -1;
    int resizeStartY = 0;
    int resizeStartHeight = 0;
    static constexpr int RESIZE_HANDLE_HEIGHT = 6;
    
    // Helper methods
    void setupTrackHeader(TrackHeader& header, int trackIndex);
    void paintTrackHeader(juce::Graphics& g, const TrackHeader& header, juce::Rectangle<int> area, bool isSelected);
    void paintResizeHandle(juce::Graphics& g, juce::Rectangle<int> area);
    juce::Rectangle<int> getTrackHeaderArea(int trackIndex) const;
    juce::Rectangle<int> getResizeHandleArea(int trackIndex) const;
    bool isResizeHandleArea(const juce::Point<int>& point, int& trackIndex) const;
    void updateTrackHeaderLayout();
    
    // Mouse handling
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeadersPanel)
};

} // namespace magica 