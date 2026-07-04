#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "../../core/TypeIds.hpp"
#include "RecordingNoteQueue.hpp"

namespace magda {

namespace te = tracktion;

class TrackController;

class MidiInputRouter : private juce::AsyncUpdater {
  public:
    MidiInputRouter(te::Engine& engine, te::Edit& edit, TrackController& trackController);
    ~MidiInputRouter() override;

    te::VirtualMidiInputDevice* getQwertyMidiDevice();

    /// Wires the destination for track-routed MIDI recording-preview events.
    /// Track-sourced MIDI ("track:N" inputs) never passes through MidiBridge's
    /// juce::MidiInputCallback layer, so the router feeds the live recording
    /// preview itself via te::InputDeviceInstance::Consumer callbacks. This
    /// queue must be distinct from MidiBridge's (RecordingNoteQueue is
    /// single-producer and the consumer pushes from the audio thread).
    void setRecordingQueue(RecordingNoteQueue* queue, std::atomic<double>* transportPosition);

    void enableAllMidiInputDevices();
    void setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId);
    juce::String getTrackMidiInput(TrackId trackId) const;
    bool setSessionSlotMidiRecordingTarget(TrackId trackId, int sceneIndex, bool enabled);

    void setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName);
    void clearSurfaceOnlyMidiInputPorts();

    void updateMidiInputRouting();

    /// Coalesced entry point for input-monitor changes. A real monitor-mode
    /// change on a te::InputDevice triggers TransportControl::
    /// restartAllTransports(), so bursts of monitor edits (e.g. rapid clicks
    /// draining in one message-loop pass) must collapse to a single
    /// application, and it must never run re-entrantly from inside a
    /// TrackManager notification or a graph-reallocation callback.
    void requestInputMonitorResync();

    /// Applies MAGDA per-track Monitor state to the TE input devices now.
    /// Prefer requestInputMonitorResync() unless synchronous application is
    /// genuinely required.
    void resyncAllInputMonitors();

    void onMidiDevicesAvailable();
    void applyPendingRoutes();
    void handlePlaybackContextTick();

  private:
    /// Pushes note on/off events from a source track's MIDI input device into
    /// the recording-preview queue, one event per armed destination track.
    ///
    /// handleIncomingMidiMessage fires on the audio thread (TrackMidiInputDeviceNode
    /// -> MidiInputDevice::sendMessageToInstances -> consumers), so it only reads
    /// atomics and pushes to the lock-free queue — no allocation, no locks. The
    /// target list is published from the message thread by
    /// syncTrackMidiPreviewConsumers().
    class TrackMidiPreviewConsumer : public te::InputDeviceInstance::Consumer {
      public:
        void configure(RecordingNoteQueue* queue, std::atomic<double>* transportPosition) {
            queue_.store(queue, std::memory_order_release);
            transportPosition_.store(transportPosition, std::memory_order_release);
        }

        /// Message thread. Publishes the armed destination tracks (capped at
        /// kMaxTargets).
        void setArmedTargets(const std::vector<TrackId>& targets) {
            const int count = std::min(static_cast<int>(targets.size()), kMaxTargets);
            for (int i = 0; i < count; ++i)
                targets_[static_cast<size_t>(i)].store(
                    static_cast<int>(targets[static_cast<size_t>(i)]), std::memory_order_relaxed);
            numTargets_.store(count, std::memory_order_release);
        }

        // Audio thread.
        void handleIncomingMidiMessage(const juce::MidiMessage& m, te::MPESourceID) override {
            if (!m.isNoteOn() && !m.isNoteOff())
                return;

            auto* queue = queue_.load(std::memory_order_acquire);
            auto* transportPosition = transportPosition_.load(std::memory_order_acquire);
            if (queue == nullptr || transportPosition == nullptr)
                return;

            const int count = numTargets_.load(std::memory_order_acquire);

            RecordingNoteEvent evt;
            evt.noteNumber = m.getNoteNumber();
            evt.velocity = m.getVelocity();
            evt.isNoteOn = m.isNoteOn();
            evt.transportSeconds = transportPosition->load(std::memory_order_relaxed);

            for (int i = 0; i < count; ++i) {
                evt.trackId = targets_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
                queue->push(evt);
            }
        }

      private:
        static constexpr int kMaxTargets = 16;

        std::atomic<RecordingNoteQueue*> queue_{nullptr};
        std::atomic<std::atomic<double>*> transportPosition_{nullptr};
        std::array<std::atomic<int>, kMaxTargets> targets_{};
        std::atomic<int> numTargets_{0};
    };

    /// Reconciles preview consumers with the current playback context: every
    /// track-MIDI input instance with armed destination targets gets a consumer
    /// (idempotent re-add, so it survives instance recreation on
    /// TransportControl::restartAllTransports / context rebuilds), instances
    /// without armed targets get theirs removed. Message thread only.
    void syncTrackMidiPreviewConsumers();

    /// Maps an input-destination EditItemID (an AudioTrack or one of its clip
    /// slots) back to the owning MAGDA track id.
    TrackId resolveTargetTrackId(te::EditItemID targetID) const;

    bool isSurfaceOnlyMidiInput(const juce::String& liveIdentifier,
                                const juce::String& liveName) const;
    void removeSurfaceOnlyMidiInputTargets();

    void handleAsyncUpdate() override;

    te::Engine& engine_;
    te::Edit& edit_;
    TrackController& trackController_;

    std::shared_ptr<te::MidiInputDevice> qwertyMidiDevice_;
    bool qwertyNeedsContextRefresh_ = false;

    juce::StringArray surfaceOnlyMidiInputPorts_;
    mutable juce::CriticalSection surfaceOnlyMidiInputLock_;

    std::vector<std::pair<TrackId, juce::String>> pendingMidiRoutes_;
    te::EditPlaybackContext* lastPlaybackContext_ = nullptr;

    // Recording-preview plumbing for track-routed MIDI. Consumers are keyed by
    // SOURCE track id (stable across playback-context/instance recreation) and
    // live until the router is destroyed — never deleted while a live instance
    // might still reference them.
    RecordingNoteQueue* recordingQueue_ = nullptr;
    std::atomic<double>* transportPositionForMidi_ = nullptr;
    std::map<TrackId, std::unique_ptr<TrackMidiPreviewConsumer>> trackMidiPreviewConsumers_;
};

}  // namespace magda
