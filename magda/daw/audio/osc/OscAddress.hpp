#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>

namespace magda::osc {

// ============================================================================
// The fixed namespace (#1757)
// ============================================================================

/**
 * @brief MAGDA's built-in OSC address space, parsed into a value type.
 *
 * This is the layer that makes a stock TouchOSC mixer template work with no
 * mapping step: every address below is understood out of the box, so the
 * surface needs no per-control setup and MAGDA needs no learn gesture. Bindings
 * to arbitrary parameters are a separate mechanism layered on top; anything
 * this parser rejects is left for that path rather than being an error.
 *
 * | Address                    | Argument         | Meaning                    |
 * |----------------------------|------------------|----------------------------|
 * | `/magda/transport/play`    | none or non-zero | start playback             |
 * | `/magda/transport/stop`    | none or non-zero | stop playback              |
 * | `/magda/transport/record`  | 0/1, or none     | recording on/off, or flip  |
 * | `/magda/transport/loop`    | 0/1, or none     | loop on/off, or flip       |
 * | `/magda/transport/tempo`   | float BPM        | set tempo                  |
 * | `/magda/transport/position`| float beats      | locate                     |
 * | `/magda/transport/seek`    | float beats      | move by, clamped at zero   |
 * | `/magda/transport/seek/bars`| int bars        | move by, meter-aware       |
 * | `/magda/track/{n}/volume`  | float 0–1        | track fader                |
 * | `/magda/track/{n}/pan`     | float 0–1        | pan, 0.5 centre            |
 * | `/magda/track/{n}/mute`    | 0/1, or none     | mute on/off, or flip       |
 * | `/magda/track/{n}/solo`    | 0/1, or none     | solo on/off, or flip       |
 * | `/magda/track/{n}/send/{m}`| float 0–1        | send level                 |
 * | `/magda/master/volume`     | float 0–1        | master fader               |
 * | `/magda/master/pan`        | float 0–1        | master pan                 |
 * | `/magda/focused/macro/{k}` | float 0–1        | macro of the focused device|
 *
 * `n`, `m` and `k` are 1-based, because that is how surface templates and
 * mixer strips are numbered. `n` is a position in the mixer's visible track
 * order rather than a `TrackId`: IDs survive a reload but are sparse — delete
 * tracks 2 and 3 and the remaining ones are 1, 4, 5 — and a template that
 * addresses eight strips needs eight dense numbers. Resolving a position to a
 * track happens at apply time, on the message thread.
 *
 * Parsing is deliberately strict: a wildcard where the track number belongs
 * is rejected, and so is `/magda/track/03/volume`. OSC address *patterns*
 * are a real part of the protocol, and quietly reading one as an index would
 * mean a surface driving every track at once when its author meant one.
 */

/// Highest 1-based track number the fixed namespace addresses. Positions past
/// this are ignored rather than clamped — clamping would land a fader on track
/// 128 because a template was misconfigured. It also bounds the coalescing
/// table (see `oscSlotIndex`), which is why it is a constant rather than the
/// live track count: an address is valid or not on its own terms, before
/// anyone asks whether that track exists.
inline constexpr int kMaxTrackNumber = 128;

/// Matches `TrackManager::MAX_SENDS_PER_TRACK`, the Tracktion Engine aux limit.
inline constexpr int kMaxSendNumber = 8;

/// Matches `NUM_MACROS`, the macro count on a device's macro array.
inline constexpr int kMaxMacroNumber = 16;

enum class OscCommandKind : std::uint8_t {
    TransportPlay,
    TransportStop,
    TransportRecord,
    TransportLoop,
    TransportTempo,
    TransportPosition,
    TransportSeekBeats,
    TransportSeekBars,
    MasterVolume,
    MasterPan,
    FocusedMacro,
    TrackVolume,
    TrackPan,
    TrackMute,
    TrackSolo,
    TrackSend,
};

/**
 * @brief What a command does with the argument it is sent, if any.
 *
 * The distinction exists because touch surfaces send two different things down
 * one address. A momentary button sends 1 on press and 0 on release; a toggle
 * button sends 1 and 0 as the state itself. Reading both the same way would
 * make releasing the Play button stop the transport.
 */
enum class OscArgKind : std::uint8_t {
    /// Acts on press. No argument, or a non-zero one, fires; a zero argument is
    /// the release half of a momentary button and does nothing.
    Trigger,
    /// A state. 0 or 1 sets it; sending no argument at all flips whatever the
    /// state currently is, so a momentary button works as well as a toggle.
    Toggle,
    /// A float in [0,1], clamped on the way in. What faders, knobs and XY pads
    /// send.
    Normalized,
    /// A float in beats per minute.
    Bpm,
    /// A float position in beats — MAGDA's timeline unit everywhere.
    Beats,
    /// A distance rather than a position: how far to move from wherever the
    /// playhead is. Unlike every other value here it must not be coalesced,
    /// because two rewind presses are two bars and latest-value-wins would
    /// make them one. It rides the ordered ring with the triggers and toggles
    /// for that reason — an edge that happens to carry a magnitude.
    Delta,
};

/**
 * @brief One parsed address from the fixed namespace.
 *
 * `index` is the 1-based track or macro number, `subIndex` the 1-based send
 * number. Both are 0 for kinds that address nothing — transport and master.
 */
struct OscCommand {
    OscCommandKind kind{};
    int index = 0;
    int subIndex = 0;

