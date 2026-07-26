#include <algorithm>

#include "../core/AutomationInfo.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/RackInfo.hpp"
#include "../core/TrackInfo.hpp"
#include "../project/ProjectInfo.hpp"
#include "automation_api.hpp"
#include "clip_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "remote_api.hpp"
#include "selection_api.hpp"
#include "session_api.hpp"
#include "track_api.hpp"
#include "transport_api.hpp"

namespace magda::remote {
namespace {

void assertMessageThread() {
#if !defined(MAGDA_ENABLE_TEST_HOOKS) || !MAGDA_ENABLE_TEST_HOOKS
    JUCE_ASSERT_MESSAGE_THREAD;
#endif
}

juce::String trackTypeName(TrackType type) {
    switch (type) {
        case TrackType::Audio:
            return "audio";
        case TrackType::Group:
            return "group";
        case TrackType::Aux:
            return "aux";
        case TrackType::Master:
            return "master";
        case TrackType::MultiOut:
            return "multi_out";
        case TrackType::Chord:
            return "chord";
    }
    return "audio";
}

juce::String deviceTypeName(DeviceType type) {
    switch (type) {
        case DeviceType::Instrument:
            return "instrument";
        case DeviceType::Effect:
            return "effect";
        case DeviceType::MIDI:
            return "midi";
        case DeviceType::Analysis:
            return "analysis";
    }
    return "effect";
}

juce::String pluginFormatName(PluginFormat format) {
    switch (format) {
        case PluginFormat::VST3:
            return "vst3";
        case PluginFormat::AU:
            return "au";
        case PluginFormat::VST:
            return "vst";
        case PluginFormat::Internal:
            return "internal";
    }
    return "vst3";
}

juce::String launchModeName(LaunchMode mode) {
    return mode == LaunchMode::Toggle ? "toggle" : "trigger";
}

juce::String launchQuantizeName(LaunchQuantize quantize) {
    switch (quantize) {
        case LaunchQuantize::None:
            return "none";
        case LaunchQuantize::EightBars:
            return "8_bars";
        case LaunchQuantize::FourBars:
            return "4_bars";
        case LaunchQuantize::TwoBars:
            return "2_bars";
        case LaunchQuantize::OneBar:
            return "1_bar";
        case LaunchQuantize::HalfBar:
            return "1/2";
        case LaunchQuantize::QuarterBar:
            return "1/4";
        case LaunchQuantize::EighthBar:
            return "1/8";
        case LaunchQuantize::SixteenthBar:
            return "1/16";
    }
    return "none";
}

juce::String followActionName(FollowAction action) {
    switch (action) {
        case FollowAction::None:
            return "none";
        case FollowAction::PlayNext:
            return "next";
        case FollowAction::PlayPrevious:
            return "previous";
        case FollowAction::PlayRandom:
            return "random";
        case FollowAction::Stop:
            return "stop";
        case FollowAction::PlayAgain:
            return "again";
    }
    return "none";
}

juce::String sessionStateName(SessionClipPlayState state) {
    switch (state) {
        case SessionClipPlayState::Stopped:
            return "stopped";
        case SessionClipPlayState::Queued:
            return "queued";
        case SessionClipPlayState::Playing:
            return "playing";
    }
    return "stopped";
}

juce::String curveName(AutomationCurveType curve) {
    switch (curve) {
        case AutomationCurveType::Linear:
            return "linear";
        case AutomationCurveType::Bezier:
            return "bezier";
        case AutomationCurveType::Step:
            return "step";
        case AutomationCurveType::HardCorner:
            return "hard_corner";
    }
    return "linear";
}

juce::String safeRoutingId(const juce::String& id) {
    if (id.isEmpty() || id == "all" || id == "master" || id == "default" || id.startsWith("track:"))
        return id;
    return {};
}

DeviceDto makeDeviceDto(const DeviceInfo& device, TrackId trackId, std::optional<RackId> rackId,
                        std::optional<ChainId> chainId) {
    DeviceDto dto;
    dto.id = device.id;
    dto.trackId = trackId;
    dto.rackId = rackId;
    dto.chainId = chainId;
    dto.name = device.name;
    dto.type = deviceTypeName(device.deviceType);
    dto.format = pluginFormatName(device.format);
    dto.instrument = device.isInstrument;
    dto.bypassed = device.bypassed;
    dto.gainDb = device.gainDb;
    return dto;
}

void appendRack(const RackInfo& rack, TrackId trackId, std::optional<RackId> parentRackId,
                std::optional<ChainId> parentChainId, DeviceGraphDto& graph) {
    RackDto rackDto;
    rackDto.id = rack.id;
    rackDto.trackId = trackId;
    rackDto.parentRackId = parentRackId;
    rackDto.parentChainId = parentChainId;
    rackDto.name = rack.name;
    rackDto.bypassed = rack.bypassed;
    rackDto.volumeDb = rack.volume;
    rackDto.pan = rack.pan;
    for (const auto& chain : rack.chains)
        rackDto.chainIds.push_back(chain.id);
    graph.racks.push_back(std::move(rackDto));

    for (const auto& chain : rack.chains) {
        ChainDto chainDto;
        chainDto.id = chain.id;
        chainDto.rackId = rack.id;
        chainDto.name = chain.name;
        chainDto.outputIndex = chain.outputIndex;
        chainDto.muted = chain.muted;
        chainDto.solo = chain.solo;
        chainDto.bypassed = chain.bypassed;
        chainDto.volumeDb = chain.volume;
        chainDto.pan = chain.pan;

        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                chainDto.deviceIds.push_back(device.id);
                graph.devices.push_back(makeDeviceDto(device, trackId, rack.id, chain.id));
            } else {
                const auto& nested = getRack(element);
                chainDto.nestedRackIds.push_back(nested.id);
                appendRack(nested, trackId, rack.id, chain.id, graph);
            }
        }
        graph.chains.push_back(std::move(chainDto));
    }
}

}  // namespace

