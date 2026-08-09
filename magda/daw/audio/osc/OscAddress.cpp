#include "osc/OscAddress.hpp"

namespace magda::osc {

namespace {

/// The deepest address in the namespace is `/magda/track/{n}/send/{m}`.
constexpr int kMaxSegments = 5;

/**
 * @brief One '/'-delimited component, pointing into the caller's address.
 *
 * A view rather than a copy: parsing runs on the receive thread for every
 * datagram, and splitting into `juce::String`s would allocate once per
 * component per message — a per-packet cost paid to look at a handful of bytes.
 */
struct Segment {
    const char* data = nullptr;
    int length = 0;

    bool equals(const char* literal) const {
        int i = 0;
        for (; i < length; ++i)
            if (literal[i] == '\0' || literal[i] != data[i])
                return false;
        return literal[i] == '\0';
    }
};

/**
 * @brief Split an address into its components without allocating.
 *
 * @return the number of components, -1 when the address is not well formed
 *         (OSC addresses begin with '/'), or a count above `kMaxSegments` when
 *         it is deeper than anything in the namespace.
 *
 * Bytes are compared raw. A multi-byte UTF-8 sequence matches no literal here
 * and parses as no digit, which is the answer we want for it anyway.
 */
int splitAddress(juce::StringRef address, Segment* out) {
    const char* cursor = address.text.getAddress();
    if (cursor == nullptr || *cursor != '/')
        return -1;
    ++cursor;

    int count = 0;
    const char* segmentStart = cursor;
    for (;; ++cursor) {
        if (*cursor != '/' && *cursor != '\0')
            continue;
        if (count == kMaxSegments)
            return kMaxSegments + 1;
        out[count].data = segmentStart;
        out[count].length = static_cast<int>(cursor - segmentStart);
        ++count;
        if (*cursor == '\0')
            return count;
        segmentStart = cursor + 1;
    }
}

/**
 * @brief Parse a strictly decimal 1-based index, or 0 if it is not one.
 *
 * Deliberately not `String::getIntValue`, which answers 0 for "abc", 3 for
 * "3x" and 3 for "3.7" — every one of which would turn a malformed or
 * pattern-bearing address into a plausible-looking command. Leading zeros are
 * rejected too, so one address means one thing: "/magda/track/03/volume" is not
 * a second spelling of track 3.
 */
int parseIndex(const Segment& token, int maxValue) {
    if (token.length <= 0 || token.length > 3)
        return 0;
    if (token.data[0] == '0')  // covers "0" itself and any leading-zero spelling
        return 0;

    int value = 0;
    for (int i = 0; i < token.length; ++i) {
        const char c = token.data[i];
        if (c < '0' || c > '9')
            return 0;
        value = (value * 10) + (c - '0');
    }
    return value <= maxValue ? value : 0;
}

OscCommand unindexed(OscCommandKind kind) {
    return OscCommand{kind, 0, 0};
}

}  // namespace

// ============================================================================
// Argument conventions
// ============================================================================

OscArgKind argKindFor(OscCommandKind kind) {
    switch (kind) {
        case OscCommandKind::TransportPlay:
        case OscCommandKind::TransportStop:
            return OscArgKind::Trigger;
        case OscCommandKind::TransportRecord:
        case OscCommandKind::TransportLoop:
        case OscCommandKind::TrackMute:
        case OscCommandKind::TrackSolo:
            return OscArgKind::Toggle;
        case OscCommandKind::TransportTempo:
            return OscArgKind::Bpm;
        case OscCommandKind::TransportPosition:
            return OscArgKind::Beats;
        case OscCommandKind::MasterVolume:
        case OscCommandKind::MasterPan:
        case OscCommandKind::FocusedMacro:
        case OscCommandKind::TrackVolume:
        case OscCommandKind::TrackPan:
        case OscCommandKind::TrackSend:
            return OscArgKind::Normalized;
    }
    return OscArgKind::Normalized;
}

// ============================================================================
// Parsing
// ============================================================================

std::optional<OscCommand> parseOscAddress(juce::StringRef address) {
    Segment parts[kMaxSegments];
    const int count = splitAddress(address, parts);
    if (count < 2 || count > kMaxSegments)
        return std::nullopt;
    if (!parts[0].equals("magda"))
        return std::nullopt;

    const auto& section = parts[1];

    if (section.equals("transport")) {
        if (count != 3)
            return std::nullopt;
        const auto& leaf = parts[2];
        if (leaf.equals("play"))
            return unindexed(OscCommandKind::TransportPlay);
        if (leaf.equals("stop"))
            return unindexed(OscCommandKind::TransportStop);
        if (leaf.equals("record"))
            return unindexed(OscCommandKind::TransportRecord);
        if (leaf.equals("loop"))
            return unindexed(OscCommandKind::TransportLoop);
        if (leaf.equals("tempo"))
            return unindexed(OscCommandKind::TransportTempo);
        if (leaf.equals("position"))
            return unindexed(OscCommandKind::TransportPosition);
        return std::nullopt;
    }

    if (section.equals("master")) {
        if (count != 3)
            return std::nullopt;
        if (parts[2].equals("volume"))
            return unindexed(OscCommandKind::MasterVolume);
        if (parts[2].equals("pan"))
            return unindexed(OscCommandKind::MasterPan);
        return std::nullopt;
    }

    if (section.equals("focused")) {
        // Only macros today. A second focused address would branch here.
        if (count != 4 || !parts[2].equals("macro"))
            return std::nullopt;
        const int macro = parseIndex(parts[3], kMaxMacroNumber);
        return macro > 0 ? std::optional(OscCommand{OscCommandKind::FocusedMacro, macro, 0})
                         : std::nullopt;
    }

    if (section.equals("track")) {
        if (count < 4)
            return std::nullopt;
        const int track = parseIndex(parts[2], kMaxTrackNumber);
        if (track == 0)
            return std::nullopt;

        const auto& leaf = parts[3];
        if (count == 4) {
            if (leaf.equals("volume"))
                return OscCommand{OscCommandKind::TrackVolume, track, 0};
            if (leaf.equals("pan"))
                return OscCommand{OscCommandKind::TrackPan, track, 0};
            if (leaf.equals("mute"))
                return OscCommand{OscCommandKind::TrackMute, track, 0};
            if (leaf.equals("solo"))
                return OscCommand{OscCommandKind::TrackSolo, track, 0};
            return std::nullopt;
        }

        if (count == 5 && leaf.equals("send")) {
            const int send = parseIndex(parts[4], kMaxSendNumber);
            return send > 0 ? std::optional(OscCommand{OscCommandKind::TrackSend, track, send})
                            : std::nullopt;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

// ============================================================================
// Slot mapping
// ============================================================================

static_assert(static_cast<int>(OscCommandKind::MasterPan) + 1 == kOscUnindexedSlots,
              "The unindexed kinds must be the leading run of OscCommandKind for the "
              "identity mapping below to hold");

int oscSlotIndex(const OscCommand& command) {
    const int kindOrdinal = static_cast<int>(command.kind);
    if (kindOrdinal < kOscUnindexedSlots)
        return kindOrdinal;

    if (command.kind == OscCommandKind::FocusedMacro)
        return kOscUnindexedSlots + command.index - 1;

    const int trackBase = kOscTrackSlotBase + ((command.index - 1) * kOscPerTrackSlots);
    switch (command.kind) {
        case OscCommandKind::TrackVolume:
            return trackBase;
        case OscCommandKind::TrackPan:
            return trackBase + 1;
        case OscCommandKind::TrackMute:
            return trackBase + 2;
        case OscCommandKind::TrackSolo:
            return trackBase + 3;
        case OscCommandKind::TrackSend:
            return trackBase + 4 + command.subIndex - 1;
        default:
            break;
    }
    jassertfalse;  // every kind is covered above
    return 0;
}

OscCommand oscCommandForSlot(int slot) {
    jassert(slot >= 0 && slot < kOscSlotCount);

    if (slot < kOscUnindexedSlots)
        return unindexed(static_cast<OscCommandKind>(slot));

    if (slot < kOscTrackSlotBase)
        return OscCommand{OscCommandKind::FocusedMacro, slot - kOscUnindexedSlots + 1, 0};

    const int trackOffset = slot - kOscTrackSlotBase;
    const int track = (trackOffset / kOscPerTrackSlots) + 1;
    switch (const int withinTrack = trackOffset % kOscPerTrackSlots) {
        case 0:
            return OscCommand{OscCommandKind::TrackVolume, track, 0};
        case 1:
            return OscCommand{OscCommandKind::TrackPan, track, 0};
        case 2:
            return OscCommand{OscCommandKind::TrackMute, track, 0};
        case 3:
            return OscCommand{OscCommandKind::TrackSolo, track, 0};
        default:
            return OscCommand{OscCommandKind::TrackSend, track, withinTrack - 4 + 1};
    }
}

}  // namespace magda::osc
