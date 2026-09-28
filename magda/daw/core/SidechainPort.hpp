#pragma once

namespace magda {

/**
 * @brief What a device declares on its sidechain slot (#2329).
 *
 * Declared by the device, never inferred from a channel count: more inputs than
 * outputs is not by itself a request for a key, and a host that guessed one had
 * no way to know how wide it was. A device's processor projects this onto
 * DeviceInfo with the rest of the device's facts, so the menu that offers a
 * source, the plan that wires it and the API that reports it read one
 * declaration.
 */
struct SidechainPort {
    enum class Kind {
        None,
        /// Audio, on the device's own port rather than as further channels of
        /// the buffer it processes.
        Audio,
        /// MIDI from another track, which reaches the device on its MIDI input:
        /// the plan fills that slot from the source instead of from the chain
        /// (ChainRoutingModel).
        MIDI,
    };

    Kind kind = Kind::None;
    /// Channels an audio key carries. Zero for every other kind.
    int channels = 0;

    bool takesAudio() const {
        return kind == Kind::Audio && channels > 0;
    }
    bool declared() const {
        return kind != Kind::None;
    }

    bool operator==(const SidechainPort&) const = default;
};

/// The audio key most dynamics devices ask for.
inline constexpr SidechainPort monoAudioSidechain{.kind = SidechainPort::Kind::Audio,
                                                  .channels = 1};

}  // namespace magda
