#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/core/ConsoleRouting.hpp"

namespace {
bool containsTool(const magda::AgentSurface& surface, const std::string& name) {
    return std::find(surface.toolAllowlist.begin(), surface.toolAllowlist.end(), name) !=
           surface.toolAllowlist.end();
}

bool containsProvider(const magda::AgentSurface& surface, magda::AgentContextProvider provider) {
    return std::find(surface.contextProviders.begin(), surface.contextProviders.end(), provider) !=
           surface.contextProviders.end();
}
}  // namespace

TEST_CASE("agent surface registry has distinct bounded capabilities", "[console_routing]") {
    using magda::AgentSurfaceId;
    const auto& arrangement = magda::agentSurface(AgentSurfaceId::Arrangement);
    const auto& piano = magda::agentSurface(AgentSurfaceId::PianoRoll);
    const auto& session = magda::agentSurface(AgentSurfaceId::Session);
    const auto& mixer = magda::agentSurface(AgentSurfaceId::Mixer);
    const auto& automation = magda::agentSurface(AgentSurfaceId::Automation);
    const auto& device = magda::agentSurface(AgentSurfaceId::DevicePanel);
    const auto& master = magda::agentSurface(AgentSurfaceId::Master);

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
    REQUIRE(containsTool(device, "devices.listParameters"));
    REQUIRE(containsTool(device, "devices.setParameter"));
    REQUIRE(containsTool(device, "devices.setParameterConfig"));
    REQUIRE(containsTool(device, "devices.add"));
    REQUIRE(containsTool(device, "devices.remove"));
    REQUIRE(containsTool(device, "devices.openEditor"));
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

TEST_CASE("surface routing is deterministic from context", "[console_routing]") {
    using magda::AgentSurfaceContext;
    using magda::AgentSurfaceId;
    using magda::ViewMode;

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
    using magda::ViewMode;
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

TEST_CASE("unknown override falls back without widening scope", "[console_routing]") {
    const auto decision = magda::resolveAgentSurface(
        {.view = magda::ViewMode::Mix, .explicitOverride = "not-an-agent"});
    REQUIRE(decision.surface == magda::AgentSurfaceId::Mixer);
    REQUIRE_FALSE(decision.overrideRecognized);
    REQUIRE_FALSE(decision.usedExplicitOverride);
}
