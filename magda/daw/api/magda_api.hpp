#pragma once

namespace magda {

class SelectionApi;
class AutomationApi;
class AliasApi;
class TrackApi;
class ClipApi;
class SessionApi;
class ProjectApi;
class UndoApi;
class MidiApi;
class TransportApi;
class FocusedApi;

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
 */
class MagdaApi {
  public:
    virtual ~MagdaApi() = default;

    virtual SelectionApi& selection() = 0;
    virtual AutomationApi& automation() = 0;
    virtual AliasApi& aliases() = 0;
    virtual TrackApi& tracks() = 0;
    virtual ClipApi& clips() = 0;
    virtual SessionApi& session() = 0;
    virtual ProjectApi& project() = 0;
    virtual UndoApi& undo() = 0;
    virtual MidiApi& midi() = 0;
    virtual TransportApi& transport() = 0;
    virtual FocusedApi& focused() = 0;
};

}  // namespace magda
