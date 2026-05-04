#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <memory>

#include "magda/daw/audio/MidiBridge.hpp"

namespace magda {

class MagdaApi;
class MidiBridge;

namespace scripting {

class LuaRuntime;

/**
 * Bridges raw MIDI from MidiBridge to a user Lua script's `on_midi(event)`.
 *
 * Lifecycle:
 *   ctor → registers as MidiBridge::RawMidiListener.
 *   loadScript(file) → creates a fresh LuaRuntime, registers the magda.*
 *                      bindings on it, and evals the script.
 *   onRawMidi (MIDI thread) → captures a POD copy of the event, posts to
 *                              the message thread via juce::MessageManager
 *                              ::callAsync. WeakReference guards against
 *                              the controller being destroyed before the
 *                              async fires.
 *   message thread → builds a Lua event table, calls `on_midi(e)` if the
 *                    script defined it. Errors are logged via juce::Logger;
 *                    the runtime stays alive so subsequent events keep
 *                    dispatching.
 *   dtor → unregisters from MidiBridge and tears down the runtime.
 *
 * One script at a time in v1. Reload to switch.
 */
class LuaController : public RawMidiListener {
  public:
    explicit LuaController(MagdaApi& api);
    ~LuaController() override;

    LuaController(const LuaController&) = delete;
    LuaController& operator=(const LuaController&) = delete;

    /** Register as a RawMidiListener on `bridge`. Tests can skip this and
     *  drive events synchronously through dispatchEventForTest instead. */
    void attach(MidiBridge& bridge);

    /** Unregister from any attached bridge. Idempotent. */
    void detach();

    /** Read `file`, create a runtime, register magda.* bindings, eval the
     *  script. Replaces any currently-loaded script. Returns true on success;
     *  on failure the previous script is also unloaded and lastError() carries
     *  the message. */
    bool loadScript(const juce::File& file);

    /** Drop the active script and runtime. Subsequent MIDI events become no-ops. */
    void unloadScript();

    /** Filename of the currently loaded script, or empty if none. */
    juce::String currentScriptName() const;

    /** Restrict on_midi to one selected DAW-protocol input. Empty means all inputs. */
    void setDawInputPort(const juce::String& port);

    /** Last load/eval error, or empty if the last loadScript succeeded. */
    juce::String lastError() const;

    // RawMidiListener (MIDI thread)
    void onRawMidi(const juce::String& deviceId, const juce::String& deviceName,
                   const juce::MidiMessage& msg) override;

    /** Test seam: synchronously dispatch an event without going through the
     *  MIDI thread → message thread bridge. */
    void dispatchEventForTest(const juce::String& deviceName, const juce::MidiMessage& msg);

    /** Test seam: synchronously fire one on_tick(dt) call. Production code
     *  uses a juce::Timer that drives this internally. */
    void tickForTest(double dt);

  private:
    void dispatchToLua(const juce::String& deviceName, const juce::MidiMessage& msg);

    /** Call a global Lua function with no args. No-op if not defined.
     *  Errors are logged. */
    void callOptionalNullary(const char* name);

    /** Call on_tick(dt) if defined. */
    void callOnTick(double dt);

    class TickTimer;

    MagdaApi& api_;
    MidiBridge* bridge_ = nullptr;  // non-owning; nullptr until attach() is called
    std::unique_ptr<LuaRuntime> rt_;
    juce::String currentScriptName_;
    juce::String dawInputPort_;
    juce::String lastError_;
    std::unique_ptr<TickTimer> tickTimer_;
    juce::int64 lastTickMs_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(LuaController)
};

}  // namespace scripting
}  // namespace magda
