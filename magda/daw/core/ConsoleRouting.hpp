#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief Deterministic, context-scoped agent surfaces (#1864).
 *
 * Surfaces are registered in the core layer rather than in AIChatConsoleContent.
 * They describe the responsibility, bounded context, Remote API capability
 * boundary, and run/output policy for an acting agent. The registry contains
 * surfaces which do not yet have a top-level ViewMode (piano roll, automation,
 * and device panel) so nested UI surfaces and explicit overrides can resolve
 * them without adding model-based intent classification.
 */
enum class AgentSurfaceId {
    Arrangement,
    PianoRoll,
    Session,
    Mixer,
    Automation,
    DevicePanel,
    Master,
    Drummer,
};

enum class AgentContextProvider {
    ProjectRevision,
    ActiveView,
    Selection,
    Timing,
    TrackSummaries,
    ClipSummaries,
    DeviceSummaries,
    SessionState,
    AutomationState,
    MixAnalysis,
    ReferenceMidi,
    Conversation,
};

enum class AgentInvocationSurface {
    ViewDefault,
    PianoRoll,
    AutomationEditor,
    DevicePanel,
};

enum class AgentOutputKind {
    ChangeSummary,
    MusicalResult,
    PerformanceStatus,
    Analysis,
    DeviceResult,
};

struct AgentRunPolicy {
    std::size_t maxSteps = 8;
    std::size_t maxMutations = 8;
    bool approveMutations = true;
};

struct AgentOutputPolicy {
    AgentOutputKind kind = AgentOutputKind::ChangeSummary;
    bool includeMutationSummary = true;
    bool includeToolTrace = false;
};

struct AgentSurface {
    AgentSurfaceId id = AgentSurfaceId::Arrangement;
    std::string name;
    std::string responsibility;
    std::vector<std::string> promptFragments;
    std::vector<AgentContextProvider> contextProviders;
    std::vector<std::string> toolAllowlist;
    AgentRunPolicy runPolicy;
    AgentOutputPolicy outputPolicy;
    bool showsAnalyzeTrigger = false;
};

struct AgentContextFragment {
    AgentContextProvider provider = AgentContextProvider::Selection;
    std::string payload;
};

struct AgentBaselineContextInput {
    std::uint64_t projectRevision = 0;
    std::string activeView;
    std::vector<AgentContextFragment> available;
};

struct AgentBaselineContext {
    std::uint64_t projectRevision = 0;
    AgentSurfaceId surface = AgentSurfaceId::Arrangement;
    std::string activeView;
    std::vector<AgentContextFragment> fragments;
    std::size_t payloadCharacters = 0;
    bool truncated = false;
};

/// All registered surfaces. Stable for the lifetime of the process.
const std::vector<AgentSurface>& registeredAgentSurfaces();

/// Look up a surface by id, or by an explicit user alias (without '@' or '/').
const AgentSurface& agentSurface(AgentSurfaceId id);
const AgentSurface* agentSurfaceForAlias(const std::string& alias);

/// Return allowlisted names which are absent from the supplied operation set.
/// This keeps the registry independent of RemoteApiService while allowing
/// startup/tests to validate it against remote::OperationRegistry.
std::vector<std::string> invalidSurfaceTools(
    const std::vector<std::string>& availableOperationNames);

/**
 * Select only the providers declared by a surface and enforce a hard payload
 * bound. Targeted providers construct `available`; this function prevents an
 * accidental whole-project blob from crossing the surface boundary.
 *
 * Conversation retains its newest text when truncated. Other summaries retain
 * their prefix. Duplicate provider fragments are ignored.
 */
