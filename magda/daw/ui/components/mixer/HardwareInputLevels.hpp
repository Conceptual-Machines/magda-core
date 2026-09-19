#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>

namespace magda {

/**
 * @brief Recent peak per physical input channel, fed from a device callback.
 *
 * The audio thread writes and any thread reads. A peak decays over about a
 * tenth of a second, so a reader polling at frame rate still sees a transient.
 */
class InputChannelLevels {
  public:
    static constexpr int kMaxChannels = 128;

    /// Which physical channel each callback index is. Never while measure() runs.
    void setActiveInputs(const juce::BigInteger& active, double sampleRate);

    /// Audio thread.
    void measure(const float* const* inputs, int numInputs, int numSamples);

    /// Linear peak on @p physicalChannel, 0 for a channel not open.
    float level(int physicalChannel) const;

  private:
    std::array<std::atomic<float>, kMaxChannels> levels_{};
    std::array<int, kMaxChannels> physicalOf_{};
    int activeCount_ = 0;
    double sampleRate_ = 44100.0;
};

/**
 * @brief The open device's input levels, measured only while someone holds this.
 *
 * A second callback on the device manager, so it reads the interface under
 * either engine and costs nothing with no input menu open. Message thread.
 */
class HardwareInputLevels final : private juce::AudioIODeviceCallback {
  public:
    /// The instance measuring @p devices, shared by every holder.
    static std::shared_ptr<HardwareInputLevels> acquire(juce::AudioDeviceManager& devices);

    ~HardwareInputLevels() override;

    float level(int physicalChannel) const {
        return levels_.level(physicalChannel);
    }

  private:
    explicit HardwareInputLevels(juce::AudioDeviceManager& devices);

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager& devices_;
    InputChannelLevels levels_;
};

}  // namespace magda
