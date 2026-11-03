// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/waveview/WaveformView.hpp"

namespace magda::daw::ui {

/**
 * @brief A sample's waveform with its start, end and loop markers, playhead, ruler and zoom
 *        (#3100). Zoom, scroll, markers and drawing live in the SDK view (magda-sdk
 *        docs/waveform-view.md).
 *
 * The markers are the owner's: the pane asks for them before it hit-tests or draws, and hands a
 * drag's moves back, so sliders or parameters stay their source of truth. Empty, it is a well a
 * click on asks the owner to load into.
 *
 * A sample that is a whole audio file draws as audio clips do, every channel from the file; the
 * view's own wave is the fallback for anything else, such as a zone cut from an SF2.
 */
class SamplerWaveformPane : public juce::Component, private juce::ChangeListener {
  public:
    SamplerWaveformPane();
    ~SamplerWaveformPane() override;

    /// @p buffer's first channel, @p lengthSeconds long; null for an empty well. The buffer is
    /// the caller's and has to outlive what the pane shows. @p sourceFile is where it came from.
    void setAudio(const juce::AudioBuffer<float>* buffer, double lengthSeconds,
                  const juce::String& sourceFile = {});
    bool hasAudio() const {
        return source_ != nullptr;
    }
    void setPlayhead(double seconds);
    void setGain(float gain);

    /// The markers as they are now, asked before each hit-test and paint.
    std::function<sdk::WaveformMarkers()> markers;
    /// A drag moved the markers to these.
    std::function<void(const sdk::WaveformMarkers&)> onMarkersMoved;
    /// The empty well was clicked.
    std::function<void()> onEmptyClicked;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    void pullMarkers();
    void apply(const sdk::WaveformResponse& response);
    sdk::WaveformPointer pointer(const juce::MouseEvent& e) const;
    void updateCursor(const juce::MouseEvent& e);
    /// Seconds or samples on the ruler, from a right-click on it.
    void showRulerMenu();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void setClipFile(const juce::String& file);
    void paintClipWave(juce::Graphics& g);

    /// What the clip wave was rendered for.
    struct ClipWaveKey {
        double start = 0.0;
        double end = 0.0;
        int width = 0;
        int height = 0;
        float gain = 1.0f;
        float scale = 1.0f;
        bool operator==(const ClipWaveKey&) const = default;
    };

    std::unique_ptr<sdk::BufferWaveformSource> source_;
    sdk::WaveformView view_;
    sdk::display::DisplayList list_;
    sdk::display::DisplayList chromeList_;
    juce::String clipFile_;  ///< Set, the wave draws as audio clips do.
    // The clip renderer reads the file, and the playhead repaints the pane on every tick.
    juce::Image clipWave_;
    ClipWaveKey clipWaveKey_;
    float gain_ = 1.0f;
    double length_ = 0.0;
    double playhead_ = 0.0;
    bool gesture_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerWaveformPane)
};

}  // namespace magda::daw::ui
