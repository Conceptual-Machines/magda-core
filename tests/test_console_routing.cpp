#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ConsoleRouting.hpp"

using magda::ConsoleIntent;
using magda::consoleSurfaceForView;
using magda::resolveConsoleIntent;
using magda::RoutingContext;
using magda::ViewMode;

namespace {
// A classify callback that always returns the same router answer, and records
// whether it was invoked (so we can assert the router is only consulted for
// Classified views).
struct FakeClassifier {
    std::string answer;
    int calls = 0;
    std::function<std::string()> fn() {
        return [this]() -> std::string {
            ++calls;
            return answer;
        };
    }
};
}  // namespace

// ---------------------------------------------------------------------------
// The data model: which views show the mix cockpit / hard-scope an agent.
// ---------------------------------------------------------------------------

TEST_CASE("consoleSurfaceForView - only mixer views show the analyze trigger",
          "[console_routing]") {
    // The reference picker (#1403) is always shown and not modeled here; only the
    // offline mix-analysis trigger is mixer-scoped.
    REQUIRE_FALSE(consoleSurfaceForView(ViewMode::Live).showsAnalyzeTrigger);
    REQUIRE_FALSE(consoleSurfaceForView(ViewMode::Arrange).showsAnalyzeTrigger);
    REQUIRE(consoleSurfaceForView(ViewMode::Mix).showsAnalyzeTrigger);
    REQUIRE(consoleSurfaceForView(ViewMode::Master).showsAnalyzeTrigger);
}

TEST_CASE("consoleSurfaceForView - hard-scoped primaries", "[console_routing]") {
    REQUIRE(consoleSurfaceForView(ViewMode::Live).primary == ConsoleIntent::Session);
    REQUIRE(consoleSurfaceForView(ViewMode::Mix).primary == ConsoleIntent::Mixing);
    REQUIRE(consoleSurfaceForView(ViewMode::Master).primary == ConsoleIntent::Mixing);
    REQUIRE(consoleSurfaceForView(ViewMode::Arrange).mode == magda::RoutingMode::Classified);
}

// ---------------------------------------------------------------------------
// Hard-scoped views: no router, fixed agent.
// ---------------------------------------------------------------------------

TEST_CASE("Mixer view routes to Mixing without the router", "[console_routing]") {
    FakeClassifier classifier{.answer = "COMMAND"};
    auto d = resolveConsoleIntent(ViewMode::Mix, RoutingContext{}, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Mixing);
    REQUIRE_FALSE(d.usedRouter);
    REQUIRE(classifier.calls == 0);

    d = resolveConsoleIntent(ViewMode::Master, RoutingContext{}, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Mixing);
}

TEST_CASE("Session view routes to Session (stub) without the router", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    auto d = resolveConsoleIntent(ViewMode::Live, RoutingContext{}, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Session);
    REQUIRE(classifier.calls == 0);
}

// ---------------------------------------------------------------------------
// Classified view (Arrange): the router decides.
// ---------------------------------------------------------------------------

TEST_CASE("Arrange view defers to the router", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    auto d = resolveConsoleIntent(ViewMode::Arrange, RoutingContext{}, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Music);
    REQUIRE(d.usedRouter);
    REQUIRE(classifier.calls == 1);
}

TEST_CASE("Arrange view falls back to Command when the router errors", "[console_routing]") {
    FakeClassifier classifier{.answer = ""};  // empty == router error/skip
    auto d = resolveConsoleIntent(ViewMode::Arrange, RoutingContext{}, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Command);
    REQUIRE_FALSE(d.usedRouter);
}

// ---------------------------------------------------------------------------
// Context overrides.
// ---------------------------------------------------------------------------

TEST_CASE("Attached capture forces Mixing from any view", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext ctx;
    ctx.mixCaptureAttached = true;
    auto d = resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Mixing);
    REQUIRE(classifier.calls == 0);
}

TEST_CASE("Drummer context routes to Drum in Arrange", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext ctx;
    ctx.drummerModeActive = true;
    auto d = resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Drum);
    REQUIRE(classifier.calls == 0);
}

TEST_CASE("Drummer context does NOT override a hard-scoped view", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext ctx;
    ctx.drummerModeActive = true;
    // Mixer hard-scope wins over drummer context.
    auto d = resolveConsoleIntent(ViewMode::Mix, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Mixing);
}

// ---------------------------------------------------------------------------
// Escape hatches: explicit @alias / [COMMAND:] bypass context scoping.
// ---------------------------------------------------------------------------

TEST_CASE("Explicit @alias bypasses mixer hard-scope to the router", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext ctx;
    ctx.hasExplicitAlias = true;
    auto d = resolveConsoleIntent(ViewMode::Mix, ctx, classifier.fn());
    // Mix is HardScoped, so an escape falls through to the surface primary
    // (Mixing) only after skipping the hard-scope branch; but with no router
    // path for a HardScoped view it lands on the fallback primary.
    REQUIRE(d.intent == ConsoleIntent::Mixing);
    REQUIRE(d.source == std::string("fallback"));
}

TEST_CASE("Explicit @alias bypasses mix-capture override", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext ctx;
    ctx.mixCaptureAttached = true;
    ctx.hasExplicitAlias = true;
    // In Arrange, the escape skips capture + (no) hard-scope and reaches the router.
    auto d = resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Music);
    REQUIRE(d.usedRouter);
}

TEST_CASE("Explicit [COMMAND:] in Arrange reaches the router", "[console_routing]") {
    FakeClassifier classifier{.answer = "COMMAND"};
    RoutingContext ctx;
    ctx.hasExplicitCommand = true;
    auto d = resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Command);
    REQUIRE(d.usedRouter);
}

TEST_CASE("Drummer context is suppressed only by @alias, not [COMMAND:]", "[console_routing]") {
    // Faithful to the legacy logic: the drummer branch checks !hasExplicitAlias
    // only, so a slash-rewritten [COMMAND:] does NOT escape drummer context.
    FakeClassifier classifier{.answer = "COMMAND"};
    RoutingContext ctx;
    ctx.drummerModeActive = true;

    ctx.hasExplicitCommand = true;
    REQUIRE(resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn()).intent ==
            ConsoleIntent::Drum);

    ctx.hasExplicitCommand = false;
    ctx.hasExplicitAlias = true;
    auto d = resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Command);  // @alias escapes -> router
    REQUIRE(d.usedRouter);
}

// ---------------------------------------------------------------------------
// String mapping round-trip.
// ---------------------------------------------------------------------------

TEST_CASE("intentFromString maps router tokens", "[console_routing]") {
    REQUIRE(magda::intentFromString("MUSIC") == ConsoleIntent::Music);
    REQUIRE(magda::intentFromString("BOTH") == ConsoleIntent::Both);
    REQUIRE(magda::intentFromString("AUTOMATION") == ConsoleIntent::Automation);
    REQUIRE(magda::intentFromString("MIXING") == ConsoleIntent::Mixing);
    REQUIRE(magda::intentFromString("nonsense") == ConsoleIntent::Command);
}
