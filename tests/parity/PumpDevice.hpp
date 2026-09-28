#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <chrono>
#include <mutex>

/**
 * @file PumpDevice.hpp
 * @brief An audio interface with no hardware behind it, pulled one callback at a time (#2082).
 *
 * Registered with an engine's juce::AudioDeviceManager and opened through the engine's own
 * AudioIOControl::apply, so each pull runs the manager's callback exactly as a device would:
 * load measurement included, then whatever engine is attached to it.
 */

namespace magda::parity {

inline constexpr const char* kPumpBackend = "Parity Pump";
inline constexpr const char* kPumpInterface = "Pump";
inline constexpr int kPumpOutputChannels = 2;

class PumpDevice final : public juce::AudioIODevice {
  public:
    /// @p registry holds this device while it is open. The manager's probes never open.
    PumpDevice(double sampleRate, int blockSize, std::atomic<PumpDevice*>& registry);
    ~PumpDevice() override;

    /// Runs one callback into @p output and returns how long the callback took.
    std::chrono::steady_clock::duration pull(juce::AudioBuffer<float>& output);

    /// Whether a callback is attached, which the manager does once the device starts.
    bool isStarted() const;

    juce::StringArray getOutputChannelNames() override;
    juce::StringArray getInputChannelNames() override;
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;
    juce::String open(const juce::BigInteger& inputs, const juce::BigInteger& outputs, double,
                      int) override;
    void close() override;
    bool isOpen() override;
    void start(juce::AudioIODeviceCallback* callback) override;
    void stop() override;
    bool isPlaying() override;
    juce::String getLastError() override;
    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override;
    juce::BigInteger getActiveOutputChannels() const override;
    juce::BigInteger getActiveInputChannels() const override;
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;

  private:
    const double sampleRate_;
    const int blockSize_;
    std::atomic<PumpDevice*>& registry_;

    /// Held across a callback and across start/stop, so a stop waits for the callback to end.
    std::mutex callbackLock_;
    juce::AudioIODeviceCallback* callback_ = nullptr;
    std::atomic<bool> started_{false};

    juce::BigInteger outputs_;
    bool open_ = false;
};

/** @brief The backend the pump is listed under. Remembers the device the manager made. */
class PumpBackend final : public juce::AudioIODeviceType {
  public:
    PumpBackend(double sampleRate, int blockSize);

    /// The device the manager opened, or null before it has. Owned by the manager.
    PumpDevice* device() const {
        return device_.load();
    }

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool wantInputNames) const override;
    int getDefaultDeviceIndex(bool) const override;
    int getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const override;
    bool hasSeparateInputsAndOutputs() const override;
    juce::AudioIODevice* createDevice(const juce::String& outputName,
                                      const juce::String& inputName) override;

  private:
    const double sampleRate_;
    const int blockSize_;
    std::atomic<PumpDevice*> device_{nullptr};
};

}  // namespace magda::parity
