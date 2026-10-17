#include "PreviewStretchSource.hpp"

#include <signalsmith-stretch.h>

#include <algorithm>
#include <cmath>

namespace magda {

struct PreviewStretchSource::Engine {
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    juce::AudioBuffer<float> output;
};

PreviewStretchSource::PreviewStretchSource(juce::PositionableAudioSource* input, int numChannels)
    : input_(input), numChannels_(std::max(1, numChannels)), engine_(std::make_unique<Engine>()) {}

PreviewStretchSource::~PreviewStretchSource() = default;

void PreviewStretchSource::setRate(double rate) {
    rate_.store(std::clamp(rate, kMinRate, kMaxRate), std::memory_order_relaxed);
}

void PreviewStretchSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    input_->prepareToPlay(samplesPerBlockExpected, sampleRate);

    maxBlock_ = std::max(samplesPerBlockExpected, 64);
    auto& stretch = engine_->stretch;
    stretch.presetDefault(numChannels_, static_cast<float>(sampleRate));

    const int seekLength = stretch.outputSeekLength(static_cast<float>(kMaxRate));
    const int blockInput = static_cast<int>(std::ceil(maxBlock_ * kMaxRate)) + 2;
    inputBuffer_.setSize(numChannels_, std::max(seekLength, blockInput));
    engine_->output.setSize(numChannels_, maxBlock_);

    // outputSeek and process size scratch vectors on first use; do it here, not in the callback.
    inputBuffer_.clear();
    stretch.outputSeek(inputBuffer_.getArrayOfReadPointers(), seekLength);
    stretch.process(inputBuffer_.getArrayOfReadPointers(), maxBlock_,
                    engine_->output.getArrayOfWritePointers(), maxBlock_);
    needsPrime_ = true;
}

void PreviewStretchSource::releaseResources() {
    input_->releaseResources();
}

void PreviewStretchSource::readInput(int numSamples) {
    juce::AudioSourceChannelInfo in(&inputBuffer_, 0, numSamples);
    input_->getNextAudioBlock(in);
}

void PreviewStretchSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    const double rate = rate_.load(std::memory_order_relaxed);
    const bool stretching = std::abs(rate - 1.0) > 1.0e-4;

    if (!stretching) {
        if (wasStretching_) {
            input_->setNextReadPosition(getNextReadPosition());
            lead_ = 0;
            wasStretching_ = false;
        }
        input_->getNextAudioBlock(info);
        return;
    }

    auto& stretch = engine_->stretch;
    if (!wasStretching_ || needsPrime_) {
        const auto audible = getNextReadPosition();
        input_->setNextReadPosition(audible);
        const int seekLength = std::min(stretch.outputSeekLength(static_cast<float>(rate)),
                                        inputBuffer_.getNumSamples());
        readInput(seekLength);
        stretch.outputSeek(inputBuffer_.getArrayOfReadPointers(), seekLength);
        lead_ = seekLength;
        inputCarry_ = 0.0;
        needsPrime_ = false;
        wasStretching_ = true;
    }

    auto& out = engine_->output;
    int done = 0;
    while (done < info.numSamples) {
        const int n = std::min(info.numSamples - done, maxBlock_);
        const double exact = n * rate + inputCarry_;
        const int inN = std::min(static_cast<int>(exact), inputBuffer_.getNumSamples());
        inputCarry_ = exact - inN;

        readInput(inN);
        stretch.process(inputBuffer_.getArrayOfReadPointers(), inN, out.getArrayOfWritePointers(),
                        n);

        for (int c = 0; c < info.buffer->getNumChannels(); ++c)
            info.buffer->copyFrom(c, info.startSample + done, out, std::min(c, numChannels_ - 1), 0,
                                  n);
        done += n;
    }
}

void PreviewStretchSource::setNextReadPosition(juce::int64 newPosition) {
    input_->setNextReadPosition(newPosition);
    lead_ = 0;
    needsPrime_ = true;
}

juce::int64 PreviewStretchSource::getNextReadPosition() const {
    return std::max<juce::int64>(0, input_->getNextReadPosition() - lead_);
}

juce::int64 PreviewStretchSource::getTotalLength() const {
    return input_->getTotalLength();
}

bool PreviewStretchSource::isLooping() const {
    return input_->isLooping();
}

void PreviewStretchSource::setLooping(bool shouldLoop) {
    input_->setLooping(shouldLoop);
}

}  // namespace magda