ProjectDto makeProjectDto(const ProjectInfo& project) {
    ProjectDto dto;
    dto.name = project.name;
    dto.tempo = project.tempo;
    dto.timeSignatureNumerator = project.timeSignatureNumerator;
    dto.timeSignatureDenominator = project.timeSignatureDenominator;
    dto.sampleRate = project.sampleRate;
    dto.timelineLengthBars = project.timelineLengthBars;
    dto.keyRoot = project.keyRoot;
    dto.keyQuality = project.keyQuality == 1 ? "minor" : "major";
    dto.loopEnabled = project.loopEnabled;
    dto.loopStartBeats = project.loopStartBeats;
    dto.loopEndBeats = project.loopEndBeats;
    return dto;
}

TrackDto makeTrackDto(const TrackInfo& track) {
    TrackDto dto;
    dto.id = track.id;
    dto.type = trackTypeName(track.type);
    dto.name = track.name;
    dto.colourArgb = track.colour.getARGB();
    if (track.parentId != INVALID_TRACK_ID)
        dto.parentId = track.parentId;
    dto.childIds = track.childIds;
    dto.volume = track.volume;
    dto.pan = track.pan;
    dto.muted = track.muted;
    dto.soloed = track.soloed;
    dto.recordArmed = track.recordArmed;
    dto.frozen = track.frozen;
    dto.audioInputDevice = safeRoutingId(track.audioInputDevice);
    dto.midiInputDevice = safeRoutingId(track.midiInputDevice);
    dto.audioOutputDevice = safeRoutingId(track.audioOutputDevice);
    dto.midiOutputDevice = safeRoutingId(track.midiOutputDevice);
    return dto;
}

ClipDto makeClipDto(const ClipInfo& clip) {
    ClipDto dto;
    dto.id = clip.id;
    dto.trackId = clip.trackId;
    dto.type = clip.isAudio() ? "audio" : "midi";
    dto.view = clip.view == ClipView::Session ? "session" : "arrangement";
    dto.name = clip.name;
    dto.colourArgb = clip.colour.getARGB();
    dto.startBeat = clip.placement.startBeat;
    dto.lengthBeats = clip.placement.lengthBeats;
    dto.enabled = clip.enabled;
    if (clip.sceneIndex >= 0)
        dto.sceneIndex = clip.sceneIndex;
    dto.launchMode = launchModeName(clip.launchMode);
    dto.launchQuantize = launchQuantizeName(clip.launchQuantize);
    dto.followAction = followActionName(clip.followAction);
    dto.notes.reserve(clip.midiNotes.size());
    for (const auto& note : clip.midiNotes)
        dto.notes.push_back({note.noteNumber, note.velocity, note.startBeat, note.lengthBeats});
    return dto;
}

