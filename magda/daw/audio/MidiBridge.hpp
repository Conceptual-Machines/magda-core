#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <unordered_map>

#include "../core/MidiTypes.hpp"
#include "../core/TypeIds.hpp"
#include "midi/MidiEventQueue.hpp"
#include "midi/RecordingNoteQueue.hpp"

namespace magda {

namespace te = tracktion;

// Forward declarations
class AudioBridge;
struct TrackMeters;

/**
 * @brief Bridges MAGDA's MIDI model to Tracktion Engine's MIDI system.
 *
 * Enumerates and manages MIDI input/output devices, routes inputs to tracks,
 * and monitors MIDI activity for visualization, with thread-safe
 * communication between the UI and audio threads. The MIDI counterpart to
 * AudioBridge.
 */
// ============================================================================
// RawMidiListener
// ============================================================================

/**
 * @brief Receives raw MIDI messages from every open input device.
 *
 * Called at the top of MidiBridge::handleIncomingMidiMessage, before routing
 * or monitoring logic. Intended for ControllerRouter to tap controller ports
 * without interfering with the existing track-routing pipeline.
 *
 * Called from the MIDI callback thread -- implementations must be lock-free.
 */
struct RawMidiListener {
    virtual ~RawMidiListener() = default;
    // deviceId: JUCE identifier (opaque, OS-specific format).
    // deviceName: human-readable display name (also provided so listeners can
    //             match via magda::midi::matches without a separate lookup).
    virtual void onRawMidi(const juce::String& deviceId, const juce::String& deviceName,
                           const juce::MidiMessage& msg) = 0;
};

/// The QWERTY keyboard's device identifier on both engines: the fork names its
/// virtual device this and TE uses the name as the ID, so a track routed to it
/// on one engine is routed to it on the other.
inline constexpr const char* kQwertyMidiDeviceId = "QWERTY Keyboard";

/**
 * @brief Where live MIDI goes when something other than the fork renders (#2579).
 *
 * Called from the MIDI callback thread and the message thread; never the audio thread.
 */
class LiveMidiSink {
  public:
    virtual ~LiveMidiSink() = default;
    virtual void pushMidi(const juce::String& deviceId, const juce::MidiMessage& message) = 0;
    virtual void audition(TrackId trackId, const juce::MidiMessage& message) = 0;
};

// ============================================================================
// MidiBridge
// ============================================================================

class MidiBridge : public juce::MidiInputCallback {
  public:
    explicit MidiBridge(te::Engine& engine);
    ~MidiBridge() override;

    // Explicitly delete move operations (copy operations deleted by JUCE macro)
    MidiBridge(MidiBridge&&) = delete;
    MidiBridge& operator=(MidiBridge&&) = delete;

    /**
     * @brief Set the AudioBridge reference used for triggering MIDI activity
     * and track lookup. Must be called after AudioBridge is created.
     */
    void setAudioBridge(AudioBridge* audioBridge);

    /**
     * @brief Clear the AudioBridge pointer before it's destroyed, to avoid a
     * dangling pointer between shutdown steps.
     */
    void clearAudioBridge() {
        audioBridge_ = nullptr;
    }

    /**
     * @brief Set the shared meters object that feeds the MIDI activity light.
     *
     * Lets the activity light work with no AudioBridge, under the magda engine.
     */
    void setMeters(TrackMeters* meters) {
        meters_ = meters;
    }

    /**
     * @brief Set where live MIDI goes under the magda engine, or clear it.
     *
     * Clearing (nullptr) waits for any in-flight handleIncomingMidiMessage
     * call to drain, the way stopAllInputs waits on activeCallbacks_.
     */
    void setLiveSink(LiveMidiSink* sink);

    /**
     * @brief Enable/disable forwarding MIDI to instrument plugins.
     *
     * When enabled, incoming MIDI is injected into Tracktion tracks.
     * @param enabled True to forward MIDI to plugins
     */
    void setMidiToPluginsEnabled(bool enabled) {
        forwardMidiToPlugins_ = enabled;
    }

    // =========================================================================
    // MIDI Device Enumeration
    // =========================================================================

    /**
     * @brief Get all available MIDI input devices
     * @return Vector of device info (id, name, enabled status)
     */
    std::vector<MidiDeviceInfo> getAvailableMidiInputs() const;

