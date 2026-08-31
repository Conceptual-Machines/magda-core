// The OSC 1.0 reader MAGDA owns because JUCE's is private to its own receive
// loop (#2096).
//
// The packets are built here from the wire format rather than from anything the
// reader shares, so a mistake in the reader cannot be a mistake both ends agree
// on. That also makes the malformed cases expressible, which is most of what is
// worth testing: an encoder will not produce a truncated argument or a bundle
// element that runs past the buffer, and those are exactly what arrives on an
// unauthenticated UDP port.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "magda/daw/audio/osc/OscPacket.hpp"

using namespace magda::osc;
using Catch::Approx;

namespace {

/// What the reader handed over, copied out of the views before the buffer they
/// point into goes away.
struct Captured {
    std::string address;
    std::vector<char> tags;
    std::vector<float> floats;
    std::vector<int> ints;
};

class Collector : public OscPacketReceiver {
  public:
    void oscMessage(const OscMessageView& message) override {
        Captured captured;
        captured.address = juce::String(message.address()).toStdString();
        for (int i = 0; i < message.size(); ++i) {
            captured.tags.push_back(message[i].typeTag);
            if (message[i].isFloat32())
                captured.floats.push_back(message[i].getFloat32());
            if (message[i].isInt32())
                captured.ints.push_back(message[i].getInt32());
        }
        messages.push_back(std::move(captured));
    }

    std::vector<Captured> messages;
};

/// A four-byte-aligned OSC string, terminator included.
void appendString(std::vector<char>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
    out.push_back('\0');
    while (out.size() % 4 != 0)
        out.push_back('\0');
}

void appendInt32(std::vector<char>& out, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<char>((raw >> 24) & 0xff));
    out.push_back(static_cast<char>((raw >> 16) & 0xff));
    out.push_back(static_cast<char>((raw >> 8) & 0xff));
    out.push_back(static_cast<char>(raw & 0xff));
}

void appendFloat32(std::vector<char>& out, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    appendInt32(out, static_cast<std::int32_t>(raw));
}

/// A message, built the way the wire format says.
std::vector<char> message(const std::string& address, const std::string& tags,
                          const std::vector<float>& floats, const std::vector<int>& ints) {
    std::vector<char> out;
    appendString(out, address);
    appendString(out, "," + tags);

    std::size_t floatIndex = 0;
    std::size_t intIndex = 0;
    for (char tag : tags) {
        if (tag == 'f')
            appendFloat32(out, floats[floatIndex++]);
        else if (tag == 'i')
            appendInt32(out, ints[intIndex++]);
        else if (tag == 's')
            appendString(out, "label");
        else if (tag == 'b') {
            appendInt32(out, 3);
            out.insert(out.end(), {'a', 'b', 'c', '\0'});
        }
    }
    return out;
}

std::vector<char> bundle(const std::vector<std::vector<char>>& elements) {
    std::vector<char> out;
    appendString(out, "#bundle");
    appendInt32(out, 0);
    appendInt32(out, 1);  // the immediate time tag
    for (const auto& element : elements) {
        appendInt32(out, static_cast<std::int32_t>(element.size()));
        out.insert(out.end(), element.begin(), element.end());
    }
    return out;
}

bool parse(const std::vector<char>& bytes, Collector& into) {
    return parseOscPacket(bytes.data(), bytes.size(), into);
}

}  // namespace

// ============================================================================
// What a surface actually sends
// ============================================================================

TEST_CASE("A float message parses to its address and its value", "[osc][packet]") {
    Collector collector;
    REQUIRE(parse(message("/magda/track/3/volume", "f", {0.62f}, {}), collector));

    REQUIRE(collector.messages.size() == 1);
    REQUIRE(collector.messages[0].address == "/magda/track/3/volume");
    REQUIRE(collector.messages[0].floats.size() == 1);
    REQUIRE(collector.messages[0].floats[0] == Approx(0.62f));
}

TEST_CASE("An int message parses, because surfaces disagree about buttons", "[osc][packet]") {
    Collector collector;
    REQUIRE(parse(message("/magda/track/1/mute", "i", {}, {1}), collector));

    REQUIRE(collector.messages[0].ints.size() == 1);
    REQUIRE(collector.messages[0].ints[0] == 1);
}

