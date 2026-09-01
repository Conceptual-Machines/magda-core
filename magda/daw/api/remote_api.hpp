#pragma once

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "../core/ChainNodePath.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/TypeIds.hpp"
#include "remote_scopes.hpp"

namespace magda {

class MagdaApi;
struct AutomationLaneInfo;
struct ClipInfo;
struct DeviceCatalogEntry;
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
    /**
     * The operation is real and the request is well formed, but this client's
     * grant does not cover it (#1860).
     *
     * Distinct from `UnknownOperation` on purpose. Hiding an operation a client
     * may not call would be security through obscurity over a socket that
     * already required a bearer token to reach, and it would make the failure
     * indistinguishable from a typo — so a client that asks for something beyond
     * its grant is told exactly that, and which scope would have allowed it.
     */
    PermissionDenied,
    ValidationFailed,
    NotFound,
    Conflict,
    Timeout,
    Cancelled,
    InternalError,
};

/**
 * @brief Monotonically increasing counter over committed project mutations.
 *
 * Bumped once per successful write operation, never reused, never decremented.
 * Serves two roles: optimistic concurrency (`RequestContext::expectedRevision`)
 * and the coalescing key for change notifications — a subscriber that saw
 * revision N asks for everything since N rather than for a stream of individual
 * callbacks.
 *
 * The value is process-scoped, not persisted: it orders mutations within one
 * run of MAGDA and carries no meaning across restarts.
 */
using Revision = std::uint64_t;

inline constexpr Revision INITIAL_REVISION = 0;

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

/**
 * @brief Public projection of a `ChainNodePath`.
 *
 * A device id alone does not identify a device: the main FX chain, the
 * post-fader list, and the mixer-analysis section each carry their own
 * `DeviceId` counter, so id 3 can exist in all three at once on one track.
 * `section` carries that discriminator and `steps` carries the route through
 * nested racks and chains, so this round-trips to exactly one device.
 *
 * Step types are strings rather than enum ordinals: this is a public wire
 * contract consumed by scripts and MCP clients, and it must not be coupled to
 * the internal `ChainStepType` ordering.
 */
struct DevicePathStepDto {
    juce::String type;  // "rack" | "chain" | "device"
    int id = -1;

    bool operator==(const DevicePathStepDto&) const = default;
};

struct DevicePathDto {
    TrackId trackId = INVALID_TRACK_ID;
    juce::String section{"fx"};  // "fx" | "post_fx" | "mixer_analysis"
    bool trackLevel = false;
    std::optional<DeviceId> topLevelDeviceId;
    std::vector<DevicePathStepDto> steps;

    bool operator==(const DevicePathDto&) const = default;
};

struct DeviceDto {
    DeviceId id = INVALID_DEVICE_ID;
    TrackId trackId = INVALID_TRACK_ID;
    // Immediate parents, for rendering the graph. `rackId`/`chainId` locate a
    // device one level up but cannot address it: `id` is unique only within a
    // section, and nesting can be arbitrarily deep. Use `devicePath` as the
    // address.
    std::optional<RackId> rackId;
    std::optional<ChainId> chainId;
    DevicePathDto devicePath;
    juce::String name;
    juce::String type;
    juce::String format;
    bool instrument = false;
    bool bypassed = false;
    double gainDb = 0.0;

    bool operator==(const DeviceDto&) const = default;
};

/**
 * @brief One parameter of a live device, in real units (Hz, dB, %).
 *
 * `index` is the address `devices.setParameter` takes. The three booleans
 * mirror the user's Configure Parameters customization: shown in the device
 * UI, pinned to the mini mixer, and opted in to AI/agent control. Internal
 * devices carry no per-parameter opt-in, so `aiAgentEnabled` is always true
 * there; for external plugins it is exactly the subset the user ticked.
 */
struct DeviceParameterDto {
    int index = -1;
    juce::String stableId;
    juce::String name;
    juce::String unit;
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.0;
    double currentValue = 0.0;
    double normalizedValue = 0.0;
    juce::String
        scale;  // "linear" | "logarithmic" | "exponential" | "discrete" | "boolean" | "fader_db"
    std::vector<juce::String> choices;  // labels for discrete parameters; empty otherwise
    bool visible = false;
    bool miniMixer = false;
    bool aiAgentEnabled = false;

    bool operator==(const DeviceParameterDto&) const = default;
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

/**
 * @brief A device the user could add, as opposed to one already on a track.
 *
 * `DeviceDto` describes an instance — it has a track, a path, and a bypass
 * state. This describes a *kind*, and the two are not interchangeable: a client
 * needs the catalogue to know what `catalogId` to ask for, and the graph to know
 * what is already there.
 *
 * `catalogId` is the whole address. `DeviceCatalogEntry` was already built to
 * carry no `fileOrIdentifier`, so this projection has nothing to strip: naming a
 * plugin never teaches a remote caller the filesystem layout of the machine
 * hosting it.
 */
struct DeviceCatalogEntryDto {
    juce::String catalogId;
    juce::String name;
    juce::String manufacturer;
    juce::String category;
    juce::String description;
    juce::String format;  // "vst3" | "au" | "lv2" | "internal"
    juce::String type;    // "instrument" | "effect" | "midi" | "analysis"
    bool instrument = false;

