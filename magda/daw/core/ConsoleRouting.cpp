#include "ConsoleRouting.hpp"

namespace magda {

const ViewAgentSurface& consoleSurfaceForView(ViewMode mode) {
    // The data model: one surface per view (#1402). The reference-track picker
    // (#1403) shows in every view and is not modeled here; only the offline
    // mix-analysis trigger is mixer-scoped (showsAnalyzeTrigger).
    //   Live    -> session agent, no analyze trigger
    //   Arrange -> router + legacy agent set, no analyze trigger
    //   Mix     -> mixing agent only, analyze trigger shown
    //   Master  -> treated like Mix
    static const ViewAgentSurface kLive{.mode = RoutingMode::HardScoped,
                                        .primary = ConsoleIntent::Session,
                                        .showsAnalyzeTrigger = false};
    static const ViewAgentSurface kArrange{.mode = RoutingMode::Classified,
                                           .primary = ConsoleIntent::Command,
                                           .showsAnalyzeTrigger = false};
    static const ViewAgentSurface kMix{.mode = RoutingMode::HardScoped,
                                       .primary = ConsoleIntent::Mixing,
                                       .showsAnalyzeTrigger = true};

    switch (mode) {
        case ViewMode::Live:
            return kLive;
        case ViewMode::Arrange:
            return kArrange;
        case ViewMode::Mix:
        case ViewMode::Master:
            return kMix;
    }
    return kArrange;  // default
}

RoutingDecision resolveConsoleIntent(ViewMode view, const RoutingContext& ctx,
                                     const std::function<std::string()>& classify) {
    const ViewAgentSurface& surface = consoleSurfaceForView(view);
    const bool explicitEscape = ctx.hasExplicitAlias || ctx.hasExplicitCommand;

    // 1. An attached relational capture (#1403) hard-scopes to the mixing agent
    //    from any view, unless the user typed an explicit escape hatch.
    if (ctx.mixCaptureAttached && !explicitEscape)
        return {
            .intent = ConsoleIntent::Mixing, .usedRouter = false, .source = "mix capture attached"};

    // 2. A hard-scoped view (mixer -> mixing, session -> session) routes straight
    //    to its primary, unless escaped.
    if (surface.mode == RoutingMode::HardScoped && !explicitEscape)
        return {.intent = surface.primary, .usedRouter = false, .source = "view hard-scope"};

    // 3. Drummer context (a selected kit) routes to the drummer, unless the user
    //    typed an explicit @alias.
    if (ctx.drummerModeActive && !ctx.hasExplicitAlias)
        return {.intent = ConsoleIntent::Drum, .usedRouter = false, .source = "drummer context"};

    // 4. Classified views ask the router and map its answer.
    if (surface.mode == RoutingMode::Classified && classify) {
        const std::string s = classify();
        if (!s.empty())
            return {.intent = intentFromString(s), .usedRouter = true, .source = "router"};
    }

    // 5. Fallback to the view's primary.
    return {.intent = surface.primary, .usedRouter = false, .source = "fallback"};
}

const char* toIntentString(ConsoleIntent intent) {
    switch (intent) {
        case ConsoleIntent::Command:
            return "COMMAND";
        case ConsoleIntent::Music:
            return "MUSIC";
        case ConsoleIntent::Both:
            return "BOTH";
        case ConsoleIntent::Automation:
            return "AUTOMATION";
        case ConsoleIntent::Drum:
            return "DRUM";
        case ConsoleIntent::Mixing:
            return "MIXING";
        case ConsoleIntent::Session:
            return "SESSION";
    }
    return "COMMAND";
}

ConsoleIntent intentFromString(const std::string& s) {
    if (s == "MUSIC")
        return ConsoleIntent::Music;
    if (s == "BOTH")
        return ConsoleIntent::Both;
    if (s == "AUTOMATION")
        return ConsoleIntent::Automation;
    if (s == "DRUM")
        return ConsoleIntent::Drum;
    if (s == "MIXING")
        return ConsoleIntent::Mixing;
    if (s == "SESSION")
        return ConsoleIntent::Session;
    return ConsoleIntent::Command;
}

}  // namespace magda
