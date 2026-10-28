#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

#include "core/MpeWire.hpp"

namespace magda::daw::audio {

/**
 * @brief Each MIDI channel's MPE dimensions as the wire last left them (#1569).
 *
 * A voice takes its channel's values at note-on and follows them while it sounds, so one
 * note's glide moves only that note. Channel 1's wheel is not tracked: it is the ordinary
 * wheel, and each device scales it by its own Bend Range.
 */
class MpeChannelState {
  public:
    struct Dimensions {
        float semitones = 0.0f;
        float timbre = static_cast<float>(mpe::valueForWire(mpe::kTimbreRest));
        float pressure = static_cast<float>(mpe::valueForWire(mpe::kPressureRest));
    };

    /// Folds @p m in. Returns the 1-based channel it moved, or 0 when it carries no dimension.
    int apply(const juce::MidiMessage& m) {
        const int channel = m.getChannel();
        if (channel < 1 || channel > 16)
            return 0;
        auto& dims = channels_[static_cast<size_t>(channel - 1)];
        if (m.isPitchWheel() && mpe::isMemberChannel(channel))
            dims.semitones = static_cast<float>(mpe::semitonesForWheel(m.getPitchWheelValue()));
        else if (m.isControllerOfType(mpe::kTimbreController))
            dims.timbre = static_cast<float>(mpe::valueForWire(m.getControllerValue()));
        else if (m.isChannelPressure())
            dims.pressure = static_cast<float>(mpe::valueForWire(m.getChannelPressureValue()));
        else
            return 0;
        return channel;
    }

    const Dimensions& operator[](int channel) const {
        return channels_[static_cast<size_t>(juce::jlimit(1, 16, channel) - 1)];
    }

    void reset() {
        channels_.fill({});
    }

  private:
    std::array<Dimensions, 16> channels_{};
};

}  // namespace magda::daw::audio
