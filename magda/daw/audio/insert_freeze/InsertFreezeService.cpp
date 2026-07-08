#include "insert_freeze/InsertFreezeService.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "audio/AudioBridge.hpp"
#include "core/ChainNodePath.hpp"
#include "core/ClipManager.hpp"
#include "core/ExternalInsertFreeze.hpp"
#include "core/InternalDeviceKind.hpp"
#include "core/TrackManager.hpp"
#include "plugins/InsertCapturePlugin.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

namespace te = tracktion;

namespace {

// Release tail captured past the last clip so decays/reverbs from the outboard
// gear are not cut off.
constexpr double kFreezeTailSeconds = 2.0;

// Transport pre-roll before the capture window so the graph and the hardware
// round-trip are settled when the window opens.
constexpr double kFreezePreRollSeconds = 1.0;

constexpr int kProgressTimerHz = 10;

double projectTempoOrDefault() {
    double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    return tempo > 0.0 ? tempo : 120.0;
}

}  // namespace

struct InsertFreezeService::ActivePass {
    TrackId trackId = INVALID_TRACK_ID;
    DeviceId deviceId = INVALID_DEVICE_ID;

    te::Plugin::Ptr capturePlugin;  // keeps the hidden tap alive until removal
    juce::File captureFile;

    double windowStartSec = 0.0;
    double windowEndSec = 0.0;
    double rangeStartBeats = 0.0;
    double rangeEndBeats = 0.0;  // includes the tail

    // Transport state to restore after the pass.
    double savedPositionSec = 0.0;
    bool savedLooping = false;
};

InsertFreezeService::InsertFreezeService(te::Edit& edit, AudioBridge& audioBridge)
    : edit_(edit), audioBridge_(audioBridge) {}

InsertFreezeService::~InsertFreezeService() {
    if (pass_ != nullptr)
        finishPass(false, {});
}

bool InsertFreezeService::isFreezing(TrackId trackId, DeviceId deviceId) const {
    return pass_ != nullptr && pass_->trackId == trackId && pass_->deviceId == deviceId;
}

double InsertFreezeService::getActiveFreezeProgress() const {
    if (pass_ == nullptr)
        return 0.0;
    auto* capture = dynamic_cast<InsertCapturePlugin*>(pass_->capturePlugin.get());
    const double windowLength = pass_->windowEndSec - pass_->windowStartSec;
    if (capture == nullptr || windowLength <= 0.0)
        return 0.0;
    return juce::jlimit(0.0, 1.0, capture->getCapturedSeconds() / windowLength);
}

