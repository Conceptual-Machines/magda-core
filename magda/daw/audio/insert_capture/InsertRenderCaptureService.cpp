#include "insert_capture/InsertRenderCaptureService.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/UserAlert.hpp"
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

// Rewrite a capture wav at the offline render's rate. The capture is recorded
// at the live device rate but the playback tap reads the file 1:1 into
// render-rate blocks, so a mismatched file would come out time-stretched.
// Message thread, offline: allocation and blocking file IO are fine here.
bool resampleCaptureFile(const juce::File& file, double targetRate) {
    juce::WavAudioFormat format;
    auto inStream = file.createInputStream();
    if (inStream == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader(
        format.createReaderFor(inStream.release(), true));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return false;
    if (reader->sampleRate == targetRate)
        return true;

    const auto tempFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_rs.wav");
    tempFile.deleteFile();
    {
        std::unique_ptr<juce::OutputStream> outStream = tempFile.createOutputStream();
        if (outStream == nullptr)
            return false;
        auto writerOptions =
            juce::AudioFormatWriterOptions()
                .withSampleRate(targetRate)
                .withNumChannels(static_cast<int>(reader->numChannels))
                .withBitsPerSample(32)
                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = format.createWriterFor(outStream, writerOptions);
        if (writer == nullptr)
            return false;

        juce::AudioFormatReaderSource readerSource(reader.get(), false);
        juce::ResamplingAudioSource resampler(&readerSource, false,
                                              static_cast<int>(reader->numChannels));
        resampler.setResamplingRatio(reader->sampleRate / targetRate);
        constexpr int blockSize = 4096;
        resampler.prepareToPlay(blockSize, targetRate);
        const auto outLength = std::llround(static_cast<double>(reader->lengthInSamples) *
                                            targetRate / reader->sampleRate);
        const bool ok =
            writer->writeFromAudioSource(resampler, static_cast<int>(outLength), blockSize);
        resampler.releaseResources();
        if (!ok) {
            tempFile.deleteFile();
            return false;
        }
    }
    reader.reset();
    return file.deleteFile() && tempFile.moveFileTo(file);
}

}  // namespace

struct InsertRenderCaptureService::Taps {
    std::vector<te::Plugin::Ptr> plugins;
    std::vector<juce::File> files;
};

InsertRenderCaptureService::InsertRenderCaptureService(te::Edit& edit) : edit_(edit) {}

InsertRenderCaptureService::~InsertRenderCaptureService() {
    try {
        if (pass_ != nullptr)
            finishPass(false);
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[InsertRenderCaptureService] ") + e.what());
        const juce::String message = juce::String("Render capture cleanup failed: ") + e.what();
        juce::MessageManager::callAsync([message] { magda::notifyUserAlert(message); });
    } catch (...) {
        juce::Logger::writeToLog("[InsertRenderCaptureService] unknown exception during teardown");
        juce::MessageManager::callAsync(
            [] { magda::notifyUserAlert("Render capture cleanup failed"); });
    }

    // Must run even if finishPass() above threw: removeTaps() releases the
    // capture plugins and temp files, which would otherwise leak.
    try {
        cleanupAfterRender();
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[InsertRenderCaptureService] ") + e.what());
        const juce::String message = juce::String("Render capture cleanup failed: ") + e.what();
        juce::MessageManager::callAsync([message] { magda::notifyUserAlert(message); });
    } catch (...) {
        juce::Logger::writeToLog("[InsertRenderCaptureService] unknown exception during teardown");
        juce::MessageManager::callAsync(
            [] { magda::notifyUserAlert("Render capture cleanup failed"); });
    }
}

bool InsertRenderCaptureService::exportNeedsCapturePass() const {
    return !routedInserts(edit_).empty();
}

bool InsertRenderCaptureService::startCapturePass(double startSec, double endSec,
                                                  double renderSampleRate,
                                                  std::function<void(bool)> onFinished) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    jassert(renderSampleRate > 0.0);

    if (pass_ != nullptr || endSec <= startSec) {
        lastError_ = PassError::SetupFailed;
        return false;
    }
    lastError_ = PassError::None;
    cleanupAfterRender();  // stale taps from an aborted previous export

    const auto inserts = routedInserts(edit_);
    if (inserts.empty())
        return false;  // nothing to capture: not an error

    auto& transport = edit_.getTransport();
    if (transport.isPlaying() || transport.isRecording())
        transport.stop(false, false);

    const double sampleRate = edit_.engine.getDeviceManager().getSampleRate();
    auto tempDir = edit_.getTempDirectory(true);

    // All-or-nothing: a qualifying insert that cannot get a tap would
    // silently export silence for its return, so any arming failure fails
    // the whole pass.
    auto taps = std::make_unique<Taps>();
    bool armFailed = false;
    for (auto* insert : inserts) {
        auto* ownerList = insert->getOwnerList();
        if (ownerList == nullptr) {
            armFailed = true;
            break;
        }

        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, InsertCapturePlugin::xmlTypeName, nullptr);
        auto tapPlugin = edit_.getPluginCache().createNewPlugin(pluginState);
        auto* tap = dynamic_cast<InsertCapturePlugin*>(tapPlugin.get());
        if (tap == nullptr) {
            armFailed = true;
            break;
        }

        auto file = tempDir.getChildFile("insert_capture_" +
                                         juce::String(insert->itemID.getRawID()) + ".wav");
        ownerList->insertPlugin(tapPlugin, ownerList->indexOf(insert) + 1, nullptr);

        if (!tap->startCapture(file, startSec, endSec, sampleRate)) {
            tapPlugin->deleteFromParent();
            armFailed = true;
            break;
        }
        taps->plugins.push_back(tapPlugin);
        taps->files.push_back(file);
    }

    if (armFailed) {
        taps_ = std::move(taps);
        removeTaps();
        lastError_ = PassError::SetupFailed;
        return false;
    }

    taps_ = std::move(taps);
    pass_ = std::make_unique<ActivePass>();
    pass_->windowStartSec = startSec;
    pass_->windowEndSec = endSec;
    pass_->renderSampleRate = renderSampleRate;
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
        if (tap != nullptr && tap->hasCaptureFailed()) {
            // A tap's file is already unfaithful (refused FIFO write or a
            // mid-capture device rate change): fail the pass now.
            lastError_ = PassError::CaptureFailed;
            finishPass(false);
            return;
        }
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
        // Finalise every capture, match each file to the render rate and flip
        // the taps to playback mode: the upcoming offline render substitutes
        // the recorded returns in place.
        for (size_t i = 0; i < taps_->plugins.size(); ++i) {
            if (auto* tap = dynamic_cast<InsertCapturePlugin*>(taps_->plugins[i].get())) {
                tap->stopCapture(true);
                if (tap->hasCaptureFailed() ||
                    !resampleCaptureFile(taps_->files[i], pass->renderSampleRate) ||
                    !tap->startPlayback(taps_->files[i], pass->windowStartSec,
                                        pass->windowEndSec)) {
                    lastError_ = PassError::CaptureFailed;
                    success = false;
                }
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
