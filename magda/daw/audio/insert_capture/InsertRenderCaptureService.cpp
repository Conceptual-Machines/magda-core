#include "insert_capture/InsertRenderCaptureService.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <vector>

#include "plugins/InsertCapturePlugin.hpp"

namespace magda {

namespace te = tracktion;

namespace {

// Transport pre-roll before the capture window so the graph and the hardware
// round-trip are settled when the window opens.
constexpr double kPreRollSeconds = 1.0;

constexpr int kProgressTimerHz = 10;

// Every enabled external insert with both a send and a return configured —
// the ones whose returns the offline render cannot produce.
std::vector<te::InsertPlugin*> routedInserts(te::Edit& edit) {
    std::vector<te::InsertPlugin*> result;
    for (auto plugin : te::getAllPlugins(edit, false)) {
        auto* insert = dynamic_cast<te::InsertPlugin*>(plugin);
        if (insert != nullptr && insert->isEnabled() && insert->outputDevice.get().isNotEmpty() &&
            insert->inputDevice.get().isNotEmpty())
            result.push_back(insert);
    }
    return result;
}

}  // namespace

struct InsertRenderCaptureService::Taps {
    std::vector<te::Plugin::Ptr> plugins;
    std::vector<juce::File> files;
};

InsertRenderCaptureService::InsertRenderCaptureService(te::Edit& edit) : edit_(edit) {}

InsertRenderCaptureService::~InsertRenderCaptureService() {
    if (pass_ != nullptr)
        finishPass(false);
    cleanupAfterRender();
}

bool InsertRenderCaptureService::exportNeedsCapturePass() const {
    return !routedInserts(edit_).empty();
}

bool InsertRenderCaptureService::startCapturePass(double startSec, double endSec,
                                                  std::function<void(bool)> onFinished) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (pass_ != nullptr || endSec <= startSec)
        return false;
    cleanupAfterRender();  // stale taps from an aborted previous export

    const auto inserts = routedInserts(edit_);
    if (inserts.empty())
        return false;

    auto& transport = edit_.getTransport();
    if (transport.isPlaying() || transport.isRecording())
        transport.stop(false, false);

    const double sampleRate = edit_.engine.getDeviceManager().getSampleRate();
    auto tempDir = edit_.getTempDirectory(true);

    auto taps = std::make_unique<Taps>();
    for (auto* insert : inserts) {
        auto* ownerList = insert->getOwnerList();
        if (ownerList == nullptr)
            continue;

        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, InsertCapturePlugin::xmlTypeName, nullptr);
        auto tapPlugin = edit_.getPluginCache().createNewPlugin(pluginState);
        auto* tap = dynamic_cast<InsertCapturePlugin*>(tapPlugin.get());
        if (tap == nullptr)
            continue;

        auto file = tempDir.getChildFile("insert_capture_" +
                                         juce::String(insert->itemID.getRawID()) + ".wav");
        ownerList->insertPlugin(tapPlugin, ownerList->indexOf(insert) + 1, nullptr);

        if (!tap->startCapture(file, startSec, endSec, sampleRate)) {
            tapPlugin->deleteFromParent();
            continue;
        }
        taps->plugins.push_back(tapPlugin);
        taps->files.push_back(file);
    }

    if (taps->plugins.empty())
        return false;

    taps_ = std::move(taps);
    pass_ = std::make_unique<ActivePass>();
    pass_->windowStartSec = startSec;
    pass_->windowEndSec = endSec;
    pass_->savedPositionSec = transport.getPosition().inSeconds();
    pass_->savedLooping = transport.looping;
    pass_->onFinished = std::move(onFinished);

    transport.looping = false;
    transport.setPosition(te::TimePosition::fromSeconds(std::max(0.0, startSec - kPreRollSeconds)));
    transport.play(false);

    startTimerHz(kProgressTimerHz);
    return true;
}

void InsertRenderCaptureService::cancelCapturePass() {
    if (pass_ != nullptr)
        finishPass(false);
}

double InsertRenderCaptureService::getProgress() const {
    if (pass_ == nullptr || taps_ == nullptr)
        return 0.0;
    const double windowLength = pass_->windowEndSec - pass_->windowStartSec;
    if (windowLength <= 0.0)
        return 0.0;
    // The pass is done when the SLOWEST tap has covered the window.
    double minSeconds = windowLength;
    for (const auto& plugin : taps_->plugins)
        if (auto* tap = dynamic_cast<InsertCapturePlugin*>(plugin.get()))
            minSeconds = std::min(minSeconds, tap->getCapturedSeconds());
    return juce::jlimit(0.0, 1.0, minSeconds / windowLength);
}

void InsertRenderCaptureService::timerCallback() {
    if (pass_ == nullptr || taps_ == nullptr) {
        stopTimer();
        return;
    }

    bool allComplete = true;
    for (const auto& plugin : taps_->plugins) {
        auto* tap = dynamic_cast<InsertCapturePlugin*>(plugin.get());
        if (tap == nullptr || !tap->isCaptureComplete())
            allComplete = false;
    }
    if (allComplete) {
        finishPass(true);
        return;
    }

    // The user stopping the transport mid-pass cancels the capture.
    if (!edit_.getTransport().isPlaying())
        finishPass(false);
}

void InsertRenderCaptureService::finishPass(bool success) {
    stopTimer();
    if (pass_ == nullptr)
        return;

    auto pass = std::move(pass_);

    auto& transport = edit_.getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);
    transport.looping = pass->savedLooping;
    transport.setPosition(te::TimePosition::fromSeconds(pass->savedPositionSec));

    if (success && taps_ != nullptr) {
        // Finalise every capture and flip the taps to playback mode: the
        // upcoming offline render substitutes the recorded returns in place.
        for (size_t i = 0; i < taps_->plugins.size(); ++i) {
            if (auto* tap = dynamic_cast<InsertCapturePlugin*>(taps_->plugins[i].get())) {
                tap->stopCapture(true);
                if (!tap->startPlayback(taps_->files[i], pass->windowStartSec, pass->windowEndSec))
                    success = false;
            }
        }
    }
    if (!success)
        cleanupAfterRender();

    if (pass->onFinished)
        pass->onFinished(success);
}

void InsertRenderCaptureService::removeTaps() {
    if (taps_ == nullptr)
        return;
    for (auto& plugin : taps_->plugins) {
        if (auto* tap = dynamic_cast<InsertCapturePlugin*>(plugin.get())) {
            tap->stopCapture(false);
            tap->stopPlayback();
        }
        if (plugin != nullptr)
            plugin->deleteFromParent();
    }
    for (auto& file : taps_->files)
        file.deleteFile();
    taps_.reset();
}

void InsertRenderCaptureService::cleanupAfterRender() {
    removeTaps();
}

}  // namespace magda
