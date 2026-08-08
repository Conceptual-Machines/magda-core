#include "track_api_live.hpp"

#include "../core/TrackManager.hpp"

namespace magda {

TrackId TrackApiLive::createTrack(const juce::String& name, TrackType type) {
    return TrackManager::getInstance().createTrack(name, type);
}

TrackId TrackApiLive::groupTracks(const std::vector<TrackId>& trackIds, const juce::String& name) {
    return TrackManager::getInstance().groupTracks(trackIds, name);
}

void TrackApiLive::deleteTrack(TrackId trackId) {
    TrackManager::getInstance().deleteTrack(trackId);
}

void TrackApiLive::moveTrackToPosition(TrackId trackId, int oneBasedPosition) {
    TrackManager::getInstance().moveTrackToPosition(trackId, oneBasedPosition);
}

int TrackApiLive::getNumTracks() const {
    return TrackManager::getInstance().getNumTracks();
}

const std::vector<TrackInfo>& TrackApiLive::getTracks() const {
    return TrackManager::getInstance().getTracks();
}

TrackInfo* TrackApiLive::getTrack(TrackId trackId) {
    return TrackManager::getInstance().getTrack(trackId);
}

const TrackInfo* TrackApiLive::getTrack(TrackId trackId) const {
    return TrackManager::getInstance().getTrack(trackId);
}

void TrackApiLive::setTrackName(TrackId trackId, const juce::String& name) {
    TrackManager::getInstance().setTrackName(trackId, name);
}

void TrackApiLive::setTrackColour(TrackId trackId, juce::Colour colour) {
    TrackManager::getInstance().setTrackColour(trackId, colour);
}

void TrackApiLive::setTrackVolume(TrackId trackId, float volume, bool fromAutomation) {
    TrackManager::getInstance().setTrackVolume(trackId, volume, fromAutomation);
}

void TrackApiLive::setTrackPan(TrackId trackId, float pan, bool fromAutomation) {
    TrackManager::getInstance().setTrackPan(trackId, pan, fromAutomation);
}

void TrackApiLive::setTrackMuted(TrackId trackId, bool muted) {
    TrackManager::getInstance().setTrackMuted(trackId, muted);
}

void TrackApiLive::setTrackRecordArmed(TrackId trackId, bool armed) {
    TrackManager::getInstance().setTrackRecordArmed(trackId, armed);
}

void TrackApiLive::setTrackSoloed(TrackId trackId, bool soloed) {
    TrackManager::getInstance().setTrackSoloed(trackId, soloed);
}

DeviceId TrackApiLive::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    return TrackManager::getInstance().addDeviceToTrack(trackId, device);
}

DeviceId TrackApiLive::addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                        const DeviceInfo& device) {
    return addDeviceToChainByPath(ChainNodePath::chain(trackId, rackId, chainId), device);
}

// ---------------------------------------------------------------------------
// Path-addressed racks and chains (#1993)
//
// Straight forwards to the `TrackManager` methods the UI already uses. Nothing
// is implemented here that was not reachable before — the model always
// supported arbitrary nesting; it was only the facade that could not name it.
// ---------------------------------------------------------------------------

RackId TrackApiLive::addRackToChainByPath(const ChainNodePath& chainPath,
                                          const juce::String& name) {
    return TrackManager::getInstance().addRackToChainByPath(chainPath, name);
}

void TrackApiLive::removeRackFromChainByPath(const ChainNodePath& rackPath) {
    TrackManager::getInstance().removeRackFromChainByPath(rackPath);
}

const RackInfo* TrackApiLive::getRackByPath(const ChainNodePath& rackPath) const {
    return TrackManager::getInstance().getRackByPath(rackPath);
}

void TrackApiLive::setRackBypassedByPath(const ChainNodePath& rackPath, bool bypassed) {
    TrackManager::getInstance().setRackBypassedByPath(rackPath, bypassed);
}

void TrackApiLive::setRackVolume(const ChainNodePath& rackPath, float volumeDb) {
    TrackManager::getInstance().setRackVolume(rackPath, volumeDb);
}

ChainId TrackApiLive::addChainToRack(const ChainNodePath& rackPath, const juce::String& name) {
    return TrackManager::getInstance().addChainToRack(rackPath, name);
}

void TrackApiLive::removeChainByPath(const ChainNodePath& chainPath) {
    TrackManager::getInstance().removeChainByPath(chainPath);
}