bool InsertFreezeService::startFreeze(TrackId trackId, DeviceId deviceId, juce::String& errorOut) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (pass_ != nullptr) {
        errorOut = "A freeze pass is already running";
        return false;
    }

    auto& trackManager = TrackManager::getInstance();
    auto* device = trackManager.getDevice(trackId, deviceId);
    if (device == nullptr ||
        classifyInternalDevice(device->pluginId) != InternalDeviceKind::ExternalInsert) {
        errorOut = "Device is not an external insert";
        return false;
    }
    if (device->isFrozen()) {
        errorOut = "Device is already frozen";
        return false;
    }

    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);
    auto plugin = audioBridge_.getPlugin(devicePath);
    auto* insert = dynamic_cast<te::InsertPlugin*>(plugin.get());
    if (insert == nullptr) {
        errorOut = "External insert is not loaded";
        return false;
    }
    if (insert->outputDevice.get().isEmpty() || insert->inputDevice.get().isEmpty()) {
        errorOut = "Choose a send and a return device first";
        return false;
    }
    if (!insert->isEnabled()) {
        errorOut = "External insert is bypassed";
        return false;
    }

    auto* teTrack = audioBridge_.getAudioTrack(trackId);
    if (teTrack == nullptr) {
        errorOut = "Track is not loaded";
        return false;
    }

    // Capture range: the extent of the track's arrangement clips plus a tail.
    const double tempo = projectTempoOrDefault();
    double startBeats = 0.0, endBeats = 0.0;
    bool hasClips = false;
    for (const auto& clip : ClipManager::getInstance().getArrangementClips()) {
        if (clip.trackId != trackId)
            continue;
        const double clipStart = clip.getStartBeats(tempo);
        const double clipEnd = clip.getEndBeats(tempo);
        if (!hasClips || clipStart < startBeats)
            startBeats = clipStart;
        if (!hasClips || clipEnd > endBeats)
            endBeats = clipEnd;
        hasClips = true;
    }
    if (!hasClips) {
        errorOut = "Track has no clips to freeze";
        return false;
    }

    auto& transport = edit_.getTransport();
    if (transport.isPlaying() || transport.isRecording())
        transport.stop(false, false);

    const double startSec =
        edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(startBeats)).inSeconds();
    const double endSec =
        edit_.tempoSequence.toTime(te::BeatPosition::fromBeats(endBeats)).inSeconds() +
        kFreezeTailSeconds;
    const double endBeatsWithTail =
        edit_.tempoSequence.toBeats(te::TimePosition::fromSeconds(endSec)).inBeats();

    // Capture file under the project media dir.
    auto mediaDir = ProjectManager::getInstance().getMediaDirectory();
    if (mediaDir == juce::File()) {
        errorOut = "Save the project first (the capture needs a media folder)";
        return false;
    }
    auto trackName = trackManager.getTrack(trackId) != nullptr
                         ? trackManager.getTrack(trackId)->name
                         : juce::String("track");
    auto baseName = (trackName + "_" + device->name + "_" +
                     juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"))
                        .replaceCharacters(" /\\:", "____");
    auto captureFile = mediaDir.getChildFile("Freeze").getChildFile(baseName + ".wav");

    // Hidden capture tap directly after the insert, so it records the PDC-
    // aligned audio return (same pattern as FollowerSourceTapPlugin).
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, InsertCapturePlugin::xmlTypeName, nullptr);
    auto capturePlugin = edit_.getPluginCache().createNewPlugin(pluginState);
    auto* capture = dynamic_cast<InsertCapturePlugin*>(capturePlugin.get());
    if (capture == nullptr) {
        errorOut = "Could not create the capture plugin";
        return false;
    }

    const int insertIndex = teTrack->pluginList.indexOf(insert);
    teTrack->pluginList.insertPlugin(capturePlugin, insertIndex + 1, nullptr);

    // The plugin's initialise() only runs once the graph rebuild picks it up;
    // the writer needs the rate now, so pass the device rate explicitly.
    const double sampleRate = edit_.engine.getDeviceManager().getSampleRate();
    if (!capture->startCapture(captureFile, startSec, endSec, sampleRate)) {
        capturePlugin->deleteFromParent();
        errorOut = "Could not create the capture file";
        return false;
    }

    pass_ = std::make_unique<ActivePass>();
    pass_->trackId = trackId;
    pass_->deviceId = deviceId;
    pass_->capturePlugin = capturePlugin;
    pass_->captureFile = captureFile;
    pass_->windowStartSec = startSec;
    pass_->windowEndSec = endSec;
    pass_->rangeStartBeats = startBeats;
    pass_->rangeEndBeats = endBeatsWithTail;
    pass_->savedPositionSec = transport.getPosition().inSeconds();
    pass_->savedLooping = transport.looping;

    transport.looping = false;
    transport.setPosition(
        te::TimePosition::fromSeconds(std::max(0.0, startSec - kFreezePreRollSeconds)));
    transport.play(false);

    startTimerHz(kProgressTimerHz);
    return true;
}

void InsertFreezeService::cancelFreeze() {
    if (pass_ != nullptr)
        finishPass(false, {});
}

void InsertFreezeService::timerCallback() {
    if (pass_ == nullptr) {
        stopTimer();
        return;
    }

    auto* capture = dynamic_cast<InsertCapturePlugin*>(pass_->capturePlugin.get());
    if (capture == nullptr) {
        finishPass(false, "Capture plugin disappeared");
        return;
    }

    if (capture->isCaptureComplete()) {
        finishPass(true, {});
        return;
    }

    // The user stopping the transport mid-pass cancels the freeze.
    if (!edit_.getTransport().isPlaying())
        finishPass(false, {});
}

