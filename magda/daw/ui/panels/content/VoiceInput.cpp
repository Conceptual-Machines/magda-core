// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VoiceInput.hpp"

#include <algorithm>

#include "../../../../agents/speech_transcriber.hpp"
#include "../../../audio/io/AudioIOControl.hpp"
#include "../../../core/TrackManager.hpp"
#include "SpeechModel.hpp"
#include "engine/AudioEngine.hpp"

namespace magda::daw::ui {

namespace {

/// Whisper decodes one 30 s window; anything longer would be cut anyway.
constexpr double kMaxSeconds = 30.0;

/// Shorter than this is a click on the button, not speech.
constexpr double kMinSeconds = 0.3;

/// A held key repeats after about half a second; a toggle sooner than this after the last is one.
constexpr juce::uint32 kToggleRepeatMs = 600;

/** @brief Whisper's names for silence and noise, such as "[BLANK_AUDIO]" or "(wind)". */
bool isNonSpeech(const juce::String& text) {
    return (text.startsWithChar('[') && text.endsWithChar(']')) ||
           (text.startsWithChar('(') && text.endsWithChar(')'));
}

}  // namespace

void MonoCapture::prepare(int capacity) {
    samples_.assign(static_cast<std::size_t>(std::max(0, capacity)), 0.0f);
    written_.store(0, std::memory_order_release);
}

void MonoCapture::append(const float* const* inputs, int numInputs, int numSamples) {
    const int start = written_.load(std::memory_order_relaxed);
    const int count = std::min(numSamples, static_cast<int>(samples_.size()) - start);
    if (inputs == nullptr || count <= 0)
        return;

    auto* destination = samples_.data() + start;
    juce::FloatVectorOperations::clear(destination, count);
    for (int channel = 0; channel < numInputs; ++channel)
        if (inputs[channel] != nullptr)
            juce::FloatVectorOperations::add(destination, inputs[channel], count);

    written_.store(start + count, std::memory_order_release);
}

class VoiceInput::Worker final : public juce::Thread {
  public:
    Worker(VoiceInput& owner, std::vector<float> samples, double sampleRate, std::string prompt)
        : juce::Thread("Voice transcription"),
          owner_(&owner),
          samples_(std::move(samples)),
          sampleRate_(sampleRate),
          prompt_(std::move(prompt)) {}

    ~Worker() override {
        abort_.store(true, std::memory_order_relaxed);
        stopThread(10000);
    }

    void run() override {
        const auto audio = SpeechTranscriber::toWhisperRate(
            samples_.data(), static_cast<int>(samples_.size()), sampleRate_);
        const auto result = SpeechTranscriber::getInstance().transcribe(audio, prompt_, &abort_);
        if (abort_.load(std::memory_order_relaxed))
            return;

        juce::MessageManager::callAsync(
            [owner = owner_, text = juce::String(result.text), error = juce::String(result.error)] {
                if (auto* self = owner.get())
                    self->finishTranscription(text, error);
            });
    }

  private:
    juce::WeakReference<VoiceInput> owner_;
    std::vector<float> samples_;
    double sampleRate_;
    std::string prompt_;
    std::atomic<bool> abort_{false};
};

VoiceInput::VoiceInput() {
    SpeechModel::getInstance().addChangeListener(this);
}

VoiceInput::~VoiceInput() {
    SpeechModel::getInstance().removeChangeListener(this);
    if (audio_ != nullptr)
        audio_->removeCallback(this);
    worker_.reset();
}

void VoiceInput::preload() {
    SpeechModel::getInstance().load();
}

bool VoiceInput::isBusy() const {
    return audio_ != nullptr || worker_ != nullptr || SpeechModel::getInstance().isDownloading();
}

void VoiceInput::press() {
    if (isBusy())
        return;

    if (!SpeechTranscriber::isAvailable()) {
        status("This build of MAGDA has no local inference, so the mic is unavailable.");
        return;
    }
    if (!SpeechModel::getInstance().isDownloaded()) {
        showingDownload_ = true;
        status("Downloading the speech model...");
        SpeechModel::getInstance().download();
        return;
    }

    auto* engine = TrackManager::getInstance().getAudioEngine();
    auto* audio = engine != nullptr ? engine->getAudioIO() : nullptr;
    if (audio == nullptr) {
        status("No audio interface is open.");
        return;
    }

    // addCallback() runs audioDeviceAboutToStart() first when an interface is open.
    sampleRate_ = 0.0;
    numInputs_ = 0;
    audio->addCallback(this);
    if (numInputs_ == 0 || sampleRate_ <= 0.0) {
        audio->removeCallback(this);
        status("No audio inputs are open. Turn one on in Audio Settings.");
        return;
    }

    audio_ = audio;
    status("Listening...");
}

void VoiceInput::release(const juce::String& prompt) {
    if (audio_ == nullptr)
        return;

    audio_->removeCallback(this);
    audio_ = nullptr;

    const int length = capture_.size();
    if (length < static_cast<int>(sampleRate_ * kMinSeconds)) {
        status("Hold the mic button while you speak.");
        return;
    }

    std::vector<float> samples(capture_.data(), capture_.data() + length);
    worker_ =
        std::make_unique<Worker>(*this, std::move(samples), sampleRate_, prompt.toStdString());
    worker_->startThread();
    status("Transcribing...");
}

void VoiceInput::toggle(const juce::String& prompt) {
    const auto now = juce::Time::getMillisecondCounter();
    const bool repeat = now - lastToggleMs_ < kToggleRepeatMs;
    lastToggleMs_ = now;
    if (repeat)
        return;

    if (isRecording())
        release(prompt);
    else
        press();
}

void VoiceInput::changeListenerCallback(juce::ChangeBroadcaster*) {
    const auto& model = SpeechModel::getInstance();
    if (model.isDownloading()) {
        showingDownload_ = true;
        status("Downloading the speech model: " +
               juce::String(juce::roundToInt(model.downloadProgress() * 100.0)) + "%");
    } else if (showingDownload_) {
        showingDownload_ = false;
        status(model.isDownloaded()
                   ? juce::String("Speech model ready. Hold the mic button to talk.")
                   : model.lastError());
    }
}

void VoiceInput::finishTranscription(const juce::String& text, const juce::String& error) {
    worker_.reset();
    SpeechModel::getInstance().notify();  // the first transcription may have loaded the model

    if (error.isNotEmpty()) {
        status(error);
        return;
    }
    if (text.isEmpty() || isNonSpeech(text)) {
        status("Didn't catch that.");
        return;
    }

    status({});
    if (onTranscript)
        onTranscript(text);
}

void VoiceInput::status(const juce::String& text) {
    if (onStatus)
        onStatus(text);
}

void VoiceInput::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                                  float* const* outputs, int numOutputs,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&) {
    capture_.append(inputs, numInputs, numSamples);

    // The manager sums this into the device output from a buffer it does not clear.
    for (int channel = 0; channel < numOutputs; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputs[channel], numSamples);
}

void VoiceInput::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device == nullptr)
        return;
    sampleRate_ = device->getCurrentSampleRate();
    numInputs_ = device->getActiveInputChannels().countNumberOfSetBits();
    capture_.prepare(static_cast<int>(sampleRate_ * kMaxSeconds));
}

}  // namespace magda::daw::ui