    bool operator==(const DeviceCatalogEntryDto&) const = default;
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
    // Absent for edit-scoped kinds (`tempo`), which address a global value.
    std::optional<DevicePathDto> devicePath;
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

/**
 * @brief Per-request identity, permission, limits, and concurrency expectations.
 *
 * Populated by the transport adapter and passed unchanged through dispatch to
 * the handler.
 */
struct RequestContext {
    /**
     * The transport's own handle for the caller — `ws:3:7`, `mcp:sess-…`.
     *
     * Unique per connection, issued by the transport, and never anything the
     * client asserted about itself. It scopes the idempotency cache, so two
     * clients that both number their requests from 1 cannot replay each other's
     * writes.
     */
    juce::String clientId;
    /**
     * What the client says it is: `cursor`, `claude-code`. Normalised, and the
     * key its grant is stored under (#1860).
     *
     * Self-declared, which is exactly as much as it sounds like. It records the
     * user's intent about a named client so a well-behaved one cannot exceed it
     * by accident; it is not proof of anything, because the bearer token that
     * admitted this request is readable by every process running as the user.
     * `clientId` is the transport's identifier and this is the client's claim,
     * and they are kept apart so nothing conflates the two.
     */
    juce::String clientName;
    /// `websocket` or `mcp`, for the audit record.
    juce::String transport;
    /// Idempotency key. Repeating a completed write with the same id returns
    /// the first response instead of applying the mutation twice.
    juce::String requestId;
    /// Optimistic concurrency: reject the write if the project has moved on.
    std::optional<Revision> expectedRevision;
    /**
     * What this caller may reach. Empty by default, which denies everything.
     *
     * Fail-closed is the point: a transport that forgot to fill this in refuses
     * every request loudly on its first test run, where one that defaulted to
     * full access would hand out the project silently and pass. Both adapters
     * populate it per request from `RemoteClientRegistry`, which is what makes
     * revoking a grant take effect on the next request rather than the next
     * restart.
     *
     * In-process callers that are not a remote client at all — the subscription
     * hub's snapshot projections, tests — set it explicitly.
     */
    ScopeSet scopes;
    /// Absolute deadline. Work still queued when it passes fails with Timeout
    /// rather than executing late.
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

/**
 * @brief What a handler returns: exactly one of a result value or an error.
 */
struct HandlerResult {
    std::optional<Error> error;
    juce::var value;
    /**
     * Whether this actually changed project state.
     *
     * A write operation can legitimately resolve to a no-op — clearing a lane
     * that is already empty, for instance. Reporting that as a committed
     * mutation would advance the revision and invalidate every other client's
     * `expectedRevision` for a request that changed nothing, so the handler
     * says so and the dispatcher leaves the revision alone. Ignored for reads.
     */
    bool mutated = true;

    static HandlerResult ok(juce::var value) {
        return {std::nullopt, std::move(value), true};
    }
    /// Succeeded, but nothing changed — see `mutated`.
    static HandlerResult unchanged(juce::var value) {
        return {std::nullopt, std::move(value), false};
    }
    static HandlerResult fail(ErrorCode code, const juce::String& message) {
        return {Error{code, message, {}}, {}, false};
    }
    static HandlerResult fail(Error error) {
        return {std::move(error), {}, false};
    }

    bool failed() const {
        return error.has_value();
    }
};

/**
 * @brief Executes one operation against the facade.
 *
 * Always invoked on the JUCE message thread, with input already validated
 * against the operation's `inputSchema`, so a handler can read its fields
 * without re-checking presence or type. A plain function pointer rather than
 * `std::function`: handlers are stateless free functions, and this keeps
 * `OperationDescriptor` trivially copyable.
 */
using OperationHandler = HandlerResult (*)(MagdaApi& api, const juce::var& input,
                                           const RequestContext& context);

struct OperationDescriptor {
    juce::String name;
    juce::String summary;
    OperationAccess access = OperationAccess::Read;
    /**
     * The one scope a client needs to call this (#1860).
     *
     * One rather than a set, because every operation this API has belongs to
     * exactly one thing the user would decide about, and a set would invite
     * combinations nobody can reason about from a settings checkbox. It sits on
     * the descriptor for the same reason `handler` does: a transport cannot
     * reach a different permission than the one declared, and there is one place
     * to look.
     *
     * Defaults to `Read`, which is the safe default only because it is paired
     * with a registry-wide assertion that every write declares something else —
     * a new write operation that forgets this is caught at startup rather than
     * by a client discovering it can edit.
     */
    Scope requiredScope = Scope::Read;
    juce::var inputSchema;
    juce::var outputSchema;
    /**
     * Implementation of this operation. Lives on the descriptor rather than in
     * a table keyed by name so a transport cannot reach a different
     * implementation than the one declared — there is only one place to look,
     * and the registry constructor asserts every declared operation has one.
     *
     * Null only for a `transportScoped` operation, which by definition has no
     * implementation that could run here.
     */
    OperationHandler handler = nullptr;

