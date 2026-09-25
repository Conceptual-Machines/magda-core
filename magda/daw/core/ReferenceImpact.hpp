#pragma once

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "AutomationInfo.hpp"
#include "ChainNodePath.hpp"
#include "ControlTarget.hpp"
#include "TrackInfo.hpp"

namespace magda {

/** The persisted reference classes a chain or device replacement can affect. */
enum class ReferenceKind {
    Automation,
    MacroLink,
    ModulatorLink,
    ControllerBinding,
    Sidechain,
    Routing,
};

/** A safe, model-level address. It cannot carry pointers, filesystem paths, plugin ids, or state.
 */
enum class ReferenceAddressKind {
    Track,
    Device,
    Parameter,
    AutomationLane,
    Macro,
    MacroLink,
    Modulator,
    ModulatorLink,
    ControllerBinding,
    Sidechain,
    Routing,
};

enum class ReferenceRouteKind {
    AudioInput,
    MidiInput,
    Send,
    MultiOutput,
    TrackVolume,
    TrackPan,
    Tempo,
};

struct ReferenceAddress {
    ReferenceAddressKind kind = ReferenceAddressKind::Track;
    std::optional<TrackId> trackId;
    std::optional<ChainNodePath> devicePath;
    std::optional<AutomationLaneId> automationLaneId;
    std::optional<MacroId> macroId;
    std::optional<ModId> modId;
    std::optional<int> linkIndex;
    std::optional<int> parameterIndex;
    juce::String parameterStableId;
    juce::String bindingId;
    std::optional<ReferenceRouteKind> route;
    std::optional<int> routeIndex;

    bool operator==(const ReferenceAddress&) const = default;
};

struct ReferenceDescriptor {
    ReferenceKind kind = ReferenceKind::Automation;
    ReferenceAddress source;
    ReferenceAddress target;

    bool operator==(const ReferenceDescriptor&) const = default;
};

/** A controller/alias reference after its owning registry has resolved it. */
struct BoundControlReference {
    juce::String id;
    ControlTarget target;
};

struct ReferenceInventorySources {
    std::span<const TrackInfo> tracks;
    const TrackInfo* master = nullptr;
    std::span<const AutomationLaneInfo> lanes;
    std::span<const BoundControlReference> bound;
};

/**
 * Inventory every durable reference class relevant to device/chain replacement.
 * Pure over the supplied snapshot: no singleton access and no project mutation.
 */
std::vector<ReferenceDescriptor> inventoryReferences(const ReferenceInventorySources& sources);

/** Select references whose source or target is one of the replaced nodes. */
std::vector<ReferenceDescriptor> referencesAffectedBy(
    std::span<const ReferenceDescriptor> inventory, std::span<const ChainNodePath> nodePaths);

enum class ReferencePolicy { Reject, Preserve, Remap, Drop };

class ReferencePolicySet {
  public:
    explicit ReferencePolicySet(ReferencePolicy fallback = ReferencePolicy::Reject);

    ReferencePolicySet& set(ReferenceKind kind, ReferencePolicy policy);
    ReferencePolicy forKind(ReferenceKind kind) const;

  private:
    static constexpr std::size_t kKindCount = static_cast<std::size_t>(ReferenceKind::Routing) + 1;
    std::array<ReferencePolicy, kKindCount> policies_;
};

/**
 * One proposed target substitution. Both identities must be non-empty and
 * equal before the planner will accept it; addresses or parameter indices do
 * not prove compatibility by themselves.
 */
struct ReferenceTargetMapping {
    ReferenceAddress from;
    ReferenceAddress to;
    juce::String sourceStableIdentity;
    juce::String targetStableIdentity;
};

enum class ReferenceImpactReason {
    PolicyPreserve,
    StableIdentityMatch,
    PolicyDrop,
    PolicyReject,
    NoProvenRemap,
    MissingStableIdentity,
    StableIdentityMismatch,
};

struct PlannedReferenceImpact {
    ReferenceDescriptor reference;
    ReferenceImpactReason reason = ReferenceImpactReason::PolicyPreserve;

    bool operator==(const PlannedReferenceImpact&) const = default;
};

struct PlannedReferenceRemap {
    ReferenceDescriptor reference;
    ReferenceAddress newTarget;
    ReferenceImpactReason reason = ReferenceImpactReason::StableIdentityMatch;

    bool operator==(const PlannedReferenceRemap&) const = default;
};

struct ReferenceImpactPlan {
    std::vector<PlannedReferenceImpact> preserved;
    std::vector<PlannedReferenceRemap> remapped;
    std::vector<PlannedReferenceImpact> dropped;
    std::vector<PlannedReferenceImpact> rejected;

    bool canCommit() const {
        return rejected.empty();
    }
};

/** Build the complete decision before a caller mutates any project state. */
ReferenceImpactPlan planReferenceImpacts(std::span<const ReferenceDescriptor> references,
                                         std::span<const ReferenceTargetMapping> mappings,
                                         const ReferencePolicySet& policy);

const char* toString(ReferenceKind kind);
const char* toString(ReferenceAddressKind kind);
const char* toString(ReferenceRouteKind kind);
const char* toString(ReferenceImpactReason reason);

}  // namespace magda