void InsertFreezeService::finishPass(bool success, const juce::String& error) {
    stopTimer();
    if (pass_ == nullptr)
        return;

    auto pass = std::move(pass_);

    auto& transport = edit_.getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);
    transport.looping = pass->savedLooping;
    transport.setPosition(te::TimePosition::fromSeconds(pass->savedPositionSec));

    if (auto* capture = dynamic_cast<InsertCapturePlugin*>(pass->capturePlugin.get()))
        capture->stopCapture(success);
    if (pass->capturePlugin != nullptr)
        pass->capturePlugin->deleteFromParent();

    if (success)
        applyFreezeResult(*pass);
    else if (error.isNotEmpty())
        DBG("InsertFreezeService: pass failed: " << error);

    // Success lands as a model change (setDeviceExternalFreeze →
    // notifyTrackDevicesChanged), which rebuilds the slot UI. A cancelled or
    // failed pass ends the isFreezing() state the UI polls.
}

void InsertFreezeService::applyFreezeResult(ActivePass& pass) {
    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();
    const double tempo = projectTempoOrDefault();

    auto freeze = std::make_shared<ExternalInsertFreeze>();
    freeze->captureFile =
        pass.captureFile.getRelativePathFrom(ProjectManager::getInstance().getMediaDirectory());

    // Stash and remove the track's arrangement clips.
    for (const auto& clip : clipManager.getArrangementClips())
        if (clip.trackId == pass.trackId)
            freeze->stashedClips.push_back(clip);
    for (const auto& clip : freeze->stashedClips)
        clipManager.deleteClip(clip.id);

    // The captured return, placed over the capture range (incl. tail).
    freeze->frozenClipId = clipManager.createAudioClipBeats(
        pass.trackId, pass.rangeStartBeats, pass.rangeEndBeats - pass.rangeStartBeats,
        pass.captureFile.getFullPathName(), ClipView::Arrangement, tempo);
    if (freeze->frozenClipId == INVALID_CLIP_ID) {
        // Clip creation failed — put the stashed clips back and abandon.
        for (const auto& clip : freeze->stashedClips) {
            clipManager.restoreClip(clip);
            audioBridge_.syncClipToEngine(clip.id);
        }
        pass.captureFile.deleteFile();
        return;
    }
    audioBridge_.syncClipToEngine(freeze->frozenClipId);

    // Bypass the insert and every device before it: the capture is post-insert
    // (wet), so pre-insert devices must not re-process the frozen clip. Devices
    // after the insert stay live. Only devices we actually bypassed are
    // recorded, so unfreeze restores the user's own bypass states.
    if (auto* track = trackManager.getTrack(pass.trackId)) {
        for (const auto& element : track->chain.fxChainElements) {
            if (!isDevice(element))
                continue;
            const auto& device = getDevice(element);
            if (!device.bypassed) {
                trackManager.setDeviceBypassed(pass.trackId, device.id, true);
                freeze->bypassedDevices.push_back(device.id);
            }
            if (device.id == pass.deviceId)
                break;
        }
    }

    trackManager.setDeviceExternalFreeze(pass.trackId, pass.deviceId, std::move(freeze));
}

bool InsertFreezeService::unfreeze(TrackId trackId, DeviceId deviceId, juce::String& errorOut) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();

    auto* device = trackManager.getDevice(trackId, deviceId);
    if (device == nullptr || !device->isFrozen()) {
        errorOut = "Device is not frozen";
        return false;
    }
    auto freeze = device->externalFreeze;

    clipManager.deleteClip(freeze->frozenClipId);

    const double tempo = projectTempoOrDefault();
    for (auto clip : freeze->stashedClips) {  // by value: re-derive seconds at the live tempo
        clip.deriveTimesFromBeats(tempo);
        clipManager.restoreClip(clip);
        audioBridge_.syncClipToEngine(clip.id);
    }

    for (auto bypassedId : freeze->bypassedDevices)
        trackManager.setDeviceBypassed(trackId, bypassedId, false);

    if (freeze->captureFile.isNotEmpty())
        ProjectManager::getInstance()
            .getMediaDirectory()
            .getChildFile(freeze->captureFile)
            .deleteFile();

    trackManager.setDeviceExternalFreeze(trackId, deviceId, nullptr);
    return true;
}

}  // namespace magda
