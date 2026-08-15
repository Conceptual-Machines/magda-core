#include "osc/OscPacket.hpp"

#include <cstring>

namespace magda::osc {

namespace {

/// The eight bytes a bundle starts with, terminator included.
constexpr char kBundleTag[] = "#bundle";
constexpr std::size_t kBundleTagSize = 8;
constexpr std::size_t kTimeTagSize = 8;

/// The whole padded length of an OSC string whose content is `length` bytes:
/// the terminator is part of what gets padded, so a four-character string
/// occupies eight bytes rather than four.
constexpr std::size_t paddedStringSize(std::size_t length) {
    return ((length / 4) + 1) * 4;
}

constexpr std::size_t paddedBlobSize(std::size_t length) {
    return ((length + 3) / 4) * 4;
}

/// OSC 1.0 restricts an address to printable ASCII, `parseOscAddress` would
/// reject anything else a byte later, and `juce::StringRef` asserts it — so a
/// packet from an unauthenticated port has to be checked here rather than
/// trusted one layer up.
bool isPrintableAscii(const char* text, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x20 || byte > 0x7e)
            return false;
    }
    return true;
}

float floatFromBigEndian(const char* bytes) {
    const auto raw = juce::ByteOrder::bigEndianInt(bytes);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

/**
 * @brief One walk over a packet, validating or delivering.
 *
 * The same code does both, because a validator that is not the reader is a
 * validator that will disagree with it. `receiver_` null is the validating
 * pass; every failure path is identical, so a packet that survives the first
 * walk cannot fail the second.
 */
class Walker {
  public:
    Walker(const char* data, OscPacketReceiver* receiver) : data_(data), receiver_(receiver) {}

    bool packet(std::size_t begin, std::size_t end, int depth) {
        if (depth >= kMaxOscBundleDepth)
            return false;
        if (end <= begin || (end - begin) % 4 != 0)
            return false;

        if (data_[begin] == '/')
            return message(begin, end);

        if (end - begin >= kBundleTagSize &&
            std::memcmp(data_ + begin, kBundleTag, kBundleTagSize) == 0)
            return bundle(begin, end, depth);

        return false;
    }

  private:
    /// The content of the OSC string starting at `pos`, with `pos` advanced past
    /// its padding. `length` excludes the terminator.
    bool readString(std::size_t& pos, std::size_t end, const char*& text, std::size_t& length) {
        const std::size_t start = pos;
        while (pos < end && data_[pos] != '\0')
            ++pos;
        if (pos >= end)
            return false;  // ran off the element without finding a terminator

        length = pos - start;
        const std::size_t padded = paddedStringSize(length);
        if (start + padded > end)
            return false;  // the padding is not there, so neither is the string

        text = data_ + start;
        pos = start + padded;
        return true;
    }

    bool readInt32(std::size_t& pos, std::size_t end, std::int32_t& value) {
        if (pos + 4 > end)
            return false;
        value = static_cast<std::int32_t>(juce::ByteOrder::bigEndianInt(data_ + pos));
        pos += 4;
        return true;
    }

    /// Advance past an argument of a type MAGDA does not read, or fail if the
    /// tag names no type at all. An unknown tag cannot be skipped by guesswork,
    /// because its payload length is exactly what is unknown.
    bool skipArgument(char tag, std::size_t& pos, std::size_t end) {
        switch (tag) {
            case 'c':  // char
            case 'r':  // 32-bit colour
            case 'm':  // 4-byte MIDI message
                pos += 4;
                return pos <= end;

            case 'h':  // int64
            case 'd':  // float64
            case 't':  // time tag
                pos += 8;
                return pos <= end;

            case 's':  // string
            case 'S':  // symbol
            {
                const char* text = nullptr;
                std::size_t length = 0;
                return readString(pos, end, text, length);
            }

            case 'b':  // blob
            {
                std::int32_t length = 0;
                if (!readInt32(pos, end, length) || length < 0)
                    return false;
                const std::size_t padded = paddedBlobSize(static_cast<std::size_t>(length));
                if (pos + padded > end)
                    return false;
                pos += padded;
                return true;
            }

            case 'T':  // true
            case 'F':  // false
            case 'N':  // null
            case 'I':  // infinitum
                return true;

            default:
                return false;
        }
    }

    using ArgumentArray = std::array<OscArgument, OscMessageView::kMaxArguments>;

    /// One argument per type tag, in order, with `pos` walking the payload. The
    /// tags MAGDA does not read still take a slot, so argument indices stay the
    /// ones the sender used: a binding on argument 1 of an XY pad that sends a
    /// label first has to still mean argument 1.
    bool readArguments(const char* tags, std::size_t tagCount, std::size_t& pos, std::size_t end,
                       ArgumentArray& arguments, int& count) {
        for (std::size_t i = 1; i < tagCount; ++i) {
            const char tag = tags[i];

            if (count >= OscMessageView::kMaxArguments)
                return false;  // see OscMessageView: rejected, not truncated
            auto& argument = arguments[static_cast<std::size_t>(count)];

            if (tag == 'i' || tag == 'f') {
                if (pos + 4 > end)
                    return false;
                if (tag == 'i')
                    argument.intValue =
                        static_cast<std::int32_t>(juce::ByteOrder::bigEndianInt(data_ + pos));
                else
                    argument.floatValue = floatFromBigEndian(data_ + pos);
                pos += 4;
            } else if (!skipArgument(tag, pos, end)) {
                return false;
            }

            argument.typeTag = tag;
            ++count;
        }
        return true;
    }

    bool message(std::size_t begin, std::size_t end) {
        std::size_t pos = begin;

        const char* address = nullptr;
        std::size_t addressLength = 0;
        if (!readString(pos, end, address, addressLength))
            return false;
        if (addressLength == 0 || !isPrintableAscii(address, addressLength))
            return false;

        ArgumentArray arguments{};
        int count = 0;

        // A message with no type tag string at all is legal OSC 1.0 and means no
        // arguments. JUCE rejected it; accepting it costs nothing and is one
        // fewer dialect that goes silent for no reason a user can see.
        if (pos < end) {
            const char* tags = nullptr;
            std::size_t tagCount = 0;
            if (!readString(pos, end, tags, tagCount))
                return false;
            if (tagCount == 0 || tags[0] != ',')
                return false;
            if (!readArguments(tags, tagCount, pos, end, arguments, count))
                return false;
        }

        if (receiver_ != nullptr)
            receiver_->oscMessage(
                OscMessageView(juce::StringRef(address), arguments.data(), count));
        return true;
    }

    bool bundle(std::size_t begin, std::size_t end, int depth) {
        std::size_t pos = begin + kBundleTagSize + kTimeTagSize;
        if (pos > end)
            return false;

        while (pos < end) {
            std::int32_t elementSize = 0;
            if (!readInt32(pos, end, elementSize))
                return false;
            // Every OSC element is a whole number of four-byte words, and an
            // empty one is not an element.
            if (elementSize <= 0 || elementSize % 4 != 0)
                return false;

            const auto span = static_cast<std::size_t>(elementSize);
            if (pos + span > end)
                return false;
            if (!packet(pos, pos + span, depth + 1))
                return false;
            pos += span;
        }

        return true;
    }

    const char* data_ = nullptr;
    OscPacketReceiver* receiver_ = nullptr;
};

}  // namespace

