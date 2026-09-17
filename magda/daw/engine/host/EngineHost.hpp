#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../../audio/DeviceMeters.hpp"
#include "../../core/ChainNodePath.hpp"
#include "../../core/ClipTypes.hpp"
#include "../../core/HostedParameterEdit.hpp"
#include "../../core/TypeIds.hpp"

namespace juce {
class AudioDeviceManager;
class AudioPluginFormatManager;
class KnownPluginList;
class MidiMessage;
class String;
}  // namespace juce

namespace magda {
class OfflineRenderSession;
class TempoMap;
struct RecordingPreview;
struct HostParameters;
struct PluginPrograms;
struct OfflineRenderRequest;
}  // namespace magda

namespace magda::daw::audio {
class MagdaDevice;
}

/**
 * @file EngineHost.hpp
 * @brief The app's end of magda::engine: a device callback and a publisher.
 *
 * Everything the engine can do it has so far done offline, driven by a harness
 * (#2551). This is the first thing to drive it from an audio device: it owns an
 * EngineSession, renders it from the callback, and republishes the project
 * whenever the model moves.
 *
 * The engine's own types stay out of this header on purpose. MagdaAudioEngine
 * is compiled into magda_daw, which does not see magda_engine's includes; what
 * crosses that line is one pointer.
 *
 * Threading: everything but the callback runs on the message thread, which is
 * the session's one publishing thread. Model edits are coalesced through an
 * async update rather than published where they are noticed, so a drag that
 * fires a hundred property changes publishes once.
 */

namespace magda::daw::engine_host {

class EngineHost {
  public:
    EngineHost();
    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    /**
     * @brief Render into @p devices, and follow the model from here on.
     *
     * The device decides the sample rate and block size, so the session behind
     * this is built on the first callback rather than here: until then the
     * output is silence and the cursor stands still, which is what a session
     * with no plan renders anyway (EngineSession::process).
     */
    void start(juce::AudioDeviceManager& devices);

    /**
     * @brief Where external plugins are found and what can open them (#2566).
     *
     * Both are the fork's, which owns the scan; the engine has no catalog of
     * its own for a plugin that is a file on a machine rather than a class this
     * build contains. Before @ref start, or the first publish goes without
     * them.
     */
    void setPluginServices(juce::AudioPluginFormatManager& formats,
                           const juce::KnownPluginList& knownPlugins);

    /**
     * @brief Where the levels this renders go (#2570).
     *
     * On the message thread at meter rate, per track and for the master, with
     * the peak since the last call. Set before @ref start; without one nothing
     * reads the taps, which costs only the meters.
     */
    using MeterSink = std::function<void(TrackId trackId, float peakL, float peakR)>;
    void meterInto(MeterSink sink);

    /**
     * @brief Where the per-slot device levels go (#2570).
     *
     * The same rate and thread as @ref meterInto, addressed by the device's
     * path because that is what the chain UI draws a slot under. A slot with
     * nothing rendering behind it -- a bypassed device, a plugin still
     * loading -- is reported as silence rather than left holding its last
     * peak.
     *
     * The store itself rather than a sink, unlike @ref meterInto: nothing
     * translates a level on the way, and this is the side that hears the
     * project boundary the store has to be emptied at -- device ids restart
     * at 1 in the next project, so a slot that has not rendered yet would
     * otherwise read what the last project left under its address.
     *
     * @p devices outlives this host.
     */
    void meterDevicesInto(DeviceMeters& devices);

    /// Take the callback back off the device and stop following the model.
    /// Safe to call twice, and called by the destructor.
    void stop();

    /**
     * @brief Drop everything built for the project that is going (#2572).
     *
     * Track and device ids restart at 1 in the next project, so what the store
     * holds and what the meters last read belong to devices that are gone.
     * Called when ProjectManager declares the teardown, while the outgoing
     * project is still the model.
     */
    void forgetProject();

    /// How many times the model has asked this to republish. A count that
    /// stops moving under an edit is one nothing here is listening for.
    std::uint64_t publishRequests() const;

    // ===== Live MIDI (#2579) =====
    //
    // Both queue one message for the next callback, from the message thread or
    // from a MIDI callback thread. Never from the audio thread.

    /// A note played at @p trackId itself: the piano roll, the chord panel and
    /// the drum pads, which preview whatever the track is monitoring.
    void audition(TrackId trackId, const juce::MidiMessage& message);