TEST_CASE("A bare message is a message with no arguments", "[osc][packet]") {
    // What a toggle sent with no argument looks like, which the fixed namespace
    // reads as "flip whatever the state is".
    Collector collector;
    REQUIRE(parse(message("/magda/transport/stop", "", {}, {}), collector));

    REQUIRE(collector.messages.size() == 1);
    REQUIRE(collector.messages[0].address == "/magda/transport/stop");
    REQUIRE(collector.messages[0].tags.empty());
}

TEST_CASE("Arguments MAGDA does not read keep the indices of the ones it does", "[osc][packet]") {
    // An XY pad that labels itself: a binding on argument 1 has to still mean
    // argument 1 after the string in front of it.
    Collector collector;
    REQUIRE(parse(message("/xy", "sff", {0.25f, 0.75f}, {}), collector));

    const auto& parsed = collector.messages[0];
    REQUIRE(parsed.tags.size() == 3);
    REQUIRE(parsed.tags[0] == 's');
    REQUIRE(parsed.tags[1] == 'f');
    REQUIRE(parsed.floats[0] == Approx(0.25f));
    REQUIRE(parsed.floats[1] == Approx(0.75f));
}

TEST_CASE("A blob is skipped by its own length", "[osc][packet]") {
    Collector collector;
    REQUIRE(parse(message("/thing", "bf", {0.5f}, {}), collector));

    REQUIRE(collector.messages[0].floats.size() == 1);
    REQUIRE(collector.messages[0].floats[0] == Approx(0.5f));
}

// ============================================================================
// Bundles
// ============================================================================

TEST_CASE("A bundle delivers its elements in order", "[osc][packet]") {
    // The show-control cue the router's ordering rules exist for.
    Collector collector;
    REQUIRE(parse(bundle({message("/magda/transport/stop", "", {}, {}),
                          message("/magda/transport/position", "f", {64.0f}, {}),
                          message("/magda/transport/play", "", {}, {})}),
                  collector));

    REQUIRE(collector.messages.size() == 3);
    REQUIRE(collector.messages[0].address == "/magda/transport/stop");
    REQUIRE(collector.messages[1].address == "/magda/transport/position");
    REQUIRE(collector.messages[2].address == "/magda/transport/play");
}

TEST_CASE("A nested bundle flattens", "[osc][packet]") {
    Collector collector;
    REQUIRE(
        parse(bundle({message("/a", "f", {1.0f}, {}), bundle({message("/b", "f", {2.0f}, {})})}),
              collector));

    REQUIRE(collector.messages.size() == 2);
    REQUIRE(collector.messages[0].address == "/a");
    REQUIRE(collector.messages[1].address == "/b");
}

TEST_CASE("An empty bundle is valid and says nothing", "[osc][packet]") {
    Collector collector;
    REQUIRE(parse(bundle({}), collector));
    REQUIRE(collector.messages.empty());
}

TEST_CASE("Nesting past the cap delivers nothing", "[osc][packet]") {
    // A stream that nests this far is either broken or trying to make the
    // receive thread recurse for free.
    auto packet = message("/deep", "f", {1.0f}, {});
    for (int depth = 0; depth < kMaxOscBundleDepth + 1; ++depth)
        packet = bundle({packet});

    Collector collector;
    REQUIRE_FALSE(parse(packet, collector));
    REQUIRE(collector.messages.empty());
}

// ============================================================================
// Rejection, as the cases that differ
// ============================================================================

TEST_CASE("A packet that is not a message or a bundle is rejected", "[osc][packet]") {
    Collector collector;
    const std::vector<char> junk{'x', 'y', 'z', '\0'};
    REQUIRE_FALSE(parseOscPacket(junk.data(), junk.size(), collector));
    REQUIRE_FALSE(parseOscPacket(nullptr, 0, collector));
}

