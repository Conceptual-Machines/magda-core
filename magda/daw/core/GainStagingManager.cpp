#include "GainStagingManager.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

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
    return juce::Decibels::gainToDecibels(linear, kGainStageSilenceDb);
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

struct CascadeSuggestion {
    float gainDb = 0.0f;
    float appliedDb = 0.0f;
    bool sawSignal = false;
    bool meaningful = false;
};

CascadeSuggestion computeCascadeSuggestion(float capturedPeakDb, float currentGainDb,
                                           float cumulativeAppliedDb, float targetDb) {
    CascadeSuggestion suggestion;
    suggestion.gainDb = currentGainDb;
    suggestion.sawSignal = capturedPeakDb > kGainStageSilenceDb + 0.5f;
    if (!suggestion.sawSignal)
        return suggestion;

    const float effectiveOutputDb = capturedPeakDb + cumulativeAppliedDb;
    const float deltaDb = juce::jmin(0.0f, targetDb - effectiveOutputDb);
    suggestion.gainDb =
        juce::jlimit(kGainStageMinGainDb, kGainStageMaxGainDb, currentGainDb + deltaDb);
    suggestion.appliedDb = suggestion.gainDb - currentGainDb;
    suggestion.meaningful = std::abs(suggestion.appliedDb) >= 0.05f;
    return suggestion;
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
            GainStagingManager::getInstance().markApplied(move.path,
                                                          move.newGainDb - move.oldGainDb);
        }
    }

    void undo() override {
        auto& tm = TrackManager::getInstance();
        for (const auto& move : moves_) {
            tm.setDeviceGainDb(move.path, move.oldGainDb);
            GainStagingManager::getInstance().clearApplied(move.path);
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
        appliedDeltas_.erase(staged.path);

    mode_ = GainStagingMode::Collecting;
    notifyMode();
    for (const auto& staged : staged_)
        notifyDevice(staged.path);

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
        auto it = info_.find(staged.path);
        if (it == info_.end())
            continue;
        auto& info = it->second;

        const auto* device = tm.getDeviceInChainByPath(staged.path);
        if (device == nullptr) {
            info.state = DeviceGainStageState::Idle;
            notifyDevice(staged.path);
            continue;
        }

        const auto suggestion = computeCascadeSuggestion(info.capturedPeakDb, device->gainDb,
                                                         cumulativeAppliedDb, targetDb_);

        // No signal seen during the pass -> leave the device untouched and
        // don't let it perturb the running adjustment.
        if (!suggestion.sawSignal) {
            info.state = DeviceGainStageState::Idle;
            notifyDevice(staged.path);
            continue;
        }

        if (suggestion.meaningful) {
            moves.push_back({staged.path, staged.deviceId, device->gainDb, suggestion.gainDb});
            cumulativeAppliedDb += suggestion.appliedDb;
            info.appliedDeltaDb = suggestion.appliedDb;
        } else {
            info.appliedDeltaDb = 0.0f;
        }

        // Only flag devices that actually moved (or clipped during capture);
        // pass-through stages stay unmarked.
        info.state = info.clipped            ? DeviceGainStageState::Clipped
                     : suggestion.meaningful ? DeviceGainStageState::Adjusted
                                             : DeviceGainStageState::Idle;

        notifyDevice(staged.path);
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

    for (auto& [path, info] : info_) {
        info.state = DeviceGainStageState::Idle;
        notifyDevice(path);
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

std::vector<GainStagingManager::DeviceSnapshot> GainStagingManager::finishCaptureForAi() {
    std::vector<DeviceSnapshot> out;
    if (mode_ != GainStagingMode::Collecting)
        return out;

    stopTimer();
    auto& tm = TrackManager::getInstance();

    // Mirror the algorithmic cascade so we can hand the agent the exact trim
    // that lands each stage at the target. The agent anchors to this number and
    // only deviates for musical reasons -- LLMs are poor at dB arithmetic, so
    // doing the math for them keeps the result close to target.
    float cumulativeAppliedDb = 0.0f;

    for (const auto& staged : staged_) {
        auto it = info_.find(staged.path);
        if (it == info_.end())
            continue;
        const auto* device = tm.getDeviceInChainByPath(staged.path);
        if (device == nullptr)
            continue;

        DeviceSnapshot s;
        s.deviceId = staged.deviceId;
        s.path = staged.path;
        s.name = device->name;
        s.pluginId = device->pluginId;
        s.isInstrument = device->isInstrument;
        s.capturedPeakDb = it->second.capturedPeakDb;
        s.currentGainDb = device->gainDb;

        const auto suggestion = computeCascadeSuggestion(it->second.capturedPeakDb, device->gainDb,
                                                         cumulativeAppliedDb, targetDb_);
        s.suggestedGainDb = suggestion.gainDb;
        if (suggestion.meaningful)
            cumulativeAppliedDb += suggestion.appliedDb;

        // Send current settings for MAGDA/internal devices so the agent can
        // reason about thresholds, drive, ceilings. External plugins are
        // skipped (their parameter sets are large and opaque).
        if (device->format == PluginFormat::Internal) {
            for (const auto& p : device->parameters)
                s.params.push_back({p.name, p.currentValue, p.unit});
        }

        out.push_back(s);
    }

    // Clear the per-device capture highlight (otherwise devices stay stuck in
    // the red "collecting" state through the whole AI wait). The UI shows the
    // "getting AI results" state on the header instead. staged_ is KEPT so
    // applyAiMoves() can still resolve paths; info_ is no longer needed (it
    // fed the snapshot we just built).
    info_.clear();
    for (const auto& staged : staged_)
        notifyDevice(staged.path);

    mode_ = GainStagingMode::Idle;
    notifyMode();
    return out;
}

void GainStagingManager::applyAiMoves(
    const std::vector<std::pair<ChainNodePath, float>>& deviceNewGainDb) {
    auto& tm = TrackManager::getInstance();

    std::vector<GainStageMove> moves;
    for (const auto& [path, requestedGainDb] : deviceNewGainDb) {
        if (!path.isValid())
            continue;

        const auto* device = tm.getDeviceInChainByPath(path);
        if (device == nullptr)
            continue;

        const float newGainDb =
            juce::jlimit(kGainStageMinGainDb, kGainStageMaxGainDb, requestedGainDb);
        if (std::abs(newGainDb - device->gainDb) < 0.05f)
            continue;  // negligible move

        moves.push_back({path, path.getDeviceId(), device->gainDb, newGainDb});
    }

    if (!moves.empty())
        UndoManager::getInstance().executeCommand(
            std::make_unique<GainStageCommand>(std::move(moves)));

    // Drop the frozen capture; the applied marks live on in appliedDeltas_.
    staged_.clear();
    info_.clear();
    activeTrackId_ = INVALID_TRACK_ID;
    mode_ = GainStagingMode::Idle;
    notifyMode();
}

const DeviceGainStageInfo* GainStagingManager::getDeviceInfo(
    const ChainNodePath& devicePath) const {
    auto it = info_.find(devicePath);
    return it != info_.end() ? &it->second : nullptr;
}

const float* GainStagingManager::getAppliedDelta(const ChainNodePath& devicePath) const {
    auto it = appliedDeltas_.find(devicePath);
    return it != appliedDeltas_.end() ? &it->second : nullptr;
}

void GainStagingManager::markApplied(const ChainNodePath& devicePath, float deltaDb) {
    appliedDeltas_[devicePath] = deltaDb;
    notifyDevice(devicePath);
}

void GainStagingManager::clearApplied(const ChainNodePath& devicePath) {
    if (appliedDeltas_.erase(devicePath) > 0)
        notifyDevice(devicePath);
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
        info_[staged.path] = info;
    }
}

bool GainStagingManager::readDevicePeakLinear(const ChainNodePath& devicePath,
                                              float& peakLinearOut) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr)
        return false;

    auto* bridge = engine->getAudioBridge();
    if (bridge == nullptr)
        return false;

    DeviceMeteringManager::DeviceMeterData data;
    if (!bridge->getDeviceMetering().getLatestLevels(devicePath, data))
        return false;

    peakLinearOut = std::max(data.peakL, data.peakR);
    return true;
}

void GainStagingManager::timerCallback() {
    if (mode_ != GainStagingMode::Collecting)
        return;

    for (const auto& staged : staged_) {
        float peakLinear = 0.0f;
        if (!readDevicePeakLinear(staged.path, peakLinear))
            continue;

        auto& info = info_[staged.path];
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
            notifyDevice(staged.path);
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

void GainStagingManager::notifyDevice(const ChainNodePath& devicePath) {
    static const DeviceGainStageInfo idleInfo{};
    auto it = info_.find(devicePath);
    const auto& info = (it != info_.end()) ? it->second : idleInfo;
    for (auto* listener : listeners_)
        listener->deviceGainStageChanged(devicePath, info);
}

}  // namespace magda