DeviceGraphDto makeDeviceGraphDto(const std::vector<TrackInfo>& tracks) {
    DeviceGraphDto graph;
    for (const auto& track : tracks) {
        for (const auto& element : track.chain.fxChainElements) {
            if (isDevice(element)) {
                graph.devices.push_back(
                    makeDeviceDto(getDevice(element), track.id, std::nullopt, std::nullopt));
            } else {
                appendRack(getRack(element), track.id, std::nullopt, std::nullopt, graph);
            }
        }
        for (const auto& element : track.chain.postFxChainElements)
            graph.devices.push_back(
                makeDeviceDto(element.device, track.id, std::nullopt, std::nullopt));
        // mixerAnalysisElements are session-only UI state and are deliberately excluded.
    }
    return graph;
}

SelectionDto makeSelectionDto(MagdaApi& api) {
    assertMessageThread();

    auto& selection = api.selection();
    SelectionDto dto;
    if (selection.getSelectedTrack() != INVALID_TRACK_ID)
        dto.trackId = selection.getSelectedTrack();
    if (selection.getSelectedClip() != INVALID_CLIP_ID)
        dto.clipId = selection.getSelectedClip();
    dto.clipIds.assign(selection.getSelectedClips().begin(), selection.getSelectedClips().end());
    std::sort(dto.clipIds.begin(), dto.clipIds.end());
    if (selection.getSelectedAutomationLaneId() != INVALID_AUTOMATION_LANE_ID)
        dto.automationLaneId = selection.getSelectedAutomationLaneId();
    if (selection.getSelectedAutomationClipId() != INVALID_AUTOMATION_CLIP_ID)
        dto.automationClipId = selection.getSelectedAutomationClipId();
    if (selection.hasNoteSelection()) {
        dto.noteClipId = selection.getNoteSelectionClipId();
        dto.noteIndices.reserve(selection.getNoteSelectionIndices().size());
        for (const auto index : selection.getNoteSelectionIndices())
            dto.noteIndices.push_back(static_cast<std::int64_t>(index));
    }
    return dto;
}

TransportDto makeTransportDto(MagdaApi& api) {
    assertMessageThread();

    const auto& transport = api.transport();
    return {transport.isPlaying(), transport.isRecording(), transport.isLoopEnabled(),
            transport.getPositionBeats()};
}

SessionDto makeSessionDto(MagdaApi& api) {
    assertMessageThread();

    SessionDto dto;
    for (const auto& track : api.tracks().getTracks()) {
        for (const auto clipId : api.clips().getClipsOnTrack(track.id)) {
            const auto* clip = api.clips().getClip(clipId);
            if (clip == nullptr || clip->view != ClipView::Session || clip->sceneIndex < 0)
                continue;
            dto.slots.push_back({track.id, clip->sceneIndex, clip->id,
                                 sessionStateName(api.session().getClipPlayState(clip->id))});
        }
    }
    std::sort(dto.slots.begin(), dto.slots.end(), [](const auto& a, const auto& b) {
        return a.sceneIndex != b.sceneIndex ? a.sceneIndex < b.sceneIndex : a.trackId < b.trackId;
    });
    return dto;
}

AutomationLaneDto makeAutomationLaneDto(const AutomationLaneInfo& lane) {
    AutomationLaneDto dto;
    dto.id = lane.id;
    dto.type = lane.type == AutomationLaneType::Absolute ? "absolute" : "clip_based";
    dto.name = lane.getDisplayName();
    dto.target.kind = juce::String(toString(lane.target.kind));
    if (!lane.target.isEditScoped() && lane.target.devicePath.trackId != INVALID_TRACK_ID)
        dto.target.trackId = lane.target.devicePath.trackId;
    if (lane.target.deviceId() != INVALID_DEVICE_ID)
        dto.target.deviceId = lane.target.deviceId();
    dto.target.parameterIndex = lane.target.paramIndex;
    dto.target.modId = lane.target.modId;
    dto.target.modParameterIndex = lane.target.modParamIndex;
    dto.target.sendBusIndex = lane.target.sendBusIndex;
    for (const auto& point : lane.absolutePoints) {
        dto.points.push_back(
            {point.id, point.beatPosition, point.value, curveName(point.curveType)});
    }
    dto.clipIds = lane.clipIds;
    return dto;
}

}  // namespace magda::remote