    /// A message from @p deviceId, for whichever tracks are routed to it.
    void pushMidi(const juce::String& deviceId, const juce::MidiMessage& message);

    /// Name a device the system's MIDI list does not hold -- the QWERTY
    /// keyboard -- so a track routed to "all" is bound to it at the next
    /// publish and stays bound to it. Message thread.
    void registerVirtualMidiSource(const juce::String& deviceId);

    // ===== Plugin state =====
    //
    // All three run on the message thread and return once the plugin has been
    // read or written, because a save writes the file immediately after.

    /** @brief Read every external plugin this renders back into the model. */
    void captureExternalPluginStates();

    /**
     * @brief Read the plugin at @p devicePath, plus any on its pads.
     *
     * Callers use this before copying, removing or snapshotting one slot.
     * Pass a Drum Grid's own path to reach its pads: pad plugins have no
     * path of their own (#2207).
     */
    void captureExternalPluginStateAt(const ChainNodePath& devicePath);

    /**
     * @brief Write the model's state for @p devicePath into the plugin (#2573).
     *
     * The model is then updated from the plugin, because a state chunk can
     * change parameters the model's array does not list.
     */
    void applyExternalPluginStateAt(const ChainNodePath& devicePath);

    /**
     * @brief The plugin's own text for a parameter value (#2600).
     *
     * @p paramIndex is the plan slot, which is what ParameterInfo carries.
     * @p normalised is the position the plugin takes. Empty for a device this
     * is not rendering, and for a parameter it does not have -- which is the
     * caller's signal to format from the range instead.
     *
     * Message thread, and synchronous: a value's text is read inside a paint.
     * See EngineExternalDevice::parameterText() for why this one operation
     * does not go through the control plane.
     */
    juce::String formatDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                       float normalised) const;

    /** @brief Every parameter the plugin at @p devicePath reports (#2629). */
    HostParameters describeDeviceParameters(const ChainNodePath& devicePath) const;

    /**
     * @brief The MAGDA device the plan renders at @p devicePath, or null (#2585).
     *
     * What a faceplate reads its telemetry off. Held open by the store's own
     * lease, so a UI reading a ring is still holding the instance a rebuild
     * has taken out of the plan.
     *
     * Message thread, and synchronous: a ring is read inside a repaint. It is
     * a query -- nothing is suspended and nothing moves -- which is why it does
     * not go through the control plane, the same exemption formatDeviceParameter
     * has.
     */
    std::shared_ptr<audio::MagdaDevice> renderedDevice(const ChainNodePath& devicePath) const;

    /**
     * @brief What the plugin last reported for @p paramIndex, if anything.
     *
     * The runtime value of a parameter the document holds nothing for, which
     * is every ordinary parameter of a hosted plugin.
     */
    std::optional<float> observedParameter(const ChainNodePath& devicePath, int paramIndex) const;

    /// Whether an edit to this parameter was accepted and has not completed.
    bool hostedEditPending(const ChainNodePath& devicePath, int paramIndex) const;

    /**
     * @brief Deliver a one-off @p normalised position to a hosted parameter.
     *
     * The ordinary edit of a parameter the plugin owns
     * (docs/specs/hosted-plugin-parameter-control.md): it changes no document
     * state, adds no table entry and rebuilds no plan. Accepted from the
     * message thread and delivered on the control executor, so a device with
     * no render op takes it like any other.
     */
    EditReceipt editHostedParameter(const ChainNodePath& devicePath, int paramIndex,
                                    float normalised, EditOrigin origin,
                                    std::function<void(EditCompletion)> completed = {});

    // ===== The plugins' own windows (#2580) =====
    //
    // Message thread, each answering what the window is afterwards, which is
    // what the slot draws. False for a device this is not rendering.

    std::optional<PluginPrograms> getPluginPrograms(const ChainNodePath& devicePath);
    bool setPluginCurrentProgram(const ChainNodePath& devicePath, int programIndex);
    bool loadPluginPresetFile(const ChainNodePath& devicePath, const juce::File& file);
    bool savePluginPresetFile(const ChainNodePath& devicePath, const juce::File& file);

    bool showDeviceEditor(const ChainNodePath& devicePath);
    bool hideDeviceEditor(const ChainNodePath& devicePath);
    bool toggleDeviceEditor(const ChainNodePath& devicePath);
    bool isDeviceEditorOpen(const ChainNodePath& devicePath);

