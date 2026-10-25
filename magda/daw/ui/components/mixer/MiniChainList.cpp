#include "MiniChainList.hpp"

namespace magda {

std::vector<MiniChainList::SignatureEntry> MiniChainList::buildSignature(const TrackInfo& track) {
    std::vector<SignatureEntry> signature;
    signature.reserve(track.chain.fxChainElements.size());
    for (const auto& element : track.chain.fxChainElements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            signature.push_back({false, device.id, device.name, device.miniMixerParameters});
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            signature.push_back({true, rack.id, rack.name, {}});
        }
    }
    return signature;
}

void MiniChainList::sync(const TrackInfo& track, AudioEngine* engine) {
    auto signature = buildSignature(track);
    if (track.id == trackId_ && signature == signature_)
        return;

    signature_ = std::move(signature);
    trackId_ = track.id;
    rebuild(track, engine);
}

void MiniChainList::rebuild(const TrackInfo& track, AudioEngine* engine) {
    rows_.clear();
    for (const auto& element : track.chain.fxChainElements) {
        auto row = std::make_unique<MiniChainRow>();
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            row->setDevice(ChainNodePath::topLevelDevice(track.id, device.id), engine, device.name,
                           device.bypassed);
            row->onExpandChanged = [this]() {
                resized();
                if (onHeightChanged)
                    onHeightChanged();
            };
        } else if (isRack(element)) {
            row->setDevice({}, engine, getRack(element).name, false);
        } else {
            continue;
        }
        addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
    }
    resized();
}

void MiniChainList::syncBypass(DeviceId deviceId, bool bypassed) {
    if (deviceId == INVALID_DEVICE_ID)
        return;
    for (auto& row : rows_) {
        if (row->deviceId() == deviceId) {
            row->setBypassedState(bypassed);
            return;
        }
    }
}

int MiniChainList::preferredHeight() const {
    int height = 0;
    for (const auto& row : rows_)
        height += row->preferredHeight() + kRowGap;
    return height;
}

void MiniChainList::resized() {
    auto bounds = getLocalBounds();
    for (auto& row : rows_) {
        row->setBounds(bounds.removeFromTop(row->preferredHeight()));
        bounds.removeFromTop(kRowGap);
    }
}

}  // namespace magda