const ChainInfo* TrackApiLive::getChainByPath(const ChainNodePath& chainPath) const {
    return TrackManager::getInstance().getChainByPath(chainPath);
}

void TrackApiLive::setChainOutput(const ChainNodePath& chainPath, int outputIndex) {
    TrackManager::getInstance().setChainOutput(chainPath, outputIndex);
}

void TrackApiLive::setChainMuted(const ChainNodePath& chainPath, bool muted) {
    TrackManager::getInstance().setChainMuted(chainPath, muted);
}

void TrackApiLive::setChainBypassed(const ChainNodePath& chainPath, bool bypassed) {
    TrackManager::getInstance().setChainBypassed(chainPath, bypassed);
}

void TrackApiLive::setChainSolo(const ChainNodePath& chainPath, bool solo) {
    TrackManager::getInstance().setChainSolo(chainPath, solo);
}

void TrackApiLive::setChainVolume(const ChainNodePath& chainPath, float volumeDb) {
    TrackManager::getInstance().setChainVolume(chainPath, volumeDb);
}

void TrackApiLive::setChainPan(const ChainNodePath& chainPath, float pan) {
    TrackManager::getInstance().setChainPan(chainPath, pan);
}

void TrackApiLive::setChainName(const ChainNodePath& chainPath, const juce::String& name) {
    TrackManager::getInstance().setChainName(chainPath, name);
}

DeviceId TrackApiLive::addDeviceToChainByPath(const ChainNodePath& chainPath,
                                              const DeviceInfo& device) {
    return TrackManager::getInstance().addDeviceToChainByPath(chainPath, device);
}

// ---------------------------------------------------------------------------
// Triple-addressed surface — shims over the path form, at depth one
// ---------------------------------------------------------------------------

RackId TrackApiLive::addRackToTrack(TrackId trackId, const juce::String& name) {
    // Not a shim: a *top-level* rack lives in the track's own FX chain rather
    // than inside another rack's chain, so there is no chain path to add it to.
    // `addRackToChainByPath` is how you nest one; this is how you start.
    return TrackManager::getInstance().addRackToTrack(trackId, name);
}

void TrackApiLive::removeRackFromTrack(TrackId trackId, RackId rackId) {
    removeRackFromChainByPath(ChainNodePath::rack(trackId, rackId));
}

const RackInfo* TrackApiLive::getRack(TrackId trackId, RackId rackId) const {
    return getRackByPath(ChainNodePath::rack(trackId, rackId));
}

void TrackApiLive::setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) {
    setRackBypassedByPath(ChainNodePath::rack(trackId, rackId), bypassed);
}

void TrackApiLive::setRackVolume(TrackId trackId, RackId rackId, float volumeDb) {
    setRackVolume(ChainNodePath::rack(trackId, rackId), volumeDb);
}

ChainId TrackApiLive::addChainToRack(TrackId trackId, RackId rackId, const juce::String& name) {
    return addChainToRack(ChainNodePath::rack(trackId, rackId), name);
}

void TrackApiLive::removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) {
    removeChainByPath(ChainNodePath::chain(trackId, rackId, chainId));
}

const ChainInfo* TrackApiLive::getChain(TrackId trackId, RackId rackId, ChainId chainId) const {
    return getChainByPath(ChainNodePath::chain(trackId, rackId, chainId));
}

void TrackApiLive::setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                  int outputIndex) {
    setChainOutput(ChainNodePath::chain(trackId, rackId, chainId), outputIndex);
}

void TrackApiLive::setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) {
    setChainMuted(ChainNodePath::chain(trackId, rackId, chainId), muted);
}

void TrackApiLive::setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                    bool bypassed) {
    setChainBypassed(ChainNodePath::chain(trackId, rackId, chainId), bypassed);
}

void TrackApiLive::setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) {
    setChainSolo(ChainNodePath::chain(trackId, rackId, chainId), solo);
}

void TrackApiLive::setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volumeDb) {
    setChainVolume(ChainNodePath::chain(trackId, rackId, chainId), volumeDb);
}

void TrackApiLive::setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) {
    setChainPan(ChainNodePath::chain(trackId, rackId, chainId), pan);
}

void TrackApiLive::setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                                const juce::String& name) {
    setChainName(ChainNodePath::chain(trackId, rackId, chainId), name);
}

const DeviceInfo* TrackApiLive::getPrimaryInstrument(TrackId trackId) const {
    return TrackManager::getInstance().getPrimaryInstrument(trackId);
}

}  // namespace magda
