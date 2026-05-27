#include "GainStagingManager.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "../audio/AudioBridge.hpp"
#include "../audio/DeviceMeteringManager.hpp"
#include "../engine/AudioEngine.hpp"
#include "DeviceInfo.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"
#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

namespace {

float linearToDb(float linear) {
    return linear > 1.0e-6f ? 20.0f * std::log10(linear) : kGainStageSilenceDb;
}

void collectChainDeviceIds(const std::vector<ChainElement>& elements, std::vector<DeviceId>& out) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            out.push_back(getDevice(element).id);
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            for (const auto& chain : rack.chains)
                collectChainDeviceIds(chain.elements, out);
        }
    }
}

// One device's output-volume move, captured so the whole pass is a single
// undo step.
struct GainStageMove {
    ChainNodePath path;
    DeviceId deviceId = INVALID_DEVICE_ID;
    float oldGainDb = 0.0f;
    float newGainDb = 0.0f;
};

class GainStageCommand : public UndoableCommand {
  public:
    explicit GainStageCommand(std::vector<GainStageMove> moves) : moves_(std::move(moves)) {}

    void execute() override {
        auto& tm = TrackManager::getInstance();
        for (const auto& move : moves_) {
            tm.setDeviceGainDb(move.path, move.newGainDb);
            GainStagingManager::getInstance().markApplied(move.deviceId,
                                                          move.newGainDb - move.oldGainDb);
        }
    }

    void undo() override {
        auto& tm = TrackManager::getInstance();
        for (const auto& move : moves_) {
            tm.setDeviceGainDb(move.path, move.oldGainDb);
            GainStagingManager::getInstance().clearApplied(move.deviceId);
        }
    }

    juce::String getDescription() const override {
        return "Gain Staging";
    }

  private:
    std::vector<GainStageMove> moves_;
};

}  // namespace

GainStagingManager& GainStagingManager::getInstance() {
    static GainStagingManager instance;
    return instance;
}

GainStagingManager::~GainStagingManager() {
    stopTimer();
}

void GainStagingManager::setTargetDb(float db) {
    targetDb_ = juce::jlimit(kGainStageMinGainDb, 0.0f, db);
}

// ============================================================================
// Pass control
// ============================================================================

void GainStagingManager::startCollection(TrackId trackId) {
    // Clear any prior pass first (keeps applied gains, drops badges).
    if (mode_ != GainStagingMode::Idle)
        reset();

    activeTrackId_ = trackId;
    buildStagedDeviceList(trackId);

    juce::Logger::writeToLog("[GainStaging] start pass on track " + juce::String((int)trackId) +
                             ": " + juce::String((int)staged_.size()) + " devices, target " +
                             juce::String(targetDb_, 1) + " dBFS");

    // A fresh pass on these devices supersedes any prior marks on them.
    for (const auto& staged : staged_)
        appliedDeltas_.erase(staged.deviceId);

    mode_ = GainStagingMode::Collecting;
    notifyMode();
    for (const auto& staged : staged_)
        notifyDevice(staged.deviceId);

    startTimerHz(30);
}

void GainStagingManager::stopCollection() {
    if (mode_ != GainStagingMode::Collecting)
        return;

    stopTimer();
    auto& tm = TrackManager::getInstance();

    std::vector<GainStageMove> moves;
    // Devices are serial: every move we make shifts the signal reaching the
    // devices after it. Carry that running adjustment forward so each stage is
    // brought to the target given its *effective* (post-upstream) level. Without
    // this, independent per-device targeting stacks the trims and the chain
    // collapses to target x N (i.e. silence). Order-aware musical decisions are
    // still the AI phase's job; this is just the linear bookkeeping.
    float cumulativeAppliedDb = 0.0f;
    for (const auto& staged : staged_) {
        auto it = info_.find(staged.deviceId);
        if (it == info_.end())
            continue;
        auto& info = it->second;

        // No signal seen during the pass -> leave the device untouched and
        // don't let it perturb the running adjustment.
        if (info.capturedPeakDb <= kGainStageSilenceDb + 0.5f) {
            info.state = DeviceGainStageState::Idle;
            notifyDevice(staged.deviceId);
            continue;
        }

        const auto* device = tm.getDeviceInChainByPath(staged.path);
        if (device == nullptr) {
            info.state = DeviceGainStageState::Idle;
            notifyDevice(staged.deviceId);
            continue;
        }

        const float currentGainDb = device->gainDb;
        // What this stage now outputs given the upstream moves already applied.
        const float effectiveOutputDb = info.capturedPeakDb + cumulativeAppliedDb;
        // Gain staging only TRIMS stages that exceed the target; it never boosts
        // a stage up to the target. A stage already sitting below target (e.g.
        // the last device after upstream trims) is left alone -- boosting it
        // would just re-inflate level for no reason.
        const float deltaDb = juce::jmin(0.0f, targetDb_ - effectiveOutputDb);
        const float newGainDb =
            juce::jlimit(kGainStageMinGainDb, kGainStageMaxGainDb, currentGainDb + deltaDb);
        const float applied = newGainDb - currentGainDb;
        const bool meaningful = std::abs(applied) >= 0.05f;

        if (meaningful) {
            moves.push_back({staged.path, staged.deviceId, currentGainDb, newGainDb});
            cumulativeAppliedDb += applied;
            info.appliedDeltaDb = applied;
        } else {
            info.appliedDeltaDb = 0.0f;
        }

        // Only flag devices that actually moved (or clipped during capture);
        // pass-through stages stay unmarked.
        info.state = info.clipped ? DeviceGainStageState::Clipped
                     : meaningful ? DeviceGainStageState::Adjusted
                                  : DeviceGainStageState::Idle;

        notifyDevice(staged.deviceId);
    }

    if (!moves.empty())
        UndoManager::getInstance().executeCommand(
            std::make_unique<GainStageCommand>(std::move(moves)));

    // Pass complete: drop the transient capture state and return to idle. The
    // applied marks (appliedDeltas_) persist, so the moved faders stay flagged.
    staged_.clear();
    info_.clear();
    activeTrackId_ = INVALID_TRACK_ID;
    mode_ = GainStagingMode::Idle;
    notifyMode();
}

