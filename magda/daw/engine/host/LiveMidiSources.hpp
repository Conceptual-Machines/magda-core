#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

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
 * two id spaces that must not overlap cannot.
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
    juce::CriticalSection lock_;
    juce::Array<juce::MidiDeviceInfo> available_;
    std::map<juce::String, int> devices_;
    std::set<int> virtual_;
    std::map<TrackId, int> auditions_;
    int next_ = 1;
};

}  // namespace magda::daw::engine_host
