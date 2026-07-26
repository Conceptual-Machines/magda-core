#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "../core/ClipTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

class MagdaApi;
struct AutomationLaneInfo;
struct ClipInfo;
struct DeviceInfo;
struct ProjectInfo;
struct TrackInfo;

namespace remote {

inline constexpr int API_VERSION_MAJOR = 1;
inline constexpr int API_VERSION_MINOR = 0;
inline constexpr std::string_view API_VERSION = "1.0";

// Request dispatch and projections that read MagdaApi live state must run on
// JUCE's message thread. Transport adapters are responsible for hopping to it
// before validation, execution, or response projection.

enum class ErrorCode {
    InvalidRequest,
    UnknownOperation,
    ValidationFailed,
    NotFound,
    Conflict,
    InternalError,
};

struct ValidationIssue {
    juce::String path;
    juce::String code;
    juce::String message;

    bool operator==(const ValidationIssue&) const = default;
};

struct Error {
    ErrorCode code = ErrorCode::InternalError;
    juce::String message;
    std::vector<ValidationIssue> issues;

    bool operator==(const Error&) const = default;
};

struct MidiNoteDto {
    int note = 60;
    int velocity = 100;
    double startBeat = 0.0;
    double lengthBeats = 1.0;

    bool operator==(const MidiNoteDto&) const = default;
};

struct ProjectDto {
    juce::String name;
    double tempo = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    double sampleRate = 44100.0;
    int timelineLengthBars = 256;
    int keyRoot = -1;
    juce::String keyQuality = "major";
    bool loopEnabled = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;

    bool operator==(const ProjectDto&) const = default;
};

struct TrackDto {
    TrackId id = INVALID_TRACK_ID;
    juce::String type;
    juce::String name;
    std::uint32_t colourArgb = 0;
    std::optional<TrackId> parentId;
    std::vector<TrackId> childIds;
    double volume = 1.0;
    double pan = 0.0;
    bool muted = false;
    bool soloed = false;
    bool recordArmed = false;
    bool frozen = false;
    juce::String audioInputDevice;
    juce::String midiInputDevice;
    juce::String audioOutputDevice;
    juce::String midiOutputDevice;

    bool operator==(const TrackDto&) const = default;
};

struct ClipDto {
    ClipId id = INVALID_CLIP_ID;
    TrackId trackId = INVALID_TRACK_ID;
    juce::String type;
    juce::String view;
    juce::String name;
    std::uint32_t colourArgb = 0;
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    bool enabled = true;
    std::optional<int> sceneIndex;
    juce::String launchMode;
    juce::String launchQuantize;
    juce::String followAction;
    std::vector<MidiNoteDto> notes;

    bool operator==(const ClipDto&) const = default;
};

struct DeviceDto {
    DeviceId id = INVALID_DEVICE_ID;
    TrackId trackId = INVALID_TRACK_ID;
    std::optional<RackId> rackId;
    std::optional<ChainId> chainId;
    juce::String name;
    juce::String type;
    juce::String format;
    bool instrument = false;
    bool bypassed = false;
    double gainDb = 0.0;

    bool operator==(const DeviceDto&) const = default;
};

struct ChainDto {
    ChainId id = INVALID_CHAIN_ID;
    RackId rackId = INVALID_RACK_ID;
    juce::String name;
    int outputIndex = 0;
    bool muted = false;
    bool solo = false;
    bool bypassed = false;
    double volumeDb = 0.0;
    double pan = 0.0;
    std::vector<DeviceId> deviceIds;
    std::vector<RackId> nestedRackIds;

    bool operator==(const ChainDto&) const = default;
};

struct RackDto {
    RackId id = INVALID_RACK_ID;
    TrackId trackId = INVALID_TRACK_ID;
    std::optional<RackId> parentRackId;
    std::optional<ChainId> parentChainId;
    juce::String name;
    bool bypassed = false;
    double volumeDb = 0.0;
    double pan = 0.0;
    std::vector<ChainId> chainIds;

    bool operator==(const RackDto&) const = default;
};

struct DeviceGraphDto {
    std::vector<DeviceDto> devices;
    std::vector<RackDto> racks;
    std::vector<ChainDto> chains;

    bool operator==(const DeviceGraphDto&) const = default;
};

struct SelectionDto {
    std::optional<TrackId> trackId;
    std::optional<ClipId> clipId;
    std::vector<ClipId> clipIds;
    std::optional<AutomationLaneId> automationLaneId;
    std::optional<AutomationClipId> automationClipId;
    std::optional<ClipId> noteClipId;
    std::vector<std::int64_t> noteIndices;

    bool operator==(const SelectionDto&) const = default;
};

struct TransportDto {
    bool playing = false;
    bool recording = false;
    bool loopEnabled = false;
    double positionBeats = 0.0;

    bool operator==(const TransportDto&) const = default;
};

struct SessionSlotDto {
    TrackId trackId = INVALID_TRACK_ID;
    int sceneIndex = -1;
    ClipId clipId = INVALID_CLIP_ID;
    juce::String state;

