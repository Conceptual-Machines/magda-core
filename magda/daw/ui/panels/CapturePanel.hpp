#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <optional>

#include "../../engine/AudioEngine.hpp"
#include "../components/common/PreviewOutputCallback.hpp"
#include "../components/common/SvgButton.hpp"
#include "../components/timeline/TimeRuler.hpp"
#include "../components/timeline/ZoomScrollBar.hpp"
#include "core/GestureRouter.hpp"

namespace magda {

/**
 * @brief What was heard recently, under the transport, to import by selection (#3053).
 *
 * Two lanes over the capture buffer as it was when the panel opened: the master's
 * audio and the notes played on MIDI devices. A TimeRuler and a ZoomScrollBar zoom
 * and scroll the view as they do in the waveform editor, a drag selects, and the
 * selection's audio can be played before it is imported.
 */
class CapturePanel : public juce::Component, private juce::Timer {
  public:
    explicit CapturePanel(AudioEngine& engine);
    ~CapturePanel() override;

    static constexpr int kHeight = 132;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    struct Range {
        std::int64_t first = 0;
        std::int64_t end = 0;

        std::int64_t length() const {
            return end - first;
        }
    };

    void timerCallback() override;
    void snapshot();
    void refresh();
    void fetchPeaks();
    void keep(bool audio, bool midi);
    void togglePlayback();
    void stopPlayback();

    /// Zoom to @p pixelsPerSecond keeping the time under @p anchorX (lane-local) still.
    void setZoom(double pixelsPerSecond, int anchorX);
    void setScroll(double pixels);
    void fitWindow();
    /// Push the view to the ruler, the scroll bar and the peaks.
    void syncView();
    double windowSeconds() const;
    double contentWidth() const;

    Range window() const;
    std::optional<Range> justPlayed() const;
    std::int64_t frameAtX(int x) const;
    float xAtFrame(std::int64_t frame) const;

    juce::Rectangle<int> lanesArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> scrollBarArea() const;
    void paintLanes(juce::Graphics& g) const;

    AudioEngine& engine_;
    RecentCaptureMeter meter_;
    Range view_;
    std::vector<float> viewPeaks_;

    /// Frames, so it stays on what was heard however the view moves.
    std::optional<Range> selection_;
    std::int64_t dragAnchor_ = 0;

    // The view, as the waveform editor keeps it: a zoom, and a scroll into the buffer.
    double pixelsPerSecond_ = 1.0;
    double scrollX_ = 0.0;
    bool fitted_ = true;  // showing the whole buffer, until zoomed or scrolled
    TimeRuler ruler_;
    ZoomScrollBar scrollBar_{ZoomScrollBar::Orientation::Horizontal};

    std::unique_ptr<SvgButton> playButton_;
    std::unique_ptr<SvgButton> keepAudioButton_;
    std::unique_ptr<SvgButton> keepMidiButton_;
    juce::String status_;

    // Preview of the selection's audio, out of the preview output pair.
    juce::AudioBuffer<float> playBuffer_;
    std::unique_ptr<juce::MemoryAudioSource> playSource_;
    juce::AudioTransportSource transport_;
    juce::AudioSourcePlayer player_;
    PreviewOutputCallback output_;
    std::int64_t playFrom_ = 0;
    bool outputAttached_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CapturePanel)
};

}  // namespace magda
