#pragma once

extern "C" {
struct lua_State;
}

namespace magda {

class MagdaApi;

namespace scripting {

/**
 * Install the `magda.*` namespace into `L`, wired to `api`.
 *
 * After this call, Lua scripts can use:
 *   magda.log.{info,warn,error}      — juce::Logger pass-through
 *   magda.selection.{...}            — SelectionApi
 *   magda.tracks.{...}               — TrackApi
 *   magda.clips.{...}                — ClipApi
 *   magda.project.info()             — ProjectApi snapshot
 *
 * The MagdaApi reference is borrowed — its lifetime must outlive `L`. The
 * caller (e.g. AIChatConsoleContent in the live app, or the test harness)
 * is responsible for that ordering.
 *
 * Threading: every binding routes through MagdaApi, which is message-thread
 * only. The caller must guarantee Lua execution happens on the message
 * thread. #592 handles MIDI-thread → message-thread dispatch.
 *
 * Idempotent: calling twice on the same state replaces the registered
 * MagdaApi pointer and re-creates the `magda` table.
 */
void registerMagdaApi(lua_State* L, MagdaApi& api);

}  // namespace scripting
}  // namespace magda
