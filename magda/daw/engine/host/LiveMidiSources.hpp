#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "core/TypeIds.hpp"

/**
 * @file LiveMidiSources.hpp
 * @brief Which live MIDI source id a device or a track's audition is (#2579).
 *
 * The engine knows an opaque LiveMidiSourceId and nothing else
 * (io/LiveInput.hpp); turning a device identifier or a piano-roll preview into
 * one is the host's.
 *
 * An id is the model's for as long as the model names it. A track's audition
 * id goes back when the track does, because one is taken for every track that
 * reads MIDI rather than for every track anyone previewed (#2579), and a
 * counter that only ever climbs turns a session's worth of editing into
 * silence the callback cannot explain (#2590). What makes reuse safe is
 * @ref observeDrains: an id the model has let go waits for the callback to
 * consume what was queued under it before anything else can have it.
 *
 * Message thread and the MIDI callback thread, under one lock. The audio
 * thread never reads this: what it needs is resolved into each source at
 * publish time.
 */

namespace magda::daw::engine_host {

class LiveMidiSources {
  public:
    /// What an unresolvable route answers, and never a real source.
    static constexpr int kNoSource = -1;

    /// Ids for every MIDI input this machine has now, so an "all" route
    /// resolves to them before any of them has played a note.
    void registerAvailableDevices();

    /// The same against a list the caller already holds, which is the
    /// snapshot every route then resolves against. A device that has left it
    /// leaves every "all" route too, which is what raises the panic for a note
    /// it was holding.
    void registerAvailableDevices(juce::Array<juce::MidiDeviceInfo> available);

    /// The id @p deviceId pushes under, assigned on first use.
    int sourceFor(const juce::String& deviceId);

    /// The id for a device the system's list will never hold -- the QWERTY
    /// keyboard is one -- which stays in every "all" route for as long as this
    /// lives, since no device list can say it is still there.
    int registerVirtualDevice(const juce::String& deviceId);

    /// The id a track's own preview arrives under. Disjoint from every
    /// device's, so an audition on one track is not heard on another.
    int auditionSourceFor(TrackId trackId);

    /**
     * @brief Give back the audition ids of every track but @p live.
     *
     * From the publishing thread, against the tracks a routing snapshot was
     * just resolved from: that walk is where the model's whole track list is
     * already in hand. A released id is not handed out again until the
     * callback has drained past it (@ref observeDrains).
     */
    void retainAuditions(const std::set<TrackId>& live);

    /**
     * @brief Watch @p drains, which the callback counts as it consumes.
     *
     * A released id is queued behind the value this held when it went, so an
     * event already queued under it is consumed before anything else can be
     * that id. Until a counter is observed nothing is reused, which is what a
     * host with no callback of its own gets.
     */
    void observeDrains(const std::atomic<std::uint64_t>& drains) {
        drains_ = &drains;
    }

    /// Ids the model has let go and the callback has not yet drained past.
    /// For the trace and the tests; message thread.
    std::size_t waitingToBeReused() const;

    /// What an "all" route resolves to: the devices the last snapshot held,
    /// plus the virtual ones. Never an audition id, and never a device that
    /// has been unplugged since.
    std::vector<int> deviceSources() const;

    /**
     * @brief The id a track's `midiInputDevice` names, or @ref kNoSource.
     *
     * By JUCE identifier, by the fork's hashed ID for it, then by device name,
     * the way MidiInputRouter reads the same field. A field naming nothing
     * this machine has is taken as the id it will push under, which is how the
     * QWERTY keyboard resolves: it pushes under the fork's ID for it.
     */
    int resolveRoute(const juce::String& midiInputDevice);

  private:
    /// An id nobody names any more, and the drain count it was released at.
    struct Released {
        int source = kNoSource;
        std::uint64_t drains = 0;
    };

    /// The next id to hand out: one the callback has drained past, or a fresh
    /// one. Call with @ref lock_ held.
    int takeSource();

    juce::CriticalSection lock_;
    juce::Array<juce::MidiDeviceInfo> available_;
    std::map<juce::String, int> devices_;
    std::set<int> virtual_;
    std::map<TrackId, int> auditions_;
    std::vector<Released> released_;
    const std::atomic<std::uint64_t>* drains_ = nullptr;
    int next_ = 1;
};

}  // namespace magda::daw::engine_host