    /**
     * @brief Notify listeners that the MIDI device list has changed.
     * Call after creating/destroying virtual devices so routing selectors refresh.
     */
    void notifyMidiDeviceListChanged() {
        midiDeviceListListeners_.call([](Listener& l) { l.midiDeviceListChanged(); });
    }

    struct Listener {
        virtual ~Listener() = default;
        virtual void midiDeviceListChanged() = 0;
    };

    void addMidiDeviceListListener(Listener* l) {
        midiDeviceListListeners_.add(l);
    }
    void removeMidiDeviceListListener(Listener* l) {
        midiDeviceListListeners_.remove(l);
    }

    /**
     * @brief Get all available MIDI output devices
     * @return Vector of device info
     */
    static std::vector<MidiDeviceInfo> getAvailableMidiOutputs();

    // =========================================================================
    // MIDI Output (host → device)
    // =========================================================================

    /**
     * @brief Send a MIDI message to an output device, opening it lazily on
     *        first use.
     *
     * `deviceNameOrId` matches against either the device's display name
     * (what scripts see in `e.port`) or its JUCE identifier. The output
     * stays open for the lifetime of MidiBridge so subsequent sends are
     * cheap.
     *
     * Thread-safe. Returns false if the device cannot be found or opened.
     */
    bool sendMidi(const juce::String& deviceNameOrId, const juce::MidiMessage& msg);

    /**
     * @brief Convenience: build a SysEx message from a byte payload (without
     *        F0/F7 framing) and dispatch via sendMidi.
     */
    bool sendSysEx(const juce::String& deviceNameOrId, const juce::uint8* data, size_t numBytes);

    /** Inject a message into one track's live plugin chain. Used by controller
     *  script User/Note modes whose physical input is otherwise surface-only. */
    bool injectMidiToTrack(TrackId trackId, const juce::MidiMessage& msg);

    // =========================================================================
    // MIDI Device Enable/Disable
    // =========================================================================

    /**
     * @brief Enable a MIDI input device globally
     * @param deviceId Device identifier from MidiDeviceInfo
     */
    void enableMidiInput(const juce::String& deviceId);

    /**
     * @brief Disable a MIDI input device globally
     * @param deviceId Device identifier
     */
    void disableMidiInput(const juce::String& deviceId);

    /**
     * @brief Stop all MIDI inputs and wait for in-flight callbacks to drain.
     * Call before destruction to avoid CoreMIDI race conditions.
     */
    void stopAllInputs();

    /**
     * @brief Check if a MIDI input is enabled
     */
    bool isMidiInputEnabled(const juce::String& deviceId) const;

    // =========================================================================
    // Track MIDI Routing
    // =========================================================================

    /**
     * @brief Set MIDI input source for a track
     * @param trackId MAGDA track ID
     * @param midiDeviceId MIDI device ID (empty string = no input)
     */
    void setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId);

    /**
     * @brief Get current MIDI input source for a track
     * @return Device ID, or empty string if no input
     */
    juce::String getTrackMidiInput(TrackId trackId) const;

    /**
     * @brief Clear MIDI input routing for a track
     */
    void clearTrackMidiInput(TrackId trackId);

    // =========================================================================
    // MIDI Monitoring (for visualization)
    // =========================================================================

    /**
     * @brief Callback when MIDI note event received on a track
     * Parameters: (trackId, noteEvent)
     * Called from audio thread - keep handlers lightweight!
     */
    std::function<void(TrackId, const MidiNoteEvent&)> onNoteEvent;

    /**
     * @brief Callback when MIDI CC event received on a track
     * Parameters: (trackId, ccEvent)
     * Called from audio thread - keep handlers lightweight!
     */
    std::function<void(TrackId, const MidiCCEvent&)> onCCEvent;

    /**
     * @brief Start monitoring MIDI events for a track
     * Enables callbacks for note/CC events
     */
    void startMonitoring(TrackId trackId);

    /**
     * @brief Stop monitoring MIDI events for a track
     */
    void stopMonitoring(TrackId trackId);

    /**
     * @brief Check if monitoring is active for a track
     */
    bool isMonitoring(TrackId trackId) const;

