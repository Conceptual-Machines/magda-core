#include "plugins/InsertCapturePlugin.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include "insert_capture/CaptureWindowMath.hpp"

namespace magda {

const char* InsertCapturePlugin::xmlTypeName = "insertcapture";

namespace {
// FIFO headroom between the audio thread and the background write thread.
constexpr int kWriterBufferSeconds = 4;
}  // namespace

InsertCapturePlugin::InsertCapturePlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {}

InsertCapturePlugin::~InsertCapturePlugin() {
    stopCapture(false);
    stopPlayback();
    notifyListenersOfDeletion();
}

bool InsertCapturePlugin::startPlayback(const juce::File& wavFile, double windowStartSec,
                                        double windowEndSec) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    stopPlayback();

    juce::WavAudioFormat format;
    auto stream = wavFile.createInputStream();
    if (stream == nullptr)
        return false;
    playbackReader_.reset(format.createReaderFor(stream.release(), true));
    if (playbackReader_ == nullptr)
        return false;

    windowStart_ = windowStartSec;
    windowEnd_ = windowEndSec;
    activeReader_.store(playbackReader_.get(), std::memory_order_release);
    return true;
}

void InsertCapturePlugin::stopPlayback() {
    activeReader_.store(nullptr, std::memory_order_release);
    playbackReader_.reset();
}

void InsertCapturePlugin::initialise(const te::PluginInitialisationInfo& info) {
    // Keep the armed rate: the writer was created against it and a mismatch
    // here (device rate change mid-capture) is aborted by the service.
    if (!isCapturing())
        sampleRate_ = info.sampleRate;
    zeroBuf_.assign(static_cast<size_t>(juce::jmax(4096, info.blockSizeSamples)), 0.0f);
}

bool InsertCapturePlugin::startCapture(const juce::File& wavFile, double windowStartSec,
                                       double windowEndSec, double sampleRate) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    stopCapture(false);
    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;
    if (zeroBuf_.empty())
        zeroBuf_.assign(4096, 0.0f);

    wavFile.getParentDirectory().createDirectory();
    wavFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = wavFile.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat format;
    auto writerOptions =
        juce::AudioFormatWriterOptions()
            .withSampleRate(sampleRate_)
            .withNumChannels(kNumChannels)
            .withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, writerOptions);
    if (writer == nullptr)
        return false;

    writeThread_.startThread();
    threadedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
        writer.release(), writeThread_,
        static_cast<int>(sampleRate_) * kNumChannels * kWriterBufferSeconds);

    captureFile_ = wavFile;
    windowStart_ = windowStartSec;
    windowEnd_ = windowEndSec;
    nextFileSample_ = 0;
    samplesWritten_.store(0, std::memory_order_relaxed);
    complete_.store(false, std::memory_order_relaxed);
    activeWriter_.store(threadedWriter_.get(), std::memory_order_release);
    return true;
}

void InsertCapturePlugin::stopCapture(bool keepFile) {
    if (threadedWriter_ == nullptr)
        return;

    activeWriter_.store(nullptr, std::memory_order_release);
    // An audio block that loaded the writer before the store may still be
    // writing; give it comfortably more than a block's duration to finish.
    // Callers stop the transport first, so this is belt-and-braces.
    juce::Thread::sleep(80);
    threadedWriter_.reset();  // flushes the FIFO and finalises the wav header
    writeThread_.stopThread(2000);

    if (!keepFile)
        captureFile_.deleteFile();
    captureFile_ = juce::File();
}

void InsertCapturePlugin::writeSilence(juce::AudioFormatWriter::ThreadedWriter& writer,
                                       juce::int64 numSamples) {
    const float* chans[kNumChannels];
    for (int c = 0; c < kNumChannels; ++c)
        chans[c] = zeroBuf_.data();

    while (numSamples > 0) {
        const auto chunk = std::min(numSamples, static_cast<juce::int64>(zeroBuf_.size()));
        writer.write(chans, static_cast<int>(chunk));
        numSamples -= chunk;
        nextFileSample_ += chunk;
    }
}

void InsertCapturePlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
        return;

    // Playback mode: substitute the captured return during an offline render.
    // The isRendering gate keeps file reads off the live audio thread.
    if (auto* reader = activeReader_.load(std::memory_order_acquire); reader != nullptr) {
        if (!fc.isRendering)
            return;  // live graph: transparent passthrough
        const auto m = mapBlockToCaptureWindow(
            fc.editTime.getStart().inSeconds(), fc.editTime.getEnd().inSeconds(),
            fc.bufferNumSamples, windowStart_, windowEnd_, reader->sampleRate);
        // REPLACE semantics: whatever the offline insert produced (silence or
        // dry passthrough) must not mix with the captured return.
        fc.destBuffer->clear(fc.bufferStartSample, fc.bufferNumSamples);
        if (m.numSamples > 0 && m.fileStartSample < (juce::int64)reader->lengthInSamples) {
            const auto available = static_cast<int>(
                juce::jmin<juce::int64>(m.numSamples, reader->lengthInSamples - m.fileStartSample));
            reader->read(fc.destBuffer, fc.bufferStartSample + m.bufferOffset, available,
                         m.fileStartSample, true, true);
        }
        return;
    }

    // Capture mode: transparent passthrough — never modify the buffer.
    auto* writer = activeWriter_.load(std::memory_order_acquire);
    if (writer == nullptr || !fc.isPlaying || fc.isRendering)
        return;
    if (complete_.load(std::memory_order_relaxed))
        return;

    const auto m = mapBlockToCaptureWindow(fc.editTime.getStart().inSeconds(),
                                           fc.editTime.getEnd().inSeconds(), fc.bufferNumSamples,
                                           windowStart_, windowEnd_, sampleRate_);
    if (m.windowFinished) {
        complete_.store(true, std::memory_order_release);
        return;
    }
    if (m.numSamples <= 0)
        return;

    // The wav is written sequentially; reconcile the block's window position
    // against the write cursor (rounding jitter or a dropped block leaves a
    // gap — pad it with silence; an overlap is trimmed).
    auto fileStart = m.fileStartSample;
    auto bufferOffset = m.bufferOffset;
    auto numSamples = m.numSamples;
    if (fileStart > nextFileSample_)
        writeSilence(*writer, fileStart - nextFileSample_);
    else if (fileStart < nextFileSample_) {
        const auto trim = nextFileSample_ - fileStart;
        if (trim >= numSamples)
            return;
        bufferOffset += static_cast<int>(trim);
        numSamples -= static_cast<int>(trim);
    }

    const int srcChannels = fc.destBuffer->getNumChannels();
    const float* chans[kNumChannels];
    for (int c = 0; c < kNumChannels; ++c)
        chans[c] = c < srcChannels
                       ? fc.destBuffer->getReadPointer(c, fc.bufferStartSample + bufferOffset)
                       : zeroBuf_.data();

    writer->write(chans, numSamples);
    nextFileSample_ += numSamples;
    samplesWritten_.store(nextFileSample_, std::memory_order_relaxed);

    if (fc.editTime.getEnd().inSeconds() >= windowEnd_)
        complete_.store(true, std::memory_order_release);
}

}  // namespace magda
