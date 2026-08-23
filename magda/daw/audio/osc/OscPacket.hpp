#pragma once

#include <juce_osc/juce_osc.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace magda::osc {

// ============================================================================
// OscArgument
// ============================================================================

/**
 * @brief One argument of a message, as far as MAGDA reads them.
 *
 * The type tag is kept verbatim rather than mapped onto an enum, because the
 * only questions asked of it are "is this a number" and, for everything else,
 * "how many bytes do I skip". A surface sending a string label beside a fader
 * value is a shape the binding path supports, and it reaches here as an
 * argument whose tag is `s` and whose value is nothing.
 *
 * The value is copied out of the datagram rather than pointed at, which costs
 * four bytes per argument and removes a lifetime: a view can outlive the buffer
 * it came from for as long as its address does, and only the address has to be
 * thought about.
 */
struct OscArgument {
    char typeTag = '\0';
    std::int32_t intValue = 0;
    float floatValue = 0.0f;

    bool isInt32() const {
        return typeTag == 'i';
    }
    bool isFloat32() const {
        return typeTag == 'f';
    }
    std::int32_t getInt32() const {
        return intValue;
    }
    float getFloat32() const {
        return floatValue;
    }
};

// ============================================================================
// OscMessageView
// ============================================================================

/**
 * @brief A parsed message that owns nothing but its arguments.
 *
 * The receive thread is a real-time-ish path — `OscRouter::handleMessage` is
 * allocation-free apart from the hop that carries a drain to the message thread
 * — so the parse in front of it must not allocate either. That is the whole
 * reason this type exists rather than `juce::OSCMessage`, which builds a
 * `String` and an `Array` per message.
 *
 * The address is a `juce::StringRef` into the datagram buffer. OSC strings are
 * NUL-terminated in place and padded to four bytes, so the bytes a `StringRef`
 * needs are already there, and no copy has to be made to get a comparable,
 * parseable address. The consequence is the usual one: **a view is only valid
 * while the buffer it was parsed from is**, which for the receive loop means
 * for the duration of the callback.
 *
 * Arguments live in a fixed inline array, so the whole view is a stack value.
 * A message carrying more than `kMaxArguments` is rejected by the parser rather
 * than truncated here — a binding reading argument 20 of a truncated message
 * would silently read nothing and look like a surface that had gone quiet.
 */
class OscMessageView {
  public:
    /// Well past what a control surface sends down one address. TouchOSC and
    /// Open Stage Control send one value, occasionally two for an XY pad, and a
    /// label beside it at most.
    static constexpr int kMaxArguments = 16;

    OscMessageView() = default;
    OscMessageView(juce::StringRef address, const OscArgument* arguments, int count);

    /**
     * @brief The bridge from JUCE's own message type.
     *
     * For callers that already hold an `OSCMessage` — tests, and anything that
     * built a message rather than receiving one. `address` has to outlive the
     * view, which is why it is a parameter rather than something taken from
     * `message`: `OSCAddressPattern::toString` returns a temporary, and a view
     * over a temporary is a dangling one.
     */
    static OscMessageView fromOscMessage(const juce::String& address,
                                         const juce::OSCMessage& message);

    juce::StringRef address() const {
        return address_;
    }

    int size() const {
        return count_;
    }

    bool isEmpty() const {
        return count_ == 0;
    }

    /// Out of range yields an empty argument, whose tag matches no type, so a
    /// caller that indexes past the end reads "not a number" rather than
    /// undefined memory.
    const OscArgument& operator[](int index) const;

  private:
    juce::StringRef address_{""};
    std::array<OscArgument, kMaxArguments> arguments_{};
    int count_ = 0;
};

// ============================================================================
// Parsing
// ============================================================================

/**
 * @brief Where the messages in a packet are delivered.
 *
 * A virtual call rather than a `std::function`, because a `std::function` built
 * from a capturing lambda may allocate, and this is the one path that must not.
 */
class OscPacketReceiver {
  public:
    virtual ~OscPacketReceiver() = default;
    virtual void oscMessage(const OscMessageView& message) = 0;
};

/// How deep a nested bundle is followed before it is treated as hostile.
/// Bundles legitimately nest one or two levels; a stream that nests further is
/// either broken or trying to make the receive thread recurse for free.
inline constexpr int kMaxOscBundleDepth = 8;

/**
 * @brief Read one datagram, and deliver the messages in it.
 *
 * A packet is either a message or a bundle; a bundle is flattened, and its time
 * tag is not honoured — everything is applied on arrival, exactly as it was
 * when JUCE was doing the reading. Scheduling a bundle for a future beat is a
 * real OSC feature and separate work.
 *
 * ## A malformed packet changes nothing
 *
 * The bytes are walked twice: once to validate, once to deliver. Delivering as
 * it went would apply the first two elements of a `[stop, position, play]` cue
 * before discovering the third was corrupt, which is a stop and a locate the
 * sender never asked for. A second walk over a datagram already in cache is not
 * a cost worth trading that for.
 *
 * ## What it will not accept
 *
 * Anything JUCE's own parser rejected, and one thing more. An address must be
 * printable ASCII: OSC 1.0 says so, `parseOscAddress` would reject anything
 * else a byte later, and `juce::StringRef` asserts it — so without the check a
 * hostile packet is a debug-build crash reachable from an unauthenticated UDP
 * port.
 *
 * Argument types are read for `i` and `f` and skipped for the rest of OSC 1.0's
 * set. An unknown type tag rejects the message rather than being skipped by
 * guesswork, because its payload length is exactly what is unknown.
 *
 * @return false when the packet is not valid OSC, in which case nothing was
 *         delivered.
 */
bool parseOscPacket(const void* data, std::size_t size, OscPacketReceiver& receiver);

}  // namespace magda::osc
