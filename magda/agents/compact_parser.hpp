#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace magda {

/** Called for each streamed token. Return false to cancel. */
using TokenCallback = std::function<bool(const juce::String& token)>;

// ============================================================================
// IR Instruction Types
// ============================================================================

enum class OpCode {
    Track,   // Create or reference track
    Del,     // Delete track
    Mute,    // Mute track by name
    Solo,    // Solo track by name
    Set,     // Set track properties
    Clip,    // Create clip
    Fx,      // Add FX
    Select,  // Select clips/tracks by criteria
    Arp,     // Add arpeggio (on last clip target)
    Chord,   // Add chord (on last clip target)
    Note,    // Add note (on last clip target)
};

/** How a track is referenced — by 1-based index, by name, or implicitly (last TRACK). */
struct TrackRef {
    int id = -1;  // 1-based index, or -1 if by name
    juce::String name;
    bool implicit = false;  // true = use last TRACK context

    bool isById() const {
        return id > 0;
    }
    bool isImplicit() const {
        return implicit;
    }
};

// --- Per-opcode payloads ---------------------------------------------------

struct TrackOp {
    juce::String name;
    juce::String fxAlias;  // non-empty = create track + add this plugin, name from plugin
};

struct DelOp {
    TrackRef target;
};

struct MuteOp {
    TrackRef target;  // implicit/by-id/by-name — falls back to current track
};

struct SoloOp {
    TrackRef target;  // implicit/by-id/by-name — falls back to current track
};

struct SetOp {
    TrackRef target;
    juce::StringPairArray props;  // key=value pairs (vol, pan, mute, solo …)
};

struct ClipOp {
    TrackRef target;
    double bar = 1.0;
    double lengthBars = 4.0;
    juce::String name;  // optional clip name
};

struct FxOp {
    TrackRef target;  // may be implicit (use current track)
    juce::String fxName;
};

struct SelectOp {
    enum class Target { Clips, Tracks };
    Target target = Target::Clips;

    // Optional predicate: field op value
    juce::String field;  // "length", "bar", "track", "name" (empty = select all)
    juce::String op;     // "<", ">", "<=", ">=", "=", "!="
    juce::String value;  // number (bars) or quoted string
};

struct ArpOp {
    juce::String root;
    juce::String quality;
    double beat = 0.0;
    double step = 0.5;
    double beats = -1.0;   // -1 = not specified
    int inversion = 0;     // 0=root, 1=first, 2=second
    juce::String pattern;  // "up", "down", "updown" (empty = up)
};

struct ChordOp {
    juce::String root;
    juce::String quality;
    double beat = 0.0;
    double length = 1.0;
    int velocity = -1;  // -1 = not specified
    int inversion = 0;  // 0=root, 1=first, 2=second
};

struct NoteOp {
    juce::String pitch;
    double beat = 0.0;
    double length = 1.0;
    int velocity = -1;  // -1 = not specified
};

using OpPayload = std::variant<TrackOp, DelOp, MuteOp, SoloOp, SetOp, ClipOp, FxOp, SelectOp, ArpOp,
                               ChordOp, NoteOp>;

struct Instruction {
    OpCode opcode;
    OpPayload payload;
};

// ============================================================================
// Compact Parser — LLM text → IR instructions
// ============================================================================

class CompactParser {
  public:
    /**
     * @brief Parse compact assembler output into IR instructions.
     * @param compact  Multi-line compact instructions from the LLM
     * @return List of instructions, empty on error (check getLastError())
     */
    std::vector<Instruction> parse(const juce::String& compact);

    juce::String getLastError() const {
        return lastError_;
    }

  private:
    static TrackRef parseRef(const juce::String& token);
    static bool isInteger(const juce::String& s);

    juce::String lastError_;
};

}  // namespace magda
