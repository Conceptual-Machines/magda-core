#include "HardwareInputLevels.hpp"

#include <algorithm>
#include <cmath>

namespace magda {

namespace {

constexpr double kReleaseSeconds = 0.1;

}  // namespace

void InputChannelLevels::setActiveInputs(const juce::BigInteger& active, double sampleRate) {
    activeCount_ = 0;
    for (auto channel = 0; channel <= active.getHighestBit() && channel < kMaxChannels; ++channel)
        if (active[channel])
            physicalOf_[static_cast<std::size_t>(activeCount_++)] = channel;

    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& level : levels_)
        level.store(0.0f, std::memory_order_relaxed);
}

void InputChannelLevels::measure(const float* const* inputs, int numInputs, int numSamples) {
    if (inputs == nullptr || numSamples <= 0)
        return;

    const auto decay = static_cast<float>(
        std::exp(-static_cast<double>(numSamples) / (sampleRate_ * kReleaseSeconds)));

    for (auto index = 0; index < std::min(numInputs, activeCount_); ++index) {
        if (inputs[index] == nullptr)
            continue;

        const auto range = juce::FloatVectorOperations::findMinAndMax(inputs[index], numSamples);
        const auto peak = std::max(-range.getStart(), range.getEnd());

        auto& level =
            levels_[static_cast<std::size_t>(physicalOf_[static_cast<std::size_t>(index)])];
        level.store(std::max(peak, level.load(std::memory_order_relaxed) * decay),
                    std::memory_order_relaxed);
    }
}

float InputChannelLevels::level(int physicalChannel) const {
    if (physicalChannel < 0 || physicalChannel >= kMaxChannels)
        return 0.0f;
    return levels_[static_cast<std::size_t>(physicalChannel)].load(std::memory_order_relaxed);
}

std::shared_ptr<HardwareInputLevels> HardwareInputLevels::acquire(
    juce::AudioDeviceManager& devices) {
    static std::weak_ptr<HardwareInputLevels> shared;

    if (auto existing = shared.lock(); existing != nullptr && &existing->devices_ == &devices)
        return existing;

    std::shared_ptr<HardwareInputLevels> created(new HardwareInputLevels(devices));
    shared = created;
    return created;
}

HardwareInputLevels::HardwareInputLevels(juce::AudioDeviceManager& devices) : devices_(devices) {
    devices_.addAudioCallback(this);
}

HardwareInputLevels::~HardwareInputLevels() {
    devices_.removeAudioCallback(this);
}

void HardwareInputLevels::audioDeviceIOCallbackWithContext(
    const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
    int numSamples, const juce::AudioIODeviceCallbackContext&) {
    levels_.measure(inputs, numInputs, numSamples);

    // The manager sums this into the device output from a buffer it does not
    // clear between callbacks (AudioDeviceManager::audioDeviceIOCallbackInt).
    for (auto channel = 0; channel < numOutputs; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputs[channel], numSamples);
}

void HardwareInputLevels::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    if (device != nullptr)
        levels_.setActiveInputs(device->getActiveInputChannels(), device->getCurrentSampleRate());
}

void HardwareInputLevels::audioDeviceStopped() {
    levels_.setActiveInputs({}, 0.0);
}

}  // namespace magda
