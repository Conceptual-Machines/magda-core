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
 */
class SamplerWaveformPane : public juce::Component {
  public:
    SamplerWaveformPane();

    /// @p buffer's first channel, @p lengthSeconds long; null for an empty well. The buffer is
    /// the caller's and has to outlive what the pane shows.
    void setAudio(const juce::AudioBuffer<float>* buffer, double lengthSeconds);
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

    std::unique_ptr<sdk::BufferWaveformSource> source_;
    sdk::WaveformView view_;
    sdk::display::DisplayList list_;
    double length_ = 0.0;
    double playhead_ = 0.0;
    bool gesture_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerWaveformPane)
};

}  // namespace magda::daw::ui
