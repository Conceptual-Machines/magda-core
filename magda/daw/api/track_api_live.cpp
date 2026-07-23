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

void TrackApiLive::setTrackSoloed(TrackId trackId, bool soloed) {
    TrackManager::getInstance().setTrackSoloed(trackId, soloed);
}

DeviceId TrackApiLive::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    return TrackManager::getInstance().addDeviceToTrack(trackId, device);
}

RackId TrackApiLive::addRackToTrack(TrackId trackId, const juce::String& name) {
    return TrackManager::getInstance().addRackToTrack(trackId, name);
}

void TrackApiLive::removeRackFromTrack(TrackId trackId, RackId rackId) {
    TrackManager::getInstance().removeRackFromTrack(trackId, rackId);
}

const RackInfo* TrackApiLive::getRack(TrackId trackId, RackId rackId) const {
    return TrackManager::getInstance().getRack(trackId, rackId);
}

void TrackApiLive::setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) {
    TrackManager::getInstance().setRackBypassed(trackId, rackId, bypassed);
}

void TrackApiLive::setRackVolume(TrackId trackId, RackId rackId, float volumeDb) {
    TrackManager::getInstance().setRackVolume(trackId, rackId, volumeDb);
}

ChainId TrackApiLive::addChainToRack(TrackId trackId, RackId rackId, const juce::String& name) {
    return TrackManager::getInstance().addChainToRack(ChainNodePath::rack(trackId, rackId), name);
}

void TrackApiLive::removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) {
    TrackManager::getInstance().removeChainFromRack(trackId, rackId, chainId);
}

const ChainInfo* TrackApiLive::getChain(TrackId trackId, RackId rackId, ChainId chainId) const {
    return TrackManager::getInstance().getChain(trackId, rackId, chainId);
}

void TrackApiLive::setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                  int outputIndex) {
    TrackManager::getInstance().setChainOutput(trackId, rackId, chainId, outputIndex);
}

void TrackApiLive::setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) {
    TrackManager::getInstance().setChainMuted(trackId, rackId, chainId, muted);
}

void TrackApiLive::setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                    bool bypassed) {
    TrackManager::getInstance().setChainBypassed(trackId, rackId, chainId, bypassed);
}

void TrackApiLive::setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) {
    TrackManager::getInstance().setChainSolo(trackId, rackId, chainId, solo);
}

void TrackApiLive::setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volumeDb) {
    TrackManager::getInstance().setChainVolume(trackId, rackId, chainId, volumeDb);
}

void TrackApiLive::setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) {
    TrackManager::getInstance().setChainPan(trackId, rackId, chainId, pan);
}

void TrackApiLive::setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                                const juce::String& name) {
    TrackManager::getInstance().setChainName(trackId, rackId, chainId, name);
}

const DeviceInfo* TrackApiLive::getPrimaryInstrument(TrackId trackId) const {
    return TrackManager::getInstance().getPrimaryInstrument(trackId);
}

AudioEngine* TrackApiLive::getAudioEngine() const {
    return TrackManager::getInstance().getAudioEngine();
}

}  // namespace magda
