#include "osc/OscAddress.hpp"

namespace magda::osc {

namespace {

/**
 * @brief Parse a strictly decimal 1-based index, or 0 if it is not one.
 *
 * Deliberately not `String::getIntValue`, which answers 0 for "abc", 3 for
 * "3x" and 3 for "3.7" — every one of which would turn a malformed or
 * pattern-bearing address into a plausible-looking command. Leading zeros are
 * rejected too, so one address means one thing: "/magda/track/03/volume" is not
 * a second spelling of track 3.
 */
int parseIndex(const juce::String& token, int maxValue) {
    if (token.isEmpty() || token.length() > 3)
        return 0;
    if (token[0] == '0')  // covers "0" itself and any leading-zero spelling
        return 0;

    int value = 0;
    for (int i = 0; i < token.length(); ++i) {
        const auto c = token[i];
        if (c < '0' || c > '9')
            return 0;
        value = value * 10 + (c - '0');
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
    // Split rather than match incrementally: the grammar is three to five
    // components deep and every branch needs the component count anyway.
    auto parts = juce::StringArray::fromTokens(juce::String(address), "/", "");
    // A well-formed OSC address starts with '/', so the first token is empty.
    if (parts.size() < 3 || parts[0].isNotEmpty())
        return std::nullopt;
    if (parts[1] != "magda")
        return std::nullopt;

    const auto& section = parts[2];

    if (section == "transport") {
        if (parts.size() != 4)
            return std::nullopt;
        const auto& leaf = parts[3];
        if (leaf == "play")
            return unindexed(OscCommandKind::TransportPlay);
        if (leaf == "stop")
            return unindexed(OscCommandKind::TransportStop);
        if (leaf == "record")
            return unindexed(OscCommandKind::TransportRecord);
        if (leaf == "loop")
            return unindexed(OscCommandKind::TransportLoop);
        if (leaf == "tempo")
            return unindexed(OscCommandKind::TransportTempo);
        if (leaf == "position")
            return unindexed(OscCommandKind::TransportPosition);
        return std::nullopt;
    }

    if (section == "master") {
        if (parts.size() != 4)
            return std::nullopt;
        if (parts[3] == "volume")
            return unindexed(OscCommandKind::MasterVolume);
        if (parts[3] == "pan")
            return unindexed(OscCommandKind::MasterPan);
        return std::nullopt;
    }

    if (section == "focused") {
        // Only macros today. A second focused address would branch here.
        if (parts.size() != 5 || parts[3] != "macro")
            return std::nullopt;
        const int macro = parseIndex(parts[4], kMaxMacroNumber);
        return macro > 0 ? std::optional(OscCommand{OscCommandKind::FocusedMacro, macro, 0})
                         : std::nullopt;
    }

    if (section == "track") {
        if (parts.size() < 5)
            return std::nullopt;
        const int track = parseIndex(parts[3], kMaxTrackNumber);
        if (track == 0)
            return std::nullopt;

        const auto& leaf = parts[4];
        if (parts.size() == 5) {
            if (leaf == "volume")
                return OscCommand{OscCommandKind::TrackVolume, track, 0};
            if (leaf == "pan")
                return OscCommand{OscCommandKind::TrackPan, track, 0};
            if (leaf == "mute")
                return OscCommand{OscCommandKind::TrackMute, track, 0};
            if (leaf == "solo")
                return OscCommand{OscCommandKind::TrackSolo, track, 0};
            return std::nullopt;
        }

        if (parts.size() == 6 && leaf == "send") {
            const int send = parseIndex(parts[5], kMaxSendNumber);
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