// ============================================================================
// OscMessageView
// ============================================================================

OscMessageView::OscMessageView(juce::StringRef address, const OscArgument* arguments, int count)
    : address_(address), count_(juce::jlimit(0, kMaxArguments, count)) {
    for (int i = 0; i < count_; ++i)
        arguments_[static_cast<std::size_t>(i)] = arguments[i];
}

OscMessageView OscMessageView::fromOscMessage(const juce::String& address,
                                              const juce::OSCMessage& message) {
    std::array<OscArgument, kMaxArguments> arguments{};
    const int count = juce::jmin(message.size(), kMaxArguments);

    for (int i = 0; i < count; ++i) {
        auto& out = arguments[static_cast<std::size_t>(i)];
        const auto& in = message[i];
        if (in.isFloat32()) {
            out.typeTag = 'f';
            out.floatValue = in.getFloat32();
        } else if (in.isInt32()) {
            out.typeTag = 'i';
            out.intValue = in.getInt32();
        } else if (in.isString()) {
            out.typeTag = 's';
        } else if (in.isBlob()) {
            out.typeTag = 'b';
        } else {
            out.typeTag = '?';
        }
    }

    return OscMessageView(juce::StringRef(address), arguments.data(), count);
}

const OscArgument& OscMessageView::operator[](int index) const {
    static const OscArgument none{};
    if (index < 0 || index >= count_)
        return none;
    return arguments_[static_cast<std::size_t>(index)];
}

// ============================================================================
// parseOscPacket
// ============================================================================

bool parseOscPacket(const void* data, std::size_t size, OscPacketReceiver& receiver) {
    const auto* bytes = static_cast<const char*>(data);
    if (bytes == nullptr || size < 4)
        return false;

    // Validate the whole packet before any of it is applied. See the header for
    // why a half-applied cue is worse than a dropped one.
    Walker validate(bytes, nullptr);
    if (!validate.packet(0, size, 0))
        return false;

    Walker deliver(bytes, &receiver);
    return deliver.packet(0, size, 0);
}

}  // namespace magda::osc
