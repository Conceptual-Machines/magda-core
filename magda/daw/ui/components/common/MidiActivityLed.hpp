#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TrackInfo.hpp"

namespace magda::daw::ui {

/** @brief True when a note on @p track should light a MIDI activity indicator: the track
 *  takes live input and its monitor mode passes it at the current transport state. */
bool showsLiveMidiActivity(const magda::TrackInfo& track, bool playing);

/** @brief A 7px LED over a small "MIDI" label that flashes green on a live note, holds
 *  briefly, then fades, so fast bursts stay visible. */
class MidiActivityLed : public juce::Component, private juce::Timer {
  public:
    MidiActivityLed();
    ~MidiActivityLed() override;

    /** Watches @p trackId, skipping any activity counted before the switch. */
    void setTrack(magda::TrackId trackId);

    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;

  private:
    void timerCallback() override;

    magda::TrackId trackId_ = magda::INVALID_TRACK_ID;
    uint32_t lastCounter_ = 0;
    float level_ = 0.0f;
    int holdFrames_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiActivityLed)
};

}  // namespace magda::daw::ui
