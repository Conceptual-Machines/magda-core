#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/remote_api.hpp"
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

bool containsTool(const magda::AgentSurface& surface, const std::string& name) {
    return std::find(surface.toolAllowlist.begin(), surface.toolAllowlist.end(), name) !=
           surface.toolAllowlist.end();
}

bool containsProvider(const magda::AgentSurface& surface, magda::AgentContextProvider provider) {
    return std::find(surface.contextProviders.begin(), surface.contextProviders.end(), provider) !=
           surface.contextProviders.end();
}
}  // namespace

// ---------------------------------------------------------------------------
// Context-scoped surfaces (#1864).
// ---------------------------------------------------------------------------

TEST_CASE("agent surface registry has distinct bounded capabilities", "[console_routing]") {
    using magda::AgentSurfaceId;
    const auto& arrangement = magda::agentSurface(AgentSurfaceId::Arrangement);
    const auto& piano = magda::agentSurface(AgentSurfaceId::PianoRoll);
    const auto& session = magda::agentSurface(AgentSurfaceId::Session);
    const auto& mixer = magda::agentSurface(AgentSurfaceId::Mixer);
    const auto& automation = magda::agentSurface(AgentSurfaceId::Automation);
    const auto& device = magda::agentSurface(AgentSurfaceId::DevicePanel);
    const auto& master = magda::agentSurface(AgentSurfaceId::Master);

    // Arrangement is the one combined structural + musical surface.
    REQUIRE(containsTool(arrangement, "tracks.create"));
    REQUIRE(containsTool(arrangement, "clips.addMidiNote"));
    REQUIRE(containsProvider(arrangement, magda::AgentContextProvider::ReferenceMidi));

    REQUIRE(containsTool(piano, "clips.addMidiNote"));
    REQUIRE_FALSE(containsTool(piano, "tracks.delete"));

    REQUIRE(containsTool(session, "session.launchScene"));
    REQUIRE_FALSE(containsTool(session, "automation.addPoint"));

    REQUIRE(containsTool(mixer, "tracks.update"));
    REQUIRE(containsProvider(mixer, magda::AgentContextProvider::MixAnalysis));
    REQUIRE_FALSE(containsTool(mixer, "clips.addMidiNote"));

    REQUIRE(containsTool(automation, "automation.createLane"));
    REQUIRE_FALSE(containsTool(automation, "session.launchClip"));

    REQUIRE(containsTool(device, "racks.setBypassed"));
    REQUIRE_FALSE(containsTool(device, "tracks.update"));

    REQUIRE(master.runPolicy.maxMutations == 0);
    REQUIRE_FALSE(containsTool(master, "tracks.update"));
}

TEST_CASE("surface context is bounded and always revisioned", "[console_routing]") {
    for (const auto& surface : magda::registeredAgentSurfaces()) {
        INFO(surface.name);
        REQUIRE(containsProvider(surface, magda::AgentContextProvider::ProjectRevision));
        REQUIRE(containsProvider(surface, magda::AgentContextProvider::ActiveView));
        REQUIRE(containsProvider(surface, magda::AgentContextProvider::Conversation));
        REQUIRE(surface.contextProviders.size() <= 10);
        REQUIRE(surface.toolAllowlist.size() <= 24);
    }
}

TEST_CASE("baseline context filters providers and enforces hard bounds", "[console_routing]") {
    using magda::AgentContextFragment;
    using magda::AgentContextProvider;
    using magda::AgentSurfaceId;

    magda::AgentBaselineContextInput input{
        .projectRevision = 42,
        .activeView = "mixer",
        .available =
            {
                AgentContextFragment{AgentContextProvider::Selection, "track:7"},
                AgentContextFragment{AgentContextProvider::MixAnalysis, std::string(20, 'm')},
                AgentContextFragment{AgentContextProvider::ReferenceMidi, "must-not-leak"},
                AgentContextFragment{AgentContextProvider::Conversation, "old|newest-turn"},
                // A duplicate is ignored deterministically.
                AgentContextFragment{AgentContextProvider::Selection, "track:99"},
            },
    };

    const auto context =
        magda::buildAgentBaselineContext(magda::agentSurface(AgentSurfaceId::Mixer), input, 18, 10);
    REQUIRE(context.projectRevision == 42);
    REQUIRE(context.surface == AgentSurfaceId::Mixer);
    REQUIRE(context.activeView == "mixer");
    REQUIRE(context.payloadCharacters <= 18);
    REQUIRE(context.truncated);

    const auto selection =
        std::find_if(context.fragments.begin(), context.fragments.end(), [](const auto& fragment) {
            return fragment.provider == AgentContextProvider::Selection;
        });
    REQUIRE(selection != context.fragments.end());
    REQUIRE(selection->payload == "track:7");
    REQUIRE(
        std::none_of(context.fragments.begin(), context.fragments.end(), [](const auto& fragment) {
            return fragment.provider == AgentContextProvider::ReferenceMidi;
        }));

    magda::AgentBaselineContextInput conversationOnly{
        .projectRevision = 43,
        .activeView = "arrangement",
        .available = {AgentContextFragment{AgentContextProvider::Conversation,
                                           "old-history|newest-turn"}},
    };
    const auto recent = magda::buildAgentBaselineContext(
        magda::agentSurface(AgentSurfaceId::Arrangement), conversationOnly, 11, 11);
    REQUIRE(recent.fragments.size() == 1);
    REQUIRE(recent.fragments.front().payload == "newest-turn");
}

