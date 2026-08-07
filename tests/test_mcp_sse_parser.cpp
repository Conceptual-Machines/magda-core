// The bridge's SSE framing, tested where the invariant actually lives.
//
// An earlier attempt asserted on the bridge process's resident set size after
// driving traffic through a live stream. It passed with the fix reverted:
// forty flushes of keep-alive comments do not move RSS past page granularity
// and allocator reuse, so the test proved nothing at all. The property is about
// what the parser retains, so that is what this measures.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "magda/mcp_bridge/sse_parser.hpp"

using magda::mcp::SseParser;

namespace {

/// Feeds a chunk and returns whatever was emitted for it.
std::vector<std::string> feed(SseParser& parser, const std::string& chunk) {
    std::vector<std::string> emitted;
    parser.consume(chunk, [&](const std::string& message) { emitted.push_back(message); });
    return emitted;
}

}  // namespace

TEST_CASE("Frames are emitted whole, however the bytes arrive", "[mcp][sse]") {
    SseParser parser;

    REQUIRE(feed(parser, "data: {\"a\":1}\n\n") == std::vector<std::string>{"{\"a\":1}"});

    // Split across chunks: a stream delivers whatever the socket had, not whole
    // frames, so a parser that assumed otherwise would lose every message that
    // straddled a read.
    REQUIRE(feed(parser, "data: {\"b\"").empty());
    REQUIRE(feed(parser, ":2}\n\n") == std::vector<std::string>{"{\"b\":2}"});

    // Several in one chunk.
    REQUIRE(feed(parser, "data: 1\n\ndata: 2\n\n") == std::vector<std::string>{"1", "2"});

    // Comments are keep-alives and carry nothing.
    REQUIRE(feed(parser, ":\n\n").empty());
}

TEST_CASE("A stream stops retaining its body once it is known to be a stream", "[mcp][sse]") {
    SseParser parser;

    // Before the first frame the response might still be an ordinary JSON-RPC
    // error — a refused subscriptions/listen — and the content receiver has
    // already consumed the body by the time the status is known, so it has to
    // be kept.
    feed(parser, "data:");
    REQUIRE(parser.retainedBytes() > 0);

    // The first parsed frame settles it: this is an event stream, the raw copy
    // can never be needed, and it is released rather than merely capped.
    REQUIRE(feed(parser, " {\"first\":true}\n\n").size() == 1);
    REQUIRE(parser.retainedBytes() == 0);

    // From here a subscription may stay open for the whole session. Nothing it
    // carries may accumulate — this is the leak the bound exists to prevent.
    const std::string frame = "data: {\"notification\":\"resources/updated\"}\n\n";
    for (int i = 0; i < 20000; ++i)
        parser.consume(frame, [](const std::string&) {});
    REQUIRE(parser.retainedBytes() == 0);

    // Keep-alive comments are the quiet case, and must behave the same.
    for (int i = 0; i < 20000; ++i)
        parser.consume(":\n\n", [](const std::string&) {});
    REQUIRE(parser.retainedBytes() == 0);
}

TEST_CASE("A response that never frames is retained, but bounded", "[mcp][sse]") {
    SseParser parser;

    // The error-body case: no frames, so the parser cannot rule out needing the
    // bytes. It keeps them, because that is the only copy left once a content
    // receiver has run — but a peer that streams unframed data forever must not
    // be able to grow this without limit.
    const std::string junk(64 * 1024, 'x');
    for (int i = 0; i < 64; ++i)
        parser.consume(junk, [](const std::string&) {});

    // Both buffers, not just the raw copy. The first version of this assertion
    // read only `raw_` and passed while the incomplete-frame buffer held four
    // megabytes of the same junk beside it.
    REQUIRE(parser.retainedBytes() <= 2 * 1024 * 1024);
    // Small error bodies still survive intact, which is the whole point of
    // retaining anything.
    SseParser small;
    small.consume("{\"error\":{\"code\":-32600}}", [](const std::string&) {});
    REQUIRE(small.raw() == "{\"error\":{\"code\":-32600}}");
}

TEST_CASE("Framing resynchronises after an oversized partial", "[mcp][sse]") {
    SseParser parser;

    // A frame that never terminates. Past the cap it cannot complete, so the
    // partial is dropped rather than held — and the truncated tail that follows
    // must not be emitted as though it were a message.
    const std::string unterminated(1024 * 1024 + 4096, 'x');
    REQUIRE(feed(parser, "data: " + unterminated).empty());
    REQUIRE(parser.retainedBytes() <= 2 * 1024 * 1024);

    // The separator that ends the garbage closes it out and nothing is emitted.
    REQUIRE(feed(parser, "tail\n\n").empty());

    // The stream is usable again straight afterwards.
    REQUIRE(feed(parser, "data: {\"after\":true}\n\n") ==
            std::vector<std::string>{"{\"after\":true}"});
}
