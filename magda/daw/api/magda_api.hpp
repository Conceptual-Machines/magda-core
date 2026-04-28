#pragma once

namespace magda {

class SelectionApi;
class AutomationApi;
class AliasApi;

/**
 * Programmatic facade for MAGDA's DAW state.
 *
 * Composed of focused sub-interfaces, one per DAW concept. Consumers
 * (the agent layer today; Lua / controllers / CLI in future) take a
 * MagdaApi& and route every state read or mutation through these
 * accessors instead of reaching into singletons.
 *
 * The live implementation (MagdaApiLive) forwards every call to the
 * existing TrackManager/ClipManager/etc. singletons — this is the
 * abstraction boundary, not a behavioural change.
 *
 * PR1 wires only the three sub-interfaces AutomationExecutor needs.
 * Track/Clip/Project/Undo land in subsequent PRs as the matching
 * call sites migrate.
 */
class MagdaApi {
  public:
    virtual ~MagdaApi() = default;

    virtual SelectionApi& selection() = 0;
    virtual AutomationApi& automation() = 0;
    virtual AliasApi& aliases() = 0;
};

}  // namespace magda