void GainStagingManager::reset() {
    if (isTimerRunning())
        stopTimer();

    for (auto& [deviceId, info] : info_) {
        info.state = DeviceGainStageState::Idle;
        notifyDevice(deviceId);
    }

    staged_.clear();
    info_.clear();
    activeTrackId_ = INVALID_TRACK_ID;
    mode_ = GainStagingMode::Idle;
    notifyMode();
}

void GainStagingManager::toggle(TrackId trackId) {
    switch (mode_) {
        case GainStagingMode::Idle:
            startCollection(trackId);
            break;
        case GainStagingMode::Collecting:
            stopCollection();
            break;
    }
}

const DeviceGainStageInfo* GainStagingManager::getDeviceInfo(DeviceId deviceId) const {
    auto it = info_.find(deviceId);
    return it != info_.end() ? &it->second : nullptr;
}

const float* GainStagingManager::getAppliedDelta(DeviceId deviceId) const {
    auto it = appliedDeltas_.find(deviceId);
    return it != appliedDeltas_.end() ? &it->second : nullptr;
}

void GainStagingManager::markApplied(DeviceId deviceId, float deltaDb) {
    appliedDeltas_[deviceId] = deltaDb;
    notifyDevice(deviceId);
}

void GainStagingManager::clearApplied(DeviceId deviceId) {
    if (appliedDeltas_.erase(deviceId) > 0)
        notifyDevice(deviceId);
}

// ============================================================================
// Internals
// ============================================================================

void GainStagingManager::buildStagedDeviceList(TrackId trackId) {
    staged_.clear();
    info_.clear();

    auto& tm = TrackManager::getInstance();
    const auto* track = tm.getTrack(trackId);
    if (track == nullptr)
        return;

    // Main FX chain (device/rack tree) in signal order.
    std::vector<DeviceId> fxIds;
    collectChainDeviceIds(track->chain.fxChainElements, fxIds);
    for (auto deviceId : fxIds) {
        auto path = tm.findDevicePath(deviceId);
        if (path.isValid())
            staged_.push_back({deviceId, path});
    }

    // Post-FX stage (flat, devices only).
    for (const auto& element : track->chain.postFxChainElements) {
        auto path = ChainNodePath::postFxDevice(trackId, element.device.id);
        if (path.isValid())
            staged_.push_back({element.device.id, path});
    }

    for (const auto& staged : staged_) {
        DeviceGainStageInfo info;
        info.state = DeviceGainStageState::Collecting;
        info_[staged.deviceId] = info;
    }
}

bool GainStagingManager::readDevicePeakLinear(DeviceId deviceId, float& peakLinearOut) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr)
        return false;

    auto* bridge = engine->getAudioBridge();
    if (bridge == nullptr)
        return false;

    DeviceMeteringManager::DeviceMeterData data;
    if (!bridge->getDeviceMetering().getLatestLevels(deviceId, data))
        return false;

    peakLinearOut = std::max(data.peakL, data.peakR);
    return true;
}

void GainStagingManager::timerCallback() {
    if (mode_ != GainStagingMode::Collecting)
        return;

    for (const auto& staged : staged_) {
        float peakLinear = 0.0f;
        if (!readDevicePeakLinear(staged.deviceId, peakLinear))
            continue;

        auto& info = info_[staged.deviceId];
        const float peakDb = linearToDb(peakLinear);

        bool changed = false;
        if (peakDb > info.capturedPeakDb + 0.01f) {
            info.capturedPeakDb = peakDb;
            changed = true;
        }
        if (peakLinear >= 1.0f && !info.clipped) {
            info.clipped = true;
            changed = true;
        }

        // Only push updates when the held peak rises, to avoid flooding the
        // message thread at the timer rate.
        if (changed)
            notifyDevice(staged.deviceId);
    }
}

// ============================================================================
// Listeners
// ============================================================================

void GainStagingManager::addListener(GainStagingListener* listener) {
    if (listener != nullptr &&
        std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void GainStagingManager::removeListener(GainStagingListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void GainStagingManager::notifyMode() {
    for (auto* listener : listeners_)
        listener->gainStagingModeChanged(mode_, activeTrackId_);
}

void GainStagingManager::notifyDevice(DeviceId deviceId) {
    static const DeviceGainStageInfo idleInfo{};
    auto it = info_.find(deviceId);
    const auto& info = (it != info_.end()) ? it->second : idleInfo;
    for (auto* listener : listeners_)
        listener->deviceGainStageChanged(deviceId, info);
}

}  // namespace magda