    bool operator==(const OscCommand&) const = default;
};

/** The argument convention for a kind. Total: every kind has one. */
OscArgKind argKindFor(OscCommandKind kind);

/**
 * @brief Parse a fixed-namespace address, or reject it.
 *
 * Pure, allocation-light and free of any socket or model dependency, so the
 * whole grammar is testable without opening a port.
 *
 * Returns nullopt for anything outside the namespace: a different prefix, an
 * unknown leaf, a wildcard, a non-numeric or out-of-range index, or a trailing
 * component the address does not take. Rejection is not an error — a binding
 * may still match the address.
 */
std::optional<OscCommand> parseOscAddress(juce::StringRef address);

/**
 * @brief The address a command is spelled with. The inverse of the parser.
 *
 * Feedback sends on the address a value is received on (#2091), so the
 * spelling has to come from the same place the grammar does rather than a
 * second list of string literals that could drift from it. `parseOscAddress`
 * of the result is `command` again, for every command the parser can
 * produce -- what the round-trip test asserts over the whole slot space.
 *
 * Called on the message thread, once per address that actually changed, so it
 * builds a `juce::String` rather than writing into a caller's buffer.
 */
juce::String formatOscAddress(const OscCommand& command);

// ============================================================================
// Coalescing slots
// ============================================================================

/**
 * @brief A command's index into the latest-value-wins table.
 *
 * The address space is fixed — bounded by the constants above and independent
 * of how many tracks currently exist — so every command maps to a slot known at
 * compile time. That is what lets the receive thread coalesce without a map,
 * a lock, or an allocation: `/magda/track/3/volume` is slot 3's volume whether
 * or not a third track is there to receive it.
 *
 * Always in `[0, kOscSlotCount)` for a command from `parseOscAddress`.
 */
int oscSlotIndex(const OscCommand& command);

/**
 * @brief The command a slot belongs to — the inverse of `oscSlotIndex`.
 *
 * Arithmetic rather than a stored table, which is what keeps the coalescing
 * table free of any per-slot bookkeeping the receive thread would have to
 * write: it publishes a value and a bit, and the drain recovers the rest.
 *
 * `slot` must be in `[0, kOscSlotCount)`.
 */
OscCommand oscCommandForSlot(int slot);

/// The eight transport kinds and the two master kinds are the first ten
/// entries of `OscCommandKind`, and take the first ten slots in that order.
///
/// The two seek kinds never publish into their slots -- they are `Delta` and
/// go to the ordered ring -- but they keep them anyway, so that "a kind's slot
/// is its ordinal" stays true of the whole enum rather than of most of it.
inline constexpr int kOscUnindexedSlots = 10;
/// Then the focused device's macros, then one block per addressable track:
/// volume, pan, mute, solo, and one slot per send.
inline constexpr int kOscTrackSlotBase = kOscUnindexedSlots + kMaxMacroNumber;
inline constexpr int kOscPerTrackSlots = 4 + kMaxSendNumber;
inline constexpr int kOscSlotCount = kOscTrackSlotBase + (kMaxTrackNumber * kOscPerTrackSlots);

/**
 * @brief The value a slot holds when a `Toggle` arrived with no argument.
 *
 * Toggles otherwise carry 0 or 1, so a negative sentinel cannot collide with a
 * real value, and the drain can tell "set it off" from "flip it" out of a
 * single atomic float.
 */
inline constexpr float kOscToggleRequest = -1.0f;

}  // namespace magda::osc