TEST_CASE("An unterminated address is rejected", "[osc][packet]") {
    Collector collector;
    const std::vector<char> bytes{'/', 'a', 'b', 'c'};
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("A type tag string without its comma is rejected", "[osc][packet]") {
    std::vector<char> bytes;
    appendString(bytes, "/thing");
    appendString(bytes, "f");  // no leading comma
    appendFloat32(bytes, 0.5f);

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("An argument the type tag promised but did not carry is rejected", "[osc][packet]") {
    std::vector<char> bytes;
    appendString(bytes, "/thing");
    appendString(bytes, ",ff");
    appendFloat32(bytes, 0.5f);  // the second float is missing

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("An unknown type tag is rejected rather than guessed past", "[osc][packet]") {
    // Its payload length is exactly what is unknown, so skipping it would be
    // guessing where the next argument starts.
    std::vector<char> bytes;
    appendString(bytes, "/thing");
    appendString(bytes, ",zf");
    appendFloat32(bytes, 0.5f);

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("A non-ASCII address is rejected", "[osc][packet]") {
    // OSC 1.0 says so, parseOscAddress would reject it a byte later, and
    // juce::StringRef asserts on it — which from an unauthenticated UDP port
    // would be a debug-build crash rather than a dropped packet.
    std::vector<char> bytes;
    std::string address = "/th";
    address.push_back(static_cast<char>(0xff));
    appendString(bytes, address);
    appendString(bytes, ",");

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("A packet that is not a whole number of words is rejected", "[osc][packet]") {
    auto bytes = message("/thing", "f", {0.5f}, {});
    bytes.push_back('x');

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("A message with bytes left over after its arguments is rejected", "[osc][packet]") {
    // The type tag string says exactly how long the payload is, so a trailing
    // aligned word is not part of the message. The "not a whole number of
    // words" rule above only catches an unaligned suffix.
    auto bytes = message("/thing", "f", {0.5f}, {});
    appendInt32(bytes, 0);

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
    REQUIRE(collector.messages.empty());
}

TEST_CASE("A bundle element padded with junk runs none of its command", "[osc][packet]") {
    // Otherwise an element could declare itself longer than the message it
    // carries and still have that message applied, which is the whole-packet
    // guarantee leaking one element at a time.
    auto padded = message("/magda/transport/play", "", {}, {});
    appendInt32(padded, 0);

    Collector collector;
    REQUIRE_FALSE(parse(bundle({message("/magda/transport/stop", "", {}, {}), padded}), collector));
    REQUIRE(collector.messages.empty());
}

TEST_CASE("A bundle element whose size runs past the buffer is rejected", "[osc][packet]") {
    auto bytes = bundle({message("/a", "f", {1.0f}, {})});
    // Overwrite the element size with one larger than what follows it. The size
    // sits after "#bundle\0" and the eight-byte time tag.
    const std::size_t sizeAt = 16;
    bytes[sizeAt + 3] = static_cast<char>(0x7c);

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
}

TEST_CASE("A malformed packet delivers none of its good elements", "[osc][packet]") {
    // The two-pass guarantee. Delivered as it went, this bundle would stop the
    // transport and locate it before finding out the third element was corrupt.
    std::vector<char> broken;
    appendString(broken, "/magda/transport/play");
    appendString(broken, ",z");  // an argument type that names nothing
    appendInt32(broken, 0);

    Collector collector;
    REQUIRE_FALSE(parse(bundle({message("/magda/transport/stop", "", {}, {}),
                                message("/magda/transport/position", "f", {64.0f}, {}), broken}),
                        collector));
    REQUIRE(collector.messages.empty());
}

TEST_CASE("More arguments than the inline array holds is a rejection, not a truncation",
          "[osc][packet]") {
    // A binding reading argument 20 of a truncated message would silently read
    // nothing and look like a surface that had gone quiet.
    const int count = OscMessageView::kMaxArguments + 1;
    std::vector<char> bytes;
    appendString(bytes, "/many");
    appendString(bytes, "," + std::string(static_cast<std::size_t>(count), 'f'));
    for (int i = 0; i < count; ++i)
        appendFloat32(bytes, static_cast<float>(i));

    Collector collector;
    REQUIRE_FALSE(parseOscPacket(bytes.data(), bytes.size(), collector));
    REQUIRE(collector.messages.empty());
}

// ============================================================================
// The bridge from JUCE's own message type
// ============================================================================

TEST_CASE("A juce::OSCMessage becomes an equivalent view", "[osc][packet]") {
    const juce::String address("/magda/track/2/pan");
    const juce::OSCMessage source(juce::OSCAddressPattern(address), 0.25f, 7);
    const auto view = OscMessageView::fromOscMessage(address, source);

    REQUIRE(juce::String(view.address()) == address);
    REQUIRE(view.size() == 2);
    REQUIRE(view[0].isFloat32());
    REQUIRE(view[0].getFloat32() == Approx(0.25f));
    REQUIRE(view[1].isInt32());
    REQUIRE(view[1].getInt32() == 7);

    // Past the end reads "not a number" rather than undefined memory.
    REQUIRE_FALSE(view[2].isFloat32());
    REQUIRE_FALSE(view[-1].isInt32());
}
