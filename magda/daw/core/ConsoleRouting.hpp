#pragma once

#include <functional>
#include <string>

#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief Data-model-driven routing for the AI console (#1402).
 *
 * The console used to pick an agent with an ad-hoc if/else blob inlined in the
 * request thread. This module replaces that with a declarative per-view table:
 * each ViewMode maps to a ViewAgentSurface describing which agent it talks to
 * and how it decides (a fixed hard-scoped agent, or the router classifying among
 * the legacy agent set). resolveConsoleIntent() is a pure function over that
 * table plus the per-turn context, so it is unit-testable without the UI/engine.
 */

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
    bool hasExplicitAlias = false;    ///< message starts '@' (escape hatch)
    bool hasExplicitCommand = false;  ///< message starts "[COMMAND:" (slash rewrite)
    bool drummerModeActive = false;   ///< selected track is a drum kit
    bool mixCaptureAttached = false;  ///< a relational capture is pending (#1403)
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
