#pragma once

#include <vector>

#include "../core/ChainNodePath.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/RackInfo.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

/**
 * Abstract view onto TrackManager — the track-level surface the agent
 * layer needs.
 */
class TrackApi {
  public:
    virtual ~TrackApi() = default;

    virtual TrackId createTrack(const juce::String& name, TrackType type) = 0;
    virtual TrackId groupTracks(const std::vector<TrackId>& trackIds, const juce::String& name) = 0;
    virtual void deleteTrack(TrackId trackId) = 0;
    // Move a track to a 1-based position among its siblings (top-level tracks,
    // or its parent group's children). Position is clamped to range.
    virtual void moveTrackToPosition(TrackId trackId, int oneBasedPosition) = 0;

    virtual int getNumTracks() const = 0;
    virtual const std::vector<TrackInfo>& getTracks() const = 0;
    virtual TrackInfo* getTrack(TrackId trackId) = 0;
    virtual const TrackInfo* getTrack(TrackId trackId) const = 0;

    virtual void setTrackName(TrackId trackId, const juce::String& name) = 0;
    virtual void setTrackColour(TrackId trackId, juce::Colour colour) = 0;
    virtual void setTrackVolume(TrackId trackId, float volume, bool fromAutomation) = 0;
    void setTrackVolume(TrackId trackId, float volume) {
        setTrackVolume(trackId, volume, false);
    }
    virtual void setTrackPan(TrackId trackId, float pan, bool fromAutomation) = 0;
    void setTrackPan(TrackId trackId, float pan) {
        setTrackPan(trackId, pan, false);
    }
    virtual void setTrackMuted(TrackId trackId, bool muted) = 0;
    virtual void setTrackSoloed(TrackId trackId, bool soloed) = 0;
    virtual void setTrackRecordArmed(TrackId trackId, bool armed) = 0;

    virtual DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device) = 0;
    virtual DeviceId addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                      const DeviceInfo& device) = 0;

    // ------------------------------------------------------------------
    // Racks and chains, by path (#1993)
    //
    // A `(trackId, rackId, chainId)` triple names exactly one level of
    // nesting, and the model nests arbitrarily — `Track > Rack > Chain >
    // Rack > Chain > Device`. So the triple-based surface below cannot
    // address anything inside a nested rack: there is no triple that names
    // the inner chain. A remote or agent caller could reach a *device* at
    // depth (`DevicePathDto` has carried a full route since #1991) but not
    // the chain containing it, so "add a chain to this rack" stopped working
    // at depth two.
    //
    // These are the same operations addressed by `ChainNodePath`, which is
    // the route the UI has always used. The names deliberately mirror
    // `TrackManager`'s own — this facade is a view onto it, and inventing a
    // third vocabulary for the same calls would be one more mapping to hold
    // in your head.
    //
    // The triple-based methods below are kept as shims over these, so there
    // is one implementation rather than two that can drift, and the agent
    // DSL — whose grammar is inherently one-rack-at-a-time — keeps working
    // unchanged.
    // ------------------------------------------------------------------

    virtual RackId addRackToChainByPath(const ChainNodePath& chainPath,
                                        const juce::String& name) = 0;
    virtual void removeRackFromChainByPath(const ChainNodePath& rackPath) = 0;
    virtual const RackInfo* getRackByPath(const ChainNodePath& rackPath) const = 0;
    virtual void setRackBypassedByPath(const ChainNodePath& rackPath, bool bypassed) = 0;
    virtual void setRackVolume(const ChainNodePath& rackPath, float volumeDb) = 0;

    virtual ChainId addChainToRack(const ChainNodePath& rackPath, const juce::String& name) = 0;
    virtual void removeChainByPath(const ChainNodePath& chainPath) = 0;
    virtual const ChainInfo* getChainByPath(const ChainNodePath& chainPath) const = 0;
    virtual void setChainOutput(const ChainNodePath& chainPath, int outputIndex) = 0;
    virtual void setChainMuted(const ChainNodePath& chainPath, bool muted) = 0;
    virtual void setChainBypassed(const ChainNodePath& chainPath, bool bypassed) = 0;
    virtual void setChainSolo(const ChainNodePath& chainPath, bool solo) = 0;
    virtual void setChainVolume(const ChainNodePath& chainPath, float volumeDb) = 0;
    virtual void setChainPan(const ChainNodePath& chainPath, float pan) = 0;
    virtual void setChainName(const ChainNodePath& chainPath, const juce::String& name) = 0;

    virtual DeviceId addDeviceToChainByPath(const ChainNodePath& chainPath,
                                            const DeviceInfo& device) = 0;

    // Focused top-level rack and chain management surface for command agents.
    // IDs are stable model IDs, surfaced in the command-state snapshot.
    //
    // Every one of these is a shim over the path form above, addressing depth
    // one: `(t, r, c)` is `ChainNodePath::chain(t, r, c)`.
    virtual RackId addRackToTrack(TrackId trackId, const juce::String& name) = 0;
    virtual void removeRackFromTrack(TrackId trackId, RackId rackId) = 0;
    virtual const RackInfo* getRack(TrackId trackId, RackId rackId) const = 0;
    virtual void setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) = 0;
    virtual void setRackVolume(TrackId trackId, RackId rackId, float volumeDb) = 0;
    virtual ChainId addChainToRack(TrackId trackId, RackId rackId, const juce::String& name) = 0;
    virtual void removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) = 0;
    virtual const ChainInfo* getChain(TrackId trackId, RackId rackId, ChainId chainId) const = 0;
    virtual void setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                int outputIndex) = 0;
    virtual void setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) = 0;
    virtual void setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                  bool bypassed) = 0;
    virtual void setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) = 0;
    virtual void setChainVolume(TrackId trackId, RackId rackId, ChainId chainId,
                                float volumeDb) = 0;
    virtual void setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) = 0;
    virtual void setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                              const juce::String& name) = 0;

    // First instrument plugin on the track, walking into racks. Returns nullptr
    // if the track has none. Used by the drummer executor to resolve role
    // tokens to the per-instance kit.
    virtual const DeviceInfo* getPrimaryInstrument(TrackId trackId) const = 0;
};

}  // namespace magda