    // ===== Transport =====
    //
    // Play, stop and locate are a request the clock applies once, so each of
    // these publishes a transport rather than reaching into the audio thread.

    void play();
    void stopPlaying();
    void locateSeconds(double seconds);
    bool isPlaying() const;

    /**
     * @brief Begin an Arrangement MIDI take on every eligible armed track.
     *
     * Locates first when playback is stopped, then starts the transport. While
     * rolling, begins at the current cursor. False means no eligible input was
     * available and nothing changed.
     */
    bool startMidiRecording(double positionSeconds);

    /// Finish every Arrangement MIDI take without stopping playback.
    void stopMidiRecording();

    /// Whether this host currently owns any live Arrangement MIDI take.
    bool isRecording() const;

    void armSessionSlotRecording(TrackId trackId, int sceneIndex);
    void beginArmedSessionSlotRecordings();
    void beginArmedSessionSlotRecordings(double positionSeconds);
    bool isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const;
    bool isSessionSlotRecording(TrackId trackId, int sceneIndex) const;

    /** @brief Arrangement MIDI passes as the record taps last published them. */
    const std::unordered_map<TrackId, RecordingPreview>& recordingPreviews();

    /// Where the cursor is. Readable from any thread; what a playhead is drawn
    /// from.
    double positionBeats() const;
    double positionSeconds() const;

    // ===== What the transport is published with =====

    void setTempo(double bpm);
    double tempo() const;
    void setTimeSignature(int numerator, int denominator);
    void getTimeSignature(int& numerator, int& denominator) const;
    void setLoop(bool enabled, double startBeat, double endBeat);
    void setMetronomeEnabled(bool enabled);
    bool isMetronomeEnabled() const;

    /** @brief The loop, as the one value the transport is published with. */
    struct LoopState {
        bool enabled = false;
        double startBeat = 0.0;
        double endBeat = 0.0;
    };
    LoopState loop() const;

    /**
     * @brief An offline render over this host's devices (#2555).
     *
     * The live callback is off and publishes are held until the session is
     * destroyed. Message thread.
     */
    std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(
        bool resumePlaybackWhenFinished);

    /** @brief The render that freezes a track, or why there is none. */
    struct FreezePlan {
        std::shared_ptr<OfflineRenderRequest> request;
        juce::String refusal;
    };

    /** @brief What freezing @p trackId renders, at the device's rate (#2555). Message thread. */
    FreezePlan planFreeze(TrackId trackId) const;

    /// Take @p request's finished file as what its track plays once frozen.
    void adoptFreeze(const OfflineRenderRequest& request);

    /// The tempo and signature above as the app's beats<->seconds facade.
    /// Never null, and valid until this host is destroyed.
    const magda::TempoMap* tempoMap() const;

    // ===== The session launcher (#2552) =====
    //
    // A launch is a request queued against the slot's handle at the beat it is
    // due on, and what a slot is doing is read back off the tap the block that
    // advanced it wrote. SlotLauncher.hpp is the whole of it; these forward.

    /** @brief Launch @p clipId's slot, at the clip's own quantization. */
    void launchClip(ClipId clipId);

    /** @brief Stop @p clipId's slot and give its track back to the arrangement. */
    void stopClip(ClipId clipId);

    /** @brief Scene @p sceneIndex across @p trackIds, launched on one beat. */
    void launchScene(const std::vector<TrackId>& trackIds, int sceneIndex);

    /** @brief Stop what @p trackId is playing, at the stopping clip's quantization. */
    void stopSessionTrack(TrackId trackId);

    /** @brief Stop every slot and return every track to its arrangement. */
    void stopAllSessionClips();

    /** @brief What @p clipId's slot is doing, as the last block left it. */
    SessionClipPlayState sessionClipPlayState(ClipId clipId) const;

    /** @brief Whether a quantized stop is in flight on @p trackId. */
    bool sessionTrackStopPending(TrackId trackId) const;

    /** @brief Where the session playhead is, in seconds, or -1.0. */
    double sessionPlayheadSeconds() const;

    /** @brief The clip the session playhead follows, or INVALID_CLIP_ID. */
    ClipId sessionPlayheadClip() const;

    /** @brief Every sounding slot's playhead, in seconds. */
    std::unordered_map<ClipId, double> sessionPlayheads() const;

    /** @brief Turn what the taps say into the model's own state, once a frame. */
    void processSessionStateEvents();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::daw::engine_host