    bool operator==(const SessionSlotDto&) const = default;
};

struct SessionDto {
    std::vector<SessionSlotDto> slots;

    bool operator==(const SessionDto&) const = default;
};

struct AutomationPointDto {
    AutomationPointId id = INVALID_AUTOMATION_POINT_ID;
    double beatPosition = 0.0;
    double value = 0.0;
    juce::String curve;

    bool operator==(const AutomationPointDto&) const = default;
};

struct AutomationTargetDto {
    juce::String kind;
    std::optional<TrackId> trackId;
    std::optional<DeviceId> deviceId;
    int parameterIndex = -1;
    int modId = -1;
    int modParameterIndex = -1;
    int sendBusIndex = -1;

    bool operator==(const AutomationTargetDto&) const = default;
};

struct AutomationLaneDto {
    AutomationLaneId id = INVALID_AUTOMATION_LANE_ID;
    juce::String type;
    juce::String name;
    AutomationTargetDto target;
    std::vector<AutomationPointDto> points;
    std::vector<AutomationClipId> clipIds;

    bool operator==(const AutomationLaneDto&) const = default;
};

enum class OperationAccess { Read, Write };

struct OperationDescriptor {
    juce::String name;
    juce::String summary;
    OperationAccess access = OperationAccess::Read;
    juce::var inputSchema;
    juce::var outputSchema;
};

class OperationRegistry {
  public:
    static const OperationRegistry& instance();

    const std::vector<OperationDescriptor>& operations() const;
    const OperationDescriptor* find(const juce::String& name) const;
    juce::var describe() const;

  private:
    OperationRegistry();

    std::vector<OperationDescriptor> operations_;
};

juce::String toString(ErrorCode code);
juce::var toJson(const Error& error);
juce::var successEnvelope(const juce::var& result);
juce::var errorEnvelope(const Error& error);

std::vector<ValidationIssue> validateJson(const juce::var& value, const juce::var& schema,
                                          const juce::String& path = "$");
std::optional<Error> validateOperationInput(const OperationDescriptor& operation,
                                            const juce::var& input);

juce::var toJson(const MidiNoteDto& dto);
juce::var toJson(const ProjectDto& dto);
juce::var toJson(const TrackDto& dto);
juce::var toJson(const ClipDto& dto);
juce::var toJson(const DeviceDto& dto);
juce::var toJson(const ChainDto& dto);
juce::var toJson(const RackDto& dto);
juce::var toJson(const DeviceGraphDto& dto);
juce::var toJson(const SelectionDto& dto);
juce::var toJson(const TransportDto& dto);
juce::var toJson(const SessionSlotDto& dto);
juce::var toJson(const SessionDto& dto);
juce::var toJson(const AutomationPointDto& dto);
juce::var toJson(const AutomationTargetDto& dto);
juce::var toJson(const AutomationLaneDto& dto);

std::optional<MidiNoteDto> midiNoteFromJson(const juce::var& json, Error& error);
std::optional<ProjectDto> projectFromJson(const juce::var& json, Error& error);
std::optional<TrackDto> trackFromJson(const juce::var& json, Error& error);
std::optional<ClipDto> clipFromJson(const juce::var& json, Error& error);
std::optional<DeviceDto> deviceFromJson(const juce::var& json, Error& error);
std::optional<ChainDto> chainFromJson(const juce::var& json, Error& error);
std::optional<RackDto> rackFromJson(const juce::var& json, Error& error);
std::optional<DeviceGraphDto> deviceGraphFromJson(const juce::var& json, Error& error);
std::optional<SelectionDto> selectionFromJson(const juce::var& json, Error& error);
std::optional<TransportDto> transportFromJson(const juce::var& json, Error& error);
std::optional<SessionDto> sessionFromJson(const juce::var& json, Error& error);
std::optional<AutomationLaneDto> automationLaneFromJson(const juce::var& json, Error& error);

ProjectDto makeProjectDto(const ProjectInfo& project);
TrackDto makeTrackDto(const TrackInfo& track);
ClipDto makeClipDto(const ClipInfo& clip);
DeviceGraphDto makeDeviceGraphDto(const std::vector<TrackInfo>& tracks);
SelectionDto makeSelectionDto(MagdaApi& api);
TransportDto makeTransportDto(MagdaApi& api);
SessionDto makeSessionDto(MagdaApi& api);
AutomationLaneDto makeAutomationLaneDto(const AutomationLaneInfo& lane);

// makeSelectionDto, makeTransportDto, and makeSessionDto read MagdaApi live
// state and assert the JUCE message thread. Tests drive them from the Catch2
// runner thread with no MessageManager, so they suspend the assertion at
// runtime for their scope; application code never changes this.
void setMessageThreadAssertionEnabled(bool enabled);
bool isMessageThreadAssertionEnabled();

class ScopedMessageThreadAssertionDisabler {
  public:
    ScopedMessageThreadAssertionDisabler();
    ~ScopedMessageThreadAssertionDisabler();

  private:
    bool previous_ = true;
};

}  // namespace remote
}  // namespace magda