TEST_CASE("surface allowlists contain only real Remote API operations", "[console_routing]") {
    std::vector<std::string> operationNames;
    for (const auto& operation : magda::remote::OperationRegistry::instance().operations())
        operationNames.push_back(operation.name.toStdString());
    REQUIRE(magda::invalidSurfaceTools(operationNames).empty());
}

TEST_CASE("deterministic surface routing performs no classification", "[console_routing]") {
    using magda::AgentSurfaceContext;
    using magda::AgentSurfaceId;
    REQUIRE(magda::resolveAgentSurface({.view = ViewMode::Arrange}).surface ==
            AgentSurfaceId::Arrangement);
    REQUIRE(magda::resolveAgentSurface({.view = ViewMode::Live}).surface ==
            AgentSurfaceId::Session);
    REQUIRE(magda::resolveAgentSurface({.view = ViewMode::Mix}).surface == AgentSurfaceId::Mixer);
    REQUIRE(magda::resolveAgentSurface({.view = ViewMode::Master}).surface ==
            AgentSurfaceId::Master);

    AgentSurfaceContext nested{.view = ViewMode::Arrange,
                               .invocation = magda::AgentInvocationSurface::AutomationEditor};
    REQUIRE(magda::resolveAgentSurface(nested).surface == AgentSurfaceId::Automation);
    nested.invocation = magda::AgentInvocationSurface::PianoRoll;
    REQUIRE(magda::resolveAgentSurface(nested).surface == AgentSurfaceId::PianoRoll);
    nested.invocation = magda::AgentInvocationSurface::DevicePanel;
    REQUIRE(magda::resolveAgentSurface(nested).surface == AgentSurfaceId::DevicePanel);
}

TEST_CASE("explicit aliases and slash overrides escape every view", "[console_routing]") {
    using magda::AgentSurfaceId;
    for (const auto view : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
        INFO(static_cast<int>(view));
        auto decision =
            magda::resolveAgentSurface({.view = view,
                                        .explicitOverride = magda::explicitAgentOverrideFromMessage(
                                            "  @music write a progression")});
        REQUIRE(decision.surface == AgentSurfaceId::PianoRoll);
        REQUIRE(decision.usedExplicitOverride);

        decision =
            magda::resolveAgentSurface({.view = view,
                                        .explicitOverride = magda::explicitAgentOverrideFromMessage(
                                            "/agent automation draw a ramp")});
        REQUIRE(decision.surface == AgentSurfaceId::Automation);

        decision =
            magda::resolveAgentSurface({.view = view,
                                        .explicitOverride = magda::explicitAgentOverrideFromMessage(
                                            "[COMMAND: GROOVE] apply swing")});
        REQUIRE(decision.surface == AgentSurfaceId::Arrangement);
    }
}

TEST_CASE("unknown override falls back deterministically without widening scope",
          "[console_routing]") {
    const auto decision =
        magda::resolveAgentSurface({.view = ViewMode::Mix, .explicitOverride = "not-an-agent"});
    REQUIRE(decision.surface == magda::AgentSurfaceId::Mixer);
    REQUIRE_FALSE(decision.overrideRecognized);
    REQUIRE_FALSE(decision.usedExplicitOverride);
}

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

TEST_CASE("Arrange view does not call the legacy router by default", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    auto d = resolveConsoleIntent(ViewMode::Arrange, RoutingContext{}, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Command);
    REQUIRE_FALSE(d.usedRouter);
    REQUIRE(classifier.calls == 0);
}

TEST_CASE("Arrange router is available only behind transitional fallback flag",
          "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext context;
    context.allowLegacyRouterFallback = true;
    const auto d = resolveConsoleIntent(ViewMode::Arrange, context, classifier.fn());
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
    ctx.explicitAgentOverride = "music";
    auto d = resolveConsoleIntent(ViewMode::Mix, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Music);
    REQUIRE(d.source == std::string("explicit surface override"));
    REQUIRE(classifier.calls == 0);
}

TEST_CASE("Explicit @alias bypasses mix-capture override", "[console_routing]") {
    FakeClassifier classifier{.answer = "MUSIC"};
    RoutingContext ctx;
    ctx.mixCaptureAttached = true;
    ctx.hasExplicitAlias = true;
    ctx.explicitAgentOverride = "music";
    ctx.allowLegacyRouterFallback = true;
    auto d = resolveConsoleIntent(ViewMode::Arrange, ctx, classifier.fn());
    REQUIRE(d.intent == ConsoleIntent::Music);
    REQUIRE_FALSE(d.usedRouter);
    REQUIRE(classifier.calls == 0);
}

TEST_CASE("Explicit [COMMAND:] in Arrange reaches the router", "[console_routing]") {
    FakeClassifier classifier{.answer = "COMMAND"};
    RoutingContext ctx;
    ctx.hasExplicitCommand = true;
    ctx.allowLegacyRouterFallback = true;
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
    ctx.allowLegacyRouterFallback = true;

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
