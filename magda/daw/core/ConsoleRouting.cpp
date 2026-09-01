#include "ConsoleRouting.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace magda {
namespace {

using Provider = AgentContextProvider;

std::string normalizedAlias(std::string alias) {
    std::transform(alias.begin(), alias.end(), alias.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    alias.erase(std::remove_if(alias.begin(), alias.end(),
                               [](unsigned char c) { return std::isspace(c) != 0; }),
                alias.end());
    if (!alias.empty() && (alias.front() == '@' || alias.front() == '/'))
        alias.erase(alias.begin());
    return alias;
}

AgentSurfaceId defaultSurfaceForView(ViewMode view) {
    switch (view) {
        case ViewMode::Live:
            return AgentSurfaceId::Session;
        case ViewMode::Arrange:
            return AgentSurfaceId::Arrangement;
        case ViewMode::Mix:
            return AgentSurfaceId::Mixer;
        case ViewMode::Master:
            return AgentSurfaceId::Master;
    }
    return AgentSurfaceId::Arrangement;
}

}  // namespace

const std::vector<AgentSurface>& registeredAgentSurfaces() {
    // Tool names deliberately come only from remote::OperationRegistry. Missing
    // APIs (device parameters/presets, mixer sends, note updates, mix capture)
    // are described as responsibilities/context but are not fabricated as tools.
    static const std::vector<AgentSurface> surfaces = {
        {.id = AgentSurfaceId::Arrangement,
         .name = "arrangement",
         .responsibility =
             "Tracks, arrangement clips, composition, MIDI, devices, and project timing.",
         .promptFragments =
             {"Plan structural and musical changes together; use the smallest coherent sequence.",
              "Prefer selection-scoped edits and inspect current state before mutation."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::Timing, Provider::TrackSummaries, Provider::ClipSummaries,
                              Provider::DeviceSummaries, Provider::ReferenceMidi,
                              Provider::Conversation},
         .toolAllowlist =
             {
                 "project.get",   "project.setTempo",  "project.setTimeSignature",
                 "tracks.list",   "tracks.get",        "tracks.create",
                 "tracks.update", "tracks.delete",     "clips.list",
                 "clips.get",     "clips.createMidi",  "clips.addMidiNote",
                 "clips.delete",  "devices.list",      "racks.create",
                 "racks.remove",  "racks.setBypassed", "selection.get",
                 "selection.set", "transport.get",
             },
         .runPolicy = {.maxSteps = 12, .maxMutations = 12, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::ChangeSummary,
                          .includeMutationSummary = true,
                          .includeToolTrace = false}},
        {.id = AgentSurfaceId::PianoRoll,
         .name = "piano-roll",
         .responsibility =
             "Notes, chords, rhythm, expression, and selected or reference MIDI clips.",
         .promptFragments =
             {"Keep note operations inside the selected MIDI clip unless asked to create one.",
              "Use reference MIDI as musical context, never as permission to rewrite it."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::Timing, Provider::ClipSummaries, Provider::ReferenceMidi,
                              Provider::Conversation},
         .toolAllowlist = {"project.get", "tracks.list", "tracks.get", "clips.list", "clips.get",
                           "clips.createMidi", "clips.addMidiNote", "clips.delete", "selection.get",
                           "selection.set", "transport.get"},
         .runPolicy = {.maxSteps = 10, .maxMutations = 8, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::MusicalResult,
                          .includeMutationSummary = true,
                          .includeToolTrace = false}},
        {.id = AgentSurfaceId::Session,
         .name = "session",
         .responsibility = "Session clips, scenes, launch state, and performance controls.",
         .promptFragments = {"Treat launch and stop operations as live-performance actions.",
                             "Inspect session state before changing what is playing."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::Timing, Provider::TrackSummaries, Provider::ClipSummaries,
                              Provider::SessionState, Provider::Conversation},
         .toolAllowlist = {"project.get",
                           "tracks.list",
                           "tracks.get",
                           "clips.list",
                           "clips.get",
                           "clips.createMidi",
                           "session.get",
                           "session.launchClip",
                           "session.stopClip",
                           "session.stopTrack",
                           "session.stopAll",
                           "session.launchScene",
                           "transport.get",
                           "transport.play",
                           "transport.stop",
                           "transport.setRecording",
                           "transport.setLoopEnabled",
                           "transport.seek",
                           "selection.get",
                           "selection.set"},
         .runPolicy = {.maxSteps = 8, .maxMutations = 6, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::PerformanceStatus,
                          .includeMutationSummary = true,
                          .includeToolTrace = false}},
        {.id = AgentSurfaceId::Mixer,
         .name = "mixer",
         .responsibility = "Levels, routing, devices, and measured mix analysis.",
         .promptFragments = {"Ground recommendations in measured mix context when it is available.",
                             "Do not claim send or parameter changes: those Remote API operations "
                             "are unavailable."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::Timing, Provider::TrackSummaries, Provider::DeviceSummaries,
                              Provider::MixAnalysis, Provider::Conversation},
         .toolAllowlist = {"project.get", "tracks.list", "tracks.get", "tracks.update",
                           "devices.list", "selection.get", "transport.get"},
         .runPolicy = {.maxSteps = 8, .maxMutations = 6, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::Analysis,
                          .includeMutationSummary = true,
                          .includeToolTrace = false},
         .showsAnalyzeTrigger = true},
        {.id = AgentSurfaceId::Automation,
         .name = "automation",
         .responsibility = "Automation targets, lanes, clips, and points.",
         .promptFragments =
             {"Resolve the target and inspect its lane before adding or clearing points.",
              "Keep generated curves bounded to the requested time and value range."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::Timing, Provider::TrackSummaries, Provider::ClipSummaries,
                              Provider::AutomationState, Provider::Conversation},
         .toolAllowlist = {"project.get", "tracks.list", "tracks.get", "clips.list", "clips.get",
                           "selection.get", "selection.set", "automation.getLane",
                           "automation.createLane", "automation.addPoint", "automation.clearLane",
                           "transport.get"},
         .runPolicy = {.maxSteps = 12, .maxMutations = 10, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::ChangeSummary,
                          .includeMutationSummary = true,
                          .includeToolTrace = false}},
        {.id = AgentSurfaceId::DevicePanel,
         .name = "device",
         .responsibility = "Device topology, racks, presets, parameters, and sound design.",
         .promptFragments = {"Operate only on the focused device or its containing rack.",
                             "List parameters before writing; only parameters the user enabled "
                             "for AI control accept writes.",
                             "Preset mutation is advisory until a corresponding Remote API tool "
                             "exists."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::TrackSummaries, Provider::DeviceSummaries,
                              Provider::Conversation},
         .toolAllowlist = {"project.get", "tracks.list", "tracks.get", "devices.list",
                           "devices.catalog", "devices.add", "devices.remove", "devices.move",
                           "devices.listParameters", "devices.setParameter",
                           "devices.setParameterConfig", "devices.openEditor", "racks.create",
                           "racks.remove", "racks.setBypassed", "selection.get", "selection.set"},
         .runPolicy = {.maxSteps = 8, .maxMutations = 5, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::DeviceResult,
                          .includeMutationSummary = true,
                          .includeToolTrace = false}},
        {.id = AgentSurfaceId::Master,
         .name = "master",
         .responsibility = "Master-chain inspection and mastering analysis.",
         .promptFragments = {"Protect headroom and report evidence for mastering recommendations.",
                             "The current Remote API exposes master inspection but no safe master "
                             "mutation tools."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Timing,
                              Provider::TrackSummaries, Provider::DeviceSummaries,
                              Provider::MixAnalysis, Provider::Conversation},
         .toolAllowlist = {"project.get", "tracks.list", "tracks.get", "devices.list",
                           "transport.get"},
         .runPolicy = {.maxSteps = 6, .maxMutations = 0, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::Analysis,
                          .includeMutationSummary = false,
                          .includeToolTrace = false},
         .showsAnalyzeTrigger = true},
        {.id = AgentSurfaceId::Drummer,
         .name = "drummer",
         .responsibility = "Drum patterns, role-aware kit rows, variations, and fills.",
         .promptFragments =
             {"Use role-aware kit context and keep hits inside the selected or created MIDI clip.",
              "Preserve the established groove unless the user explicitly asks to replace it."},
         .contextProviders = {Provider::ProjectRevision, Provider::ActiveView, Provider::Selection,
                              Provider::Timing, Provider::TrackSummaries, Provider::ClipSummaries,
                              Provider::ReferenceMidi, Provider::Conversation},
         .toolAllowlist = {"project.get", "tracks.list", "tracks.get", "clips.list", "clips.get",
                           "clips.createMidi", "clips.addMidiNote", "clips.delete", "selection.get",
                           "selection.set", "transport.get"},
         .runPolicy = {.maxSteps = 10, .maxMutations = 8, .approveMutations = true},
         .outputPolicy = {.kind = AgentOutputKind::MusicalResult,
                          .includeMutationSummary = true,
                          .includeToolTrace = false}},
    };
    return surfaces;
}

const AgentSurface& agentSurface(AgentSurfaceId id) {
    const auto& surfaces = registeredAgentSurfaces();
    const auto found =
        std::find_if(surfaces.begin(), surfaces.end(), [id](const auto& s) { return s.id == id; });
    return found != surfaces.end() ? *found : surfaces.front();
}

const AgentSurface* agentSurfaceForAlias(const std::string& rawAlias) {
    const auto alias = normalizedAlias(rawAlias);
    AgentSurfaceId id;
    if (alias == "arrange" || alias == "arrangement" || alias == "command")
        id = AgentSurfaceId::Arrangement;
    else if (alias == "piano" || alias == "pianoroll" || alias == "piano-roll" || alias == "music")
        id = AgentSurfaceId::PianoRoll;
    else if (alias == "live" || alias == "session")
        id = AgentSurfaceId::Session;
    else if (alias == "mix" || alias == "mixer" || alias == "mixing")
        id = AgentSurfaceId::Mixer;
    else if (alias == "automation" || alias == "auto")
        id = AgentSurfaceId::Automation;
    else if (alias == "device" || alias == "devices" || alias == "sound-design")
        id = AgentSurfaceId::DevicePanel;
    else if (alias == "master" || alias == "mastering")
        id = AgentSurfaceId::Master;
    else if (alias == "drum" || alias == "drummer")
        id = AgentSurfaceId::Drummer;
    else
        return nullptr;
    return &agentSurface(id);
}

std::vector<std::string> invalidSurfaceTools(
    const std::vector<std::string>& availableOperationNames) {
    const std::unordered_set<std::string> available(availableOperationNames.begin(),
                                                    availableOperationNames.end());
    std::vector<std::string> invalid;
    for (const auto& surface : registeredAgentSurfaces()) {
        for (const auto& tool : surface.toolAllowlist) {
            if (!available.contains(tool) &&
                std::find(invalid.begin(), invalid.end(), tool) == invalid.end())
                invalid.push_back(tool);
        }
    }
    return invalid;
}

AgentBaselineContext buildAgentBaselineContext(const AgentSurface& surface,
                                               const AgentBaselineContextInput& input,
                                               std::size_t maxPayloadCharacters,
                                               std::size_t maxFragmentCharacters) {
    AgentBaselineContext result{.projectRevision = input.projectRevision,
                                .surface = surface.id,
                                .activeView = input.activeView};
    if (maxPayloadCharacters == 0 || maxFragmentCharacters == 0) {
        result.truncated = !input.available.empty();
        return result;
    }

    // Follow provider registration order rather than caller order so context
    // shape remains stable across UI implementations.
    for (const auto provider : surface.contextProviders) {
        if (provider == AgentContextProvider::ProjectRevision ||
            provider == AgentContextProvider::ActiveView)
            continue;

        const auto candidate = std::find_if(
            input.available.begin(), input.available.end(),
            [provider](const auto& fragment) { return fragment.provider == provider; });
        if (candidate == input.available.end() || candidate->payload.empty())
            continue;

        const auto remaining = maxPayloadCharacters - result.payloadCharacters;
        if (remaining == 0) {
            result.truncated = true;
            break;
        }
        const auto allowed =
            std::min({candidate->payload.size(), maxFragmentCharacters, remaining});
        std::string payload;
        if (provider == AgentContextProvider::Conversation && allowed < candidate->payload.size())
            payload = candidate->payload.substr(candidate->payload.size() - allowed);
        else
            payload = candidate->payload.substr(0, allowed);

        result.payloadCharacters += payload.size();
        result.fragments.push_back({.provider = provider, .payload = std::move(payload)});
        result.truncated = result.truncated || allowed < candidate->payload.size();
    }
    return result;
}

std::string explicitAgentOverrideFromMessage(const std::string& message) {
    const auto first = message.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    if (message.compare(first, 9, "[COMMAND:") == 0)
        return "command";

    if (message[first] == '@') {
        const auto end = message.find_first_of(" \t\r\n", first + 1);
        return message.substr(first + 1, end == std::string::npos ? end : end - first - 1);
    }

    if (message.compare(first, 6, "/agent") == 0 &&
        (first + 6 == message.size() ||
         std::isspace(static_cast<unsigned char>(message[first + 6])) != 0)) {
        const auto value = message.find_first_not_of(" \t\r\n", first + 6);
        if (value == std::string::npos)
            return {};
        const auto end = message.find_first_of(" \t\r\n", value);
        return message.substr(value, end == std::string::npos ? end : end - value);
    }
    return {};
}

AgentSurfaceDecision resolveAgentSurface(const AgentSurfaceContext& context) {
    if (!context.explicitOverride.empty()) {
        if (const auto* surface = agentSurfaceForAlias(context.explicitOverride))
            return {.surface = surface->id,
                    .usedExplicitOverride = true,
                    .overrideRecognized = true,
                    .source = "explicit override"};
    }

    if (context.mixCaptureAttached)
        return {.surface = AgentSurfaceId::Mixer, .source = "mix capture"};
    if (context.drummerModeActive)
        return {.surface = AgentSurfaceId::Drummer, .source = "drummer context"};

    switch (context.invocation) {
        case AgentInvocationSurface::PianoRoll:
            return {.surface = AgentSurfaceId::PianoRoll, .source = "piano-roll invocation"};
        case AgentInvocationSurface::AutomationEditor:
            return {.surface = AgentSurfaceId::Automation, .source = "automation invocation"};
        case AgentInvocationSurface::DevicePanel:
            return {.surface = AgentSurfaceId::DevicePanel, .source = "device invocation"};
        case AgentInvocationSurface::ViewDefault:
            break;
    }

    return {.surface = defaultSurfaceForView(context.view),
            .usedExplicitOverride = false,
            .overrideRecognized = context.explicitOverride.empty(),
            .source = context.explicitOverride.empty() ? "view default"
                                                       : "unknown override; view default"};
}

const char* toSurfaceString(AgentSurfaceId id) {
    return agentSurface(id).name.c_str();
}

}  // namespace magda
