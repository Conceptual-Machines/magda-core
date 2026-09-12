#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
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
 * one is the host's. Ids come from a single counter and are never reused, so
 * two id spaces that must not overlap cannot, and an id someone resolved before
 * a track was deleted still means that track and nothing else.
 *
 * What is bounded is the room the callback has, and that is a different thing:
 * an id is bound to one of @ref kSlots buffers for as long as the model names
 * it, and the slot goes back when the id does (#2590). One is taken for every
 * track that reads MIDI rather than for every track anyone previewed, so
 * without that a session of building and deleting tracks runs the room out and
 * the callback drops what it cannot place.
 *
 * A slot outlives the queue, so an event pushed under an id whose slot has
 * since been taken by another would land in the wrong track. @ref ownerOfSlot
 * is what the callback checks to refuse it: the id and the slot have to agree.
 *
 * Message thread and the MIDI callback thread, under one lock. The audio thread
 * calls @ref ownerOfSlot and nothing else, which reads an atomic and takes no
 * lock.
 */

namespace magda::daw::engine_host {

class LiveMidiSources {
  public:
    LiveMidiSources();

    /// What an unresolvable route answers, and never a real source.
    static constexpr int kNoSource = -1;

    /// Buffers the callback has room for: one per enabled input, one per
    /// virtual device, one per track that reads MIDI. A project size rather
    /// than a session's history, because a slot goes back when its track does.
    static constexpr int kSlots = 256;

    /// What an id past @ref kSlots is bound to, and what a retired one leaves.
    static constexpr int kNoSlot = -1;

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
     * @brief Forget the audition of every track but @p live, slot and all.
     *
     * From the publishing thread, against the tracks a routing snapshot was
     * just resolved from: that walk is where the model's whole track list is
     * already in hand. The id is not handed out again -- no id ever is -- so
     * an event still queued under it is refused by @ref ownerOfSlot rather
     * than delivered to whoever took the slot.
     */
    void retainAuditions(const std::set<TrackId>& live);

    /// The buffer @p source is bound to, or @ref kNoSlot for an id the room
    /// had none left for. What a producer pushes beside the id.
    int slotFor(int source) const;

    /// The id @p slot belongs to now, or @ref kNoSource. The one thing the
    /// audio thread asks, and the one that takes no lock.
    int ownerOfSlot(int slot) const;

    /// Slots nothing holds. For the trace and the tests; message thread.
    std::size_t freeSlots() const;

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
    /// A fresh id, bound to a slot while there is one. Call with @ref lock_
    /// held.
    int take();

    /// Take a free slot for @p source. Call with @ref lock_ held and a slot
    /// free.
    void bind(int source);

    /// Give @p source's slot back. Call with @ref lock_ held.
    void release(int source);

    /// Bind the ids the room had no slot for when they were taken, oldest
    /// first. Room comes back when a track goes, and an id that waited through
    /// that is one the model still names. Call with @ref lock_ held.
    void bindWaiting();

    juce::CriticalSection lock_;
    juce::Array<juce::MidiDeviceInfo> available_;
    std::map<juce::String, int> devices_;
    std::set<int> virtual_;
    std::map<TrackId, int> auditions_;

    /// Which id owns each slot, written under the lock and read by the audio
    /// thread without one.
    std::array<std::atomic<int>, kSlots> slotOwner_;

    std::map<int, int> slotForSource_;
    std::vector<int> freeSlots_;
    int next_ = 1;
};

}  // namespace magda::daw::engine_host
