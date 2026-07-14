#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace magda {

namespace te = tracktion;

/**
 * @brief Hidden audio-thread plugin that bridges an external insert's audio
 *        return into offline export (#1623).
 *
 * Placed directly after a te::InsertPlugin in the TE plugin list (never in the
 * MAGDA device model), so the signal it sees is the insert's audio return,
 * PDC-aligned into the timeline. Two mutually exclusive modes:
 *
 * - **Capture** (live pass): writes every block that intersects the capture
 *   window to a juce::AudioFormatWriter::ThreadedWriter (lock-free FIFO +
 *   background write thread) and flags completion once the window has fully
 *   passed. Transparent passthrough; never active while rendering.
 * - **Playback** (offline render): REPLACES the buffer with the captured file
 *   at the same chain position, substituting the hardware return the offline
 *   graph cannot produce. Gated on PluginRenderContext::isRendering, so the
 *   live graph passes through untouched and file reads never happen on the
 *   live audio thread.
 *
 * Registered via MagdaEngineBehaviour::createCustomPlugin(); orchestrated by
 * InsertRenderCaptureService.
 */
class InsertCapturePlugin : public te::Plugin {
  public:
    InsertCapturePlugin(const te::PluginCreationInfo& info);
    ~InsertCapturePlugin() override;

    static const char* getPluginName() {
        return "Insert Capture";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "InsCapture";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& fc) override;

    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override {}

    // ------------------------------------------------------------------
    // Message-thread capture control (InsertRenderCaptureService)

    /** Start capturing [windowStartSec, windowEndSec) to a stereo float wav.
        sampleRate is passed explicitly because arming happens right after the
        plugin is inserted, before the graph rebuild runs initialise().
        Returns false if the file/writer could not be created. */
    bool startCapture(const juce::File& wavFile, double windowStartSec, double windowEndSec,
                      double sampleRate);

    /** Finalise (keepFile) or abort (!keepFile, deletes the file) the capture.
        An in-flight audio block may still hold the writer; this waits
        (bounded) for the writer-in-use handshake to clear before destroying
        it. */
    void stopCapture(bool keepFile);

    /** Arm offline-render playback of a previously captured window: while the
        context is rendering, the buffer is REPLACED with the file's content at
        the window position (silence outside it). No effect on live playback.
        Returns false if the file cannot be read. */
    bool startPlayback(const juce::File& wavFile, double windowStartSec, double windowEndSec);

    /** Disarm playback. Call while no offline render is running. */
    void stopPlayback();

    bool isCapturing() const {
        return activeWriter_.load(std::memory_order_acquire) != nullptr;
    }
    bool isCaptureComplete() const {
        return complete_.load(std::memory_order_acquire);
    }
    /** True when the capture can no longer produce a faithful file: the FIFO
        refused a write (the rest of the take would be time-shifted) or the
        device sample rate changed mid-capture. The service fails the pass. */
    bool hasCaptureFailed() const {
        return captureFailed_.load(std::memory_order_acquire);
    }
    /** Seconds of the window written so far — progress for the UI. */
    double getCapturedSeconds() const {
        return static_cast<double>(samplesWritten_.load(std::memory_order_relaxed)) /
               std::max(1.0, sampleRate_);
    }

    static constexpr int kNumChannels = 2;

  private:
    juce::TimeSliceThread writeThread_{"Insert Capture Writer"};
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter_{nullptr};

    // Offline-render playback state. The reader is only dereferenced when the
    // render context reports isRendering, i.e. on render threads — never on
    // the live audio thread — so plain blocking file reads are fine.
    std::unique_ptr<juce::AudioFormatReader> playbackReader_;
    std::atomic<juce::AudioFormatReader*> activeReader_{nullptr};

    // Window bounds are written before activeWriter_ is published (release) and
    // only read on the audio thread after the acquire load — plain doubles.
    double windowStart_ = 0.0;
    double windowEnd_ = 0.0;

    std::atomic<juce::int64> samplesWritten_{0};
    std::atomic<bool> complete_{false};
    std::atomic<bool> captureFailed_{false};
    juce::int64 nextFileSample_ = 0;  // audio-thread only: sequential-write cursor

    // Handshake with stopCapture(): incremented before the audio thread loads
    // activeWriter_, decremented when it is done with it (both seq_cst so the
    // store-load pairing with stopCapture's nullptr store is race-free).
    std::atomic<int> writersInUse_{0};

    double sampleRate_ = 44100.0;
    std::vector<float> zeroBuf_;  // pre-allocated silence for gap padding
    juce::File captureFile_;

    bool writeSilence(juce::AudioFormatWriter::ThreadedWriter& writer, juce::int64 numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertCapturePlugin)
};

}  // namespace magda
