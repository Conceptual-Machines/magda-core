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
HandlerResult engineHealth(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult metersRead(MagdaApi&, const juce::var&, const RequestContext&);

// Asynchronous jobs
HandlerResult jobsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult jobsGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult jobsCancel(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult fileHandlesList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult fileHandlesRevoke(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult engineRenderRange(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult engineMasterCaptureStart(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult engineMasterCaptureStop(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult engineMasterCaptureStatus(MagdaApi&, const juce::var&, const RequestContext&);

// Project
HandlerResult projectGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectNew(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectClose(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectOpen(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSaveAs(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSave(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSetTempo(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSetTimeSignature(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult projectSetLoopRange(MagdaApi&, const juce::var&, const RequestContext&);

// Saved track-chain presets
HandlerResult trackPresetsList(MagdaApi&, const juce::var&, const RequestContext&);

// Tracks
HandlerResult tracksList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksCreateFromPreset(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksApplyPreset(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksDelete(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksGroup(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult tracksMove(MagdaApi&, const juce::var&, const RequestContext&);

// Track audio/MIDI routing
HandlerResult routingListEndpoints(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult routingGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult routingSet(MagdaApi&, const juce::var&, const RequestContext&);

// Track sends
HandlerResult sendsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sendsCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sendsUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sendsRemove(MagdaApi&, const juce::var&, const RequestContext&);

// Device/rack sidechains
HandlerResult sidechainsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sidechainsGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sidechainsSet(MagdaApi&, const juce::var&, const RequestContext&);

// Singleton chord track
HandlerResult chordTrackGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chordTrackEnsure(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chordTrackReplaceProgression(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chordTrackDetect(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chordTrackExtract(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chordTrackSendToTrack(MagdaApi&, const juce::var&, const RequestContext&);

// Clips
HandlerResult clipsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsCreateMidi(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsAddMidiNote(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsListMidiEvents(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsAddMidiEvents(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsUpdateMidiEvents(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsReplaceMidiEvents(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsDeleteMidiEvents(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsDelete(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsMove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsResize(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsDuplicate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsTranspose(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsQuantize(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult clipsSliceNotes(MagdaApi&, const juce::var&, const RequestContext&);

// Devices and racks
HandlerResult devicesList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsSetDevice(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsSetSample(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsClear(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsSwap(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult padsUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesCatalog(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicePresetsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesApplyPreset(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesReplace(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesAdd(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesRemove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesMove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesSetBypassed(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesListParameters(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesSetParameter(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesSetParameterConfig(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult devicesOpenEditor(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult modsList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult modsCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult modsUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult modsRemove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult modsLink(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult modsUnlink(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult macrosList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult macrosSetValue(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult macrosLink(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult macrosUnlink(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksRemove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksSetBypassed(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult racksUpdate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chainsCreate(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chainsRemove(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult chainsUpdate(MagdaApi&, const juce::var&, const RequestContext&);

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
HandlerResult transportSeekRelative(MagdaApi&, const juce::var&, const RequestContext&);

// Session
HandlerResult sessionGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionLaunchClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionStopClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionStopTrack(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionStopAll(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionLaunchScene(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionUpdateClipSettings(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionReturnToArrangement(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionCreateScene(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionUpdateScene(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionMoveScene(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionDuplicateScene(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult sessionDeleteScene(MagdaApi&, const juce::var&, const RequestContext&);

// Automation
HandlerResult automationListLanes(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationGetLane(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationCreateLane(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationAddPoint(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationSetPoints(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationClearLane(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationDeleteLane(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationListClips(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationGetClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationCreateClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationDeleteClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationMoveClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationResizeClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationDuplicateClip(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult automationUpdateClip(MagdaApi&, const juce::var&, const RequestContext&);

// Grooves
HandlerResult groovesList(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult groovesUpsert(MagdaApi&, const juce::var&, const RequestContext&);

// Focused device macros
HandlerResult focusedGet(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult focusedSetMacro(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult focusedCycleDevice(MagdaApi&, const juce::var&, const RequestContext&);

// Hardware MIDI out
HandlerResult midiListOutputPorts(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult midiSend(MagdaApi&, const juce::var&, const RequestContext&);
HandlerResult midiSendSysEx(MagdaApi&, const juce::var&, const RequestContext&);

}  // namespace remote::handlers
}  // namespace magda
