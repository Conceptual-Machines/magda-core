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
 * The sample's name, root and load button over four tabs, each a plot and its controls: Sample is
 * the recording with its start, end and loop markers, the playhead and the slice as frame boxes;
 * Pitch, Filter and Amp are that module's envelope with draggable handles beside its tiles.
 * Expanded into the arrangement, it is the waveform and the loop switch.
 *
 * Parameter indices follow MagdaSamplerPlugin::ParamIndex; getLinkableSliders() lists them.
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

    /// The pitch envelope's stages and its depth in semitones (#3096).
    void updatePitchEnvelope(float attack, float decay, float sustain, float release, float amount);

    /// The filter's slots from MagdaSamplerPlugin::kFilterType to kFilterEnvAmount, in that order
    /// (#3097).
    static constexpr int kFilterSlotCount = 12;
    void updateFilter(const std::array<float, kFilterSlotCount>& values);

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

    // Pitch envelope: its stages share the amp stages' tiles, shown in the Pitch view.
    LinkableTextSlider pitchAttackSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider pitchDecaySlider_{TextSlider::Format::Decimal};
    LinkableTextSlider pitchSustainSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider pitchReleaseSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider pitchEnvAmountSlider_{TextSlider::Format::Decimal};
    juce::Label pitchEnvAmountLabel_;

    // Filter: its own column, and its envelope's stages in the amp stages' tiles in the Filter
    // view. In MagdaSamplerPlugin::ParamIndex order from kFilterType.
    LinkableTextSlider filterTypeSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterModeSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterPolesSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterCutoffSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterResonanceSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterKeyTrackSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterVelocitySlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterAttackSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterDecaySlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterSustainSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterReleaseSlider_{TextSlider::Format::Decimal};
    LinkableTextSlider filterEnvAmountSlider_{TextSlider::Format::Decimal};
    std::array<LinkableTextSlider*, kFilterSlotCount> filterSliders();
    // Type, mode and poles are picked here; their sliders above stay hidden and carry the links.
    juce::ComboBox filterTypeBox_;
    magda::SegmentedChoice filterModeChoice_;
    magda::SegmentedChoice filterPolesChoice_;
    juce::Label filterModeLabel_, filterPolesLabel_, filterCutoffLabel_, filterResonanceLabel_,
        filterKeyTrackLabel_, filterVelocityLabel_, filterEnvAmountLabel_;
    /// Dim what the chosen type does not read, and show the mode it plays.
    void refreshFilterTiles();

    enum class View { Sample, Pitch, Filter, Amp };
    View view_ = View::Sample;
    /// What a tab shows beside its plot.
    std::vector<juce::Component*> controlsFor(View view);

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

    // The amp or pitch envelope in its own view: a handle per stage, in this component's
    // coordinates.
    enum class EnvHandle { None, Attack, Decay, Release };
    struct EnvSliders {
        LinkableTextSlider *attack, *decay, *sustain, *release;
    };
    EnvSliders envSliders();
    /// A negative pitch envelope bends down, so its shape hangs from the top.
    bool envelopeInverted() const;
    float envelopeLevelAt(float y) const;
    struct EnvShape {
        juce::Point<float> start, peak, decayEnd, releaseStart, end;
    };
    std::optional<EnvShape> envelopeShape();
    EnvHandle envHandleAt(juce::Point<float> position);
    void dragEnvelope(EnvHandle handle, juce::Point<float> position);
    EnvHandle envDrag_ = EnvHandle::None;
    juce::Rectangle<float> envelopeArea() const;
    void paintEnvelope(juce::Graphics& g);

    // Which tab shows: the recording or a module's envelope, each with its controls.
    magda::SegmentedChoice viewChoice_;
    bool ampView_ = false;
    bool pitchView_ = false;
    bool filterView_ = false;
    bool dropHover_ = false;  ///< A sample is being dragged over the device.
    bool expanded_ = false;   ///< Lent to the arrangement: the waveform and the loop switch.
    std::vector<juce::Component*> hiddenWhileExpanded_;
    void setExpanded(bool expanded);
    bool showsEnvelope() const {
        return (ampView_ || pitchView_ || filterView_) && !expanded_;
    }
    void showView(int index);

    // The slice in frames, driving the hidden marker sliders.
    double sampleRate_ = 44100.0;
    std::array<FrameBox, 4> frameBoxes_;
    std::array<juce::Label, 4> frameCaptions_;
    void syncFrameBoxes();
    /// LOOP IN and LOOP OUT greyed and locked while the loop is off.
    void lockLoopBoxes();

    // Areas resized() lays out and paint() draws into.
    juce::Rectangle<int> waveArea_;
    std::array<juce::Rectangle<int>, 2> sectionTitles_;
    std::vector<juce::Rectangle<int>> tiles_;
    juce::Rectangle<int> rootChip_;

    void setupLabel(juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerUI)
};

}  // namespace magda::daw::ui