    /**
     * @brief Get the global MIDI event queue for the debug monitor
     * Audio thread pushes, UI thread reads.
     */
    MidiEventQueue& getGlobalEventQueue() {
        return globalEventQueue_;
    }

    void setRecordingQueue(RecordingNoteQueue* queue, std::atomic<double>* transportPos);

    // =========================================================================
    // Raw MIDI listener (for ControllerRouter)
    // =========================================================================

    /**
     * @brief Subscribe to raw MIDI from every open input device.
     *
     * Thread-safe. The listener is called from the MIDI callback thread;
     * implementations must be lock-free.
     */
    void addRawMidiListener(RawMidiListener* listener);
    void removeRawMidiListener(RawMidiListener* listener);

    /**
     * @brief Fan a QWERTY-synthesized note out to the UI layer.
     *
     * Mirrors the physical-MIDI fan-out in handleIncomingMidiMessage:
     *   - Fires MIDI activity + note-on/off UI on EVERY track whose MIDI
     *     input routes this virtual device (or "all"), regardless of
     *     record-arm state.
     *   - Pushes to the recording preview queue ONLY for armed tracks.
     *
     * @param sourceDeviceId TE device ID of the virtual device that produced
     *                       the note (typically the QWERTY keyboard).
     */
    void broadcastSynthesizedNote(const juce::String& sourceDeviceId, int noteNumber, int velocity,
                                  bool isNoteOn);

    /**
     * @brief Play a QWERTY-keyboard note through whichever engine is live, and
     * fan it out to the UI via broadcastSynthesizedNote.
     */
    void playQwertyNote(int note, int velocity, bool isNoteOn);

    /// Enables or disables the QWERTY device on the fork, and for the sink path.
    void setQwertyEnabled(bool enabled);

    void resetTestState();

  private:
    // MidiInputCallback implementation
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // Shared note-event fan-out for both real MIDI (handleIncomingMidiMessage)
    // and synthesized QWERTY notes (broadcastSynthesizedNote): fires onNoteEvent
    // only when the track is being monitored. Caller must hold routingLock_.
    void notifyNoteEventIfMonitored(TrackId trackId, int noteNumber, int velocity, bool isNoteOn);

    te::Engine& engine_;

    // AudioBridge reference for triggering MIDI activity (not owned)
    AudioBridge* audioBridge_ = nullptr;

    // Shared MIDI-activity monitor; not owned (#2579).
    TrackMeters* meters_ = nullptr;

    // Where live MIDI goes under the magda engine; not owned (#2579).
    std::atomic<LiveMidiSink*> liveSink_{nullptr};

    // QWERTY enable state for the sink path, where there is no fork device to ask.
    bool qwertyEnabled_ = false;

    // Track MIDI input routing (trackId → MIDI device ID)
    std::unordered_map<TrackId, juce::String> trackMidiInputs_;

    // Tracks being monitored for MIDI activity
    std::unordered_set<TrackId> monitoredTracks_;

    // Active MIDI input listeners (deviceId → MidiInput)
    std::unordered_map<juce::String, std::unique_ptr<juce::MidiInput>> activeMidiInputs_;

    // Active MIDI outputs (JUCE identifier → MidiOutput). Opened lazily by
    // sendMidi() and kept alive until MidiBridge is destroyed.
    std::unordered_map<juce::String, std::unique_ptr<juce::MidiOutput>> activeMidiOutputs_;

    // Synchronization for UI thread access
    mutable juce::CriticalSection routingLock_;

    // Whether to forward MIDI to instrument plugins
    bool forwardMidiToPlugins_ = true;

    // Global MIDI event queue for debug monitor (audio thread → UI thread)
    MidiEventQueue globalEventQueue_;

    // Recording note queue for real-time MIDI preview (not owned)
    RecordingNoteQueue* recordingQueue_ = nullptr;
    std::atomic<double>* transportPosition_ = nullptr;

    // Shutdown guard: prevents CoreMIDI callbacks from accessing destroyed state
    std::atomic<bool> isShuttingDown_{false};
    std::atomic<int> activeCallbacks_{0};

    juce::ListenerList<Listener> midiDeviceListListeners_;

    // Raw MIDI listeners (for ControllerRouter)
    juce::Array<RawMidiListener*> rawMidiListeners_;
    juce::CriticalSection rawMidiListenersLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiBridge)
};

}  // namespace magda