    /**
     * Executed by the transport adapter rather than by `RemoteApiService`.
     *
     * The subscription methods (#1857) are the reason this exists: subscribing
     * is per-connection state, and a connection is the one thing the dispatcher
     * deliberately knows nothing about. Their names, summaries, and schemas
     * still belong here — that is what stops the WebSocket and MCP adapters
     * declaring two different contracts for the same thing, and what keeps
     * `system.describe` a complete catalogue.
     *
     * Dispatching one through the service is an error, and says so, rather than
     * failing as an unknown operation: a client that reaches this has asked for
     * something real over a transport that cannot carry it.
     */
    bool transportScoped = false;
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

/**
 * @brief A JSON number as an exact integer in range, or nothing.
 *
 * JSON has one numeric type, so a field declared as a count arrives as a double
 * whenever it was written with a decimal point — and casting that to `int` or
 * `int64` truncates silently when it is fractional and is undefined behaviour
 * when it is out of range or not finite. Neither belongs on a path fed by
 * whatever a client chose to send, so the value has to be proved integral and
 * within bounds before it becomes one.
 *
 * Shared by every transport rather than reimplemented per adapter: getting the
 * `2^63` boundary wrong is silent, and one copy is one thing to get right.
 */
std::optional<juce::int64> jsonInteger(const juce::var& value, juce::int64 lowest,
                                       juce::int64 highest);
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
juce::var toJson(const DeviceCatalogEntryDto& dto);
juce::var toJson(const DeviceParameterDto& dto);
juce::var toJson(const SelectionDto& dto);
juce::var toJson(const TransportDto& dto);
juce::var toJson(const SessionSlotDto& dto);
juce::var toJson(const SessionDto& dto);
juce::var toJson(const AutomationPointDto& dto);
juce::var toJson(const DevicePathDto& dto);
juce::var toJson(const AutomationTargetDto& dto);
juce::var toJson(const AutomationLaneDto& dto);

/**
 * @brief Decode a device path from its wire form.
 *
 * Total rather than fallible: an unrecognised `section` or step `type` survives
 * as a string here and is rejected by `toChainNodePath`, which is the single
 * point where a path becomes an internal address.
 */
DevicePathDto devicePathFromJson(const juce::var& json);

std::optional<MidiNoteDto> midiNoteFromJson(const juce::var& json, Error& error);
std::optional<ProjectDto> projectFromJson(const juce::var& json, Error& error);
std::optional<TrackDto> trackFromJson(const juce::var& json, Error& error);
std::optional<ClipDto> clipFromJson(const juce::var& json, Error& error);
std::optional<DeviceDto> deviceFromJson(const juce::var& json, Error& error);
std::optional<ChainDto> chainFromJson(const juce::var& json, Error& error);
std::optional<RackDto> rackFromJson(const juce::var& json, Error& error);
std::optional<DeviceGraphDto> deviceGraphFromJson(const juce::var& json, Error& error);
std::optional<DeviceCatalogEntryDto> deviceCatalogEntryFromJson(const juce::var& json,
                                                                Error& error);
std::optional<DeviceParameterDto> deviceParameterFromJson(const juce::var& json, Error& error);
std::optional<SelectionDto> selectionFromJson(const juce::var& json, Error& error);
std::optional<TransportDto> transportFromJson(const juce::var& json, Error& error);
std::optional<SessionDto> sessionFromJson(const juce::var& json, Error& error);
std::optional<AutomationLaneDto> automationLaneFromJson(const juce::var& json, Error& error);

ProjectDto makeProjectDto(const ProjectInfo& project);
TrackDto makeTrackDto(const TrackInfo& track);
ClipDto makeClipDto(const ClipInfo& clip);
DeviceGraphDto makeDeviceGraphDto(const std::vector<TrackInfo>& tracks);
DeviceCatalogEntryDto makeDeviceCatalogEntryDto(const DeviceCatalogEntry& entry);
std::vector<DeviceParameterDto> makeDeviceParameterDtos(const DeviceInfo& device);
SelectionDto makeSelectionDto(MagdaApi& api);
TransportDto makeTransportDto(MagdaApi& api);
SessionDto makeSessionDto(MagdaApi& api);
AutomationLaneDto makeAutomationLaneDto(const AutomationLaneInfo& lane);

/**
 * @brief Project an internal path to its public form.
 *
 * Lifts a leading `Segment` step out into `section` so the remaining steps are
 * a uniform rack/chain/device route, and maps step types to strings.
 */
DevicePathDto makeDevicePathDto(const ChainNodePath& path);

/**
 * @brief Rebuild an internal path from its public form.
 *
 * Returns nullopt on an unrecognised `section` or step `type`. Round-trips
 * with `makeDevicePathDto` for every path the `ChainNodePath` factories
 * produce — including which of the three per-section `DeviceId` spaces the
 * leaf belongs to. An explicit leading `Segment(Fx)` step normalizes to the
 * implicit form, which is the same path by every accessor.
 */
std::optional<ChainNodePath> toChainNodePath(const DevicePathDto& dto);

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