AgentBaselineContext buildAgentBaselineContext(const AgentSurface& surface,
                                               const AgentBaselineContextInput& input,
                                               std::size_t maxPayloadCharacters = 12'000,
                                               std::size_t maxFragmentCharacters = 3'000);

/// Extract an explicit surface override from "@alias ...", "/agent alias ...",
/// or the legacy "[COMMAND: ...]" slash rewrite. Empty means no override.
std::string explicitAgentOverrideFromMessage(const std::string& message);

struct AgentSurfaceContext {
    ViewMode view = ViewMode::Arrange;
    AgentInvocationSurface invocation = AgentInvocationSurface::ViewDefault;
    std::string explicitOverride;
    bool drummerModeActive = false;
    bool mixCaptureAttached = false;
};

struct AgentSurfaceDecision {
    AgentSurfaceId surface = AgentSurfaceId::Arrangement;
    bool usedExplicitOverride = false;
    bool overrideRecognized = true;
    std::string source;
};

/**
 * Pure deterministic resolution. Precedence:
 *   1. recognized explicit @agent or /agent override
 *   2. attached mix capture
 *   3. contextual drummer mode
 *   4. nested invocation surface
 *   5. top-level view default
 */
AgentSurfaceDecision resolveAgentSurface(const AgentSurfaceContext& context);

const char* toSurfaceString(AgentSurfaceId id);

// -------------------------------------------------------------------------
// Transitional legacy dispatch API.
//
// The existing panel still dispatches to separate command/music/etc. agents
// until an AgentRuntime <-> Remote API adapter is wired. The classifier is
// disabled by default and can only run when allowLegacyRouterFallback is set.
// New integrations should use resolveAgentSurface() instead.
// -------------------------------------------------------------------------

/// The agent surface the console dispatches a turn to.
enum class ConsoleIntent {
    Command,     ///< DSL command agent
    Music,       ///< music (note/chord) agent
    Both,        ///< command + music in parallel
    Automation,  ///< automation agent
    Drum,        ///< drummer agent
    Mixing,      ///< mix-analysis agent (#886)
    Session,     ///< session/live agent (stubbed; real agent is a later issue)
};

/// How a view decides which intent a turn routes to.
enum class RoutingMode {
    HardScoped,  ///< always the view's primary intent, no router
    Classified,  ///< run the router agent to pick among the legacy agent set
};

/// Declarative routing surface for one view.
struct ViewAgentSurface {
    RoutingMode mode = RoutingMode::Classified;
    ConsoleIntent primary = ConsoleIntent::Command;  ///< HardScoped target / Classified fallback
    /// Whether the footer's offline mix-analysis trigger (Live/Quick/Deep, #886)
    /// shows for this view. Mixer-only. The reference-track picker (#1403) is
    /// always shown regardless of view, so it is not gated here.
    bool showsAnalyzeTrigger = false;
};

/// The data model: the routing surface for a given view.
const ViewAgentSurface& consoleSurfaceForView(ViewMode mode);

/// Per-turn context that can override the view's default routing.
struct RoutingContext {
    bool hasExplicitAlias = false;           ///< message starts '@' (escape hatch)
    bool hasExplicitCommand = false;         ///< message starts "[COMMAND:" (slash rewrite)
    bool drummerModeActive = false;          ///< selected track is a drum kit
    bool mixCaptureAttached = false;         ///< a relational capture is pending (#1403)
    std::string explicitAgentOverride;       ///< deterministic surface alias, if present
    bool allowLegacyRouterFallback = false;  ///< transitional #1843 compatibility only
};

/// The resolved routing for a turn, plus a human-readable reason for logging.
struct RoutingDecision {
    ConsoleIntent intent = ConsoleIntent::Command;
    bool usedRouter = false;
    std::string source;  ///< why this intent was chosen (for DBG)
};

/**
 * @brief Resolve which agent a console turn routes to.
 *
 * Pure over (view, ctx) plus a `classify` callback that wraps the router agent.
 * `classify` returns the router intent string ("COMMAND"/"MUSIC"/"BOTH"/...) or
 * an empty string on error/skip; keeping it a callback leaves this module free
 * of any agent/UI dependency. Precedence (matches the legacy inline logic):
 *   1. attached mix capture (and no explicit escape) -> Mixing
 *   2. HardScoped view (and no explicit escape)       -> the view's primary
 *   3. drummer context (and no explicit @alias)       -> Drum
 *   4. Classified view: router classification         -> mapped intent
 *   5. fallback                                        -> the view's primary
 */
RoutingDecision resolveConsoleIntent(ViewMode view, const RoutingContext& ctx,
                                     const std::function<std::string()>& classify);

/// Stable string token for an intent (DBG + dispatch readability).
const char* toIntentString(ConsoleIntent intent);

/// Map a router output string ("COMMAND"/"MUSIC"/...) to an intent.
/// Unrecognized strings fall back to Command.
ConsoleIntent intentFromString(const std::string& s);

}  // namespace magda
