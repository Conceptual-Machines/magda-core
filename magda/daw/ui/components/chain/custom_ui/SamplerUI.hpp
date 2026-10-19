#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/waveview/WaveformView.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SegmentedChoice.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::audio {
class MagdaSamplerPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Custom inline UI for the Magda Sampler plugin
 *
 * The sample's name, root and load button over two views: the whole recording with its start,
 * end and loop markers and the playhead, or the amp envelope with draggable handles. Beside them,
 * the slice as frame boxes with step arrows, and Pitch and Amp tiles. Expanded into the
 * arrangement, it is the waveform and the loop switch.
 *
 * Parameter indices (matching MagdaSamplerPlugin::addParam order):
 *   0=attack, 1=decay, 2=sustain, 3=release, 4=pitch, 5=fine, 6=level,
 *   7=sampleStart, 8=sampleEnd, 9=loopStart, 10=loopEnd, 11=velAmount
 *
 * Note: loopEnabled is non-automatable state controlled via onLoopEnabledChanged
 * and does not have a parameter index.
 */
class SamplerUI : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropTarget,
                  private juce::Timer {
  public:
    SamplerUI();
    ~SamplerUI() override;

    /**
     * @brief Update all UI controls from device parameters
     */
    void updateParameters(float attack, float decay, float sustain, float release, float pitch,
                          float fine, float level, float sampleStart, float sampleEnd,
                          bool loopEnabled, float loopStart, float loopEnd, float velAmount,
                          const juce::String& sampleName, int rootNote, float voiceMode,
                          float glide);

    /**
     * @brief Callback when a parameter changes (paramIndex, actualValue)
     */
    std::function<void(int paramIndex, float actualValue)> onParameterChanged;

    // Get all linkable sliders for mod/macro wiring (in parameter-index order)
    std::vector<LinkableTextSlider*> getLinkableSliders();

    /**
     * @brief Callback when user requests to load a sample file
     */
    std::function<void()> onLoadSampleRequested;

    /**
     * @brief Callback when a file is dropped onto the sampler UI
     */
    std::function<void(const juce::File&)> onFileDropped;

    /**
     * @brief Callback when loop enabled state changes
     */
    std::function<void(bool)> onLoopEnabledChanged;

    /**
     * @brief Callback when root note changes (MIDI note 0-127)
     */
    std::function<void(int)> onRootNoteChanged;

    /**
     * @brief Callback to read current playback position from plugin
     */
    std::function<double()> getPlaybackPosition;

    /**
     * @brief Set the waveform thumbnail data for display
     */
    void setWaveformData(const juce::AudioBuffer<float>* buffer, double sampleRate,
                         double sampleLengthSeconds);

    bool hasWaveform() const {
        return hasWaveform_;
    }

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // FileDragAndDropTarget — samples dropped from the OS file manager.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    // DragAndDropTarget — samples dragged from MAGDA's own browser arrive as an
    // internal {type:"files"} payload; these forward it to the handlers above.
    // See InternalFileDrag.hpp.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    // Mouse interaction on waveform
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    /// A frame count typed or stepped one frame at a time, as Rhythmica's slice boxes.
    class FrameBox : public juce::Component {
      public:
        FrameBox();
        void setFrames(juce::int64 frames);
        std::function<void(juce::int64 frames)> onCommit;
        void resized() override;

      private:
        void commit();
        juce::TextEditor box_;
        juce::ArrowButton up_, down_;
        juce::int64 frames_ = 0;
    };

    // Timer
    void timerCallback() override;

    juce::Rectangle<int> getWaveformBounds() const;

    // Sample info
    juce::Label sampleNameLabel_;
    bool hasSampleName_ = false;
    std::unique_ptr<magda::SvgButton> loadButton_;
    TextSlider rootNoteSlider_{TextSlider::Format::Decimal};
    juce::Label rootNoteLabel_;

    // Waveform pane: zoom, scroll, markers and drawing live in the SDK view (magda-sdk
    // docs/waveform-view.md); the sliders stay the markers' source of truth.
    bool hasWaveform_ = false;
    double sampleLength_ = 0.0;
    double playheadPosition_ = 0.0;
    std::unique_ptr<sdk::BufferWaveformSource> waveformSource_;
    sdk::WaveformView waveformView_;
    sdk::display::DisplayList waveformList_;
    bool waveformGesture_ = false;

    // Sample start/end / Loop controls
    LinkableTextSlider startSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider endSlider_{TextSlider::Format::Decimal};
    std::unique_ptr<magda::SvgButton> loopButton_;
    LinkableTextSlider loopStartSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider loopEndSlider_{TextSlider::Format::Decimal};
    juce::Label startLabel_, endLabel_, loopStartLabel_, loopEndLabel_;

    // ADSR
    LinkableTextSlider attackSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider decaySlider_{TextSlider::Format::Decimal};
    LinkableTextSlider sustainSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider releaseSlider_{TextSlider::Format::Decimal};

    // Pitch
    LinkableTextSlider pitchSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider fineSlider_{TextSlider::Format::Decimal};

    // Level
    LinkableTextSlider levelSlider_{TextSlider::Format::Decibels};

    // Velocity amount
    LinkableTextSlider velAmountSlider_{TextSlider::Format::Decimal};

    // Voice mode (param 12) lives in the slot footer; this hidden slider carries its links.
    LinkableTextSlider voiceModeSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider glideSlider_{TextSlider::Format::Decimal};
    juce::Label glideLabel_;

    // Labels
    juce::Label attackLabel_, decayLabel_, sustainLabel_, releaseLabel_;
    juce::Label pitchLabel_, fineLabel_, levelLabel_, velAmountLabel_;

    // Waveform gain (driven by level parameter)
    float waveformGain_ = 1.0f;

    /// The sliders' marker values into the view, before it hit-tests or draws.
    void syncWaveformMarkers();
    /// A marker the view moved, out to its slider (and so to the parameter).
    void applyWaveformMarkers(const sdk::WaveformResponse& response);
    sdk::WaveformPointer waveformPointer(const juce::MouseEvent& e) const;
    void updateWaveformCursor(const juce::MouseEvent& e);
    /// Seconds or samples on the ruler, from a right-click on it.
    void showRulerMenu();

    // The amp envelope in its own view: a handle per stage, in this component's coordinates.
    enum class EnvHandle { None, Attack, Decay, Release };
    struct EnvShape {
        juce::Point<float> start, peak, decayEnd, releaseStart, end;
    };
    std::optional<EnvShape> envelopeShape() const;
    EnvHandle envHandleAt(juce::Point<float> position) const;
    void dragEnvelope(EnvHandle handle, juce::Point<float> position);
    EnvHandle envDrag_ = EnvHandle::None;
    juce::Rectangle<float> envelopeArea() const;
    void paintEnvelope(juce::Graphics& g);

    // Which view the plot shows: the recording or the envelope.
    magda::SegmentedChoice viewChoice_;
    bool ampView_ = false;
    bool dropHover_ = false;  ///< A sample is being dragged over the device.
    bool loopView_ = false;   ///< The loop markers come first, and show while the loop is off.
    bool expanded_ = false;   ///< Lent to the arrangement: the waveform and the loop switch.
    std::vector<juce::Component*> hiddenWhileExpanded_;
    void setExpanded(bool expanded);
    bool showsEnvelope() const {
        return ampView_ && !expanded_;
    }
    void showView(int index);

    // The slice in frames, driving the hidden marker sliders.
    double sampleRate_ = 44100.0;
    std::array<FrameBox, 4> frameBoxes_;
    std::array<juce::Label, 4> frameCaptions_;
    void syncFrameBoxes();

    // Areas resized() lays out and paint() draws into.
    juce::Rectangle<int> waveArea_;
    std::array<juce::Rectangle<int>, 3> sectionTitles_;
    std::vector<juce::Rectangle<int>> tiles_;
    juce::Rectangle<int> rootChip_;

    void setupLabel(juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerUI)
};

}  // namespace magda::daw::ui
