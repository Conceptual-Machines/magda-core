#pragma once

#include "remote_api.hpp"

namespace magda {
class MagdaApi;

namespace remote::handlers {

/**
 * @brief Implementations behind the operations declared in `OperationRegistry`.
 *
 * One free function per declared operation, wired into its `OperationDescriptor`
 * at registry construction. They are declared here rather than defined inline in
 * the registry constructor purely to keep that constructor readable — the
 * descriptor still owns the pointer, so there is no name-keyed lookup a
 * transport could diverge from.
 *
 * Every handler runs on the JUCE message thread with input already validated
 * against the operation's `inputSchema`. That means a handler may read a
 * required field directly: schema validation has established it is present and
 * of the declared type. What it must still check is *semantic* validity —
 * whether the id names something that exists — which is where `NotFound` and
 * `Conflict` come from.
 */

// System
HandlerResult systemDescribe(MagdaApi&, const juce::var&, const RequestContext&);

// Project
HandlerResult projectGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSetTempo(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSetTimeSignature(MagdaApi&, const juce::var&, const RequestContext&);

// Tracks
HandlerResult tracksList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksDelete(MagdaApi&, const juce::var&, const RequestContext&);

// Clips
HandlerResult clipsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsCreateMidi(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsAddMidiNote(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsDelete(MagdaApi&, const juce::var&, const RequestContext&);

// Devices and racks
HandlerResult devicesList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesCatalog(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksRemove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksSetBypassed(MagdaApi&, const juce::var&, const RequestContext&);

// Selection
HandlerResult selectionGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult selectionSet(MagdaApi&, const juce::var&, const RequestContext&);

// Transport
HandlerResult transportGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult transportPlay(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult transportStop(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult transportSetRecording(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult transportSetLoopEnabled(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult transportSeek(MagdaApi&, const juce::var&, const RequestContext&);

// Session
HandlerResult sessionGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionLaunchClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionStopClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionStopTrack(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionStopAll(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionLaunchScene(MagdaApi&, const juce::var&, const RequestContext&);

// Automation
HandlerResult automationListLanes(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationGetLane(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationCreateLane(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationAddPoint(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationClearLane(MagdaApi&, const juce::var&, const RequestContext&);

}  // namespace remote::handlers
}  // namespace magda
