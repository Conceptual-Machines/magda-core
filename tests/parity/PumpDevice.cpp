#include "PumpDevice.hpp"

namespace magda::parity {

PumpDevice::PumpDevice(double sampleRate, int blockSize, std::atomic<PumpDevice*>& registry)
    : juce::AudioIODevice(kPumpInterface, kPumpBackend),
      sampleRate_(sampleRate),
      blockSize_(blockSize),
      registry_(registry) {}

PumpDevice::~PumpDevice() {
    close();
}

std::chrono::steady_clock::duration PumpDevice::pull(juce::AudioBuffer<float>& output) {
    const std::scoped_lock lock(callbackLock_);
    if (callback_ == nullptr) {
        output.clear();
        return {};
    }

    const juce::AudioIODeviceCallbackContext context{};
    const auto started = std::chrono::steady_clock::now();
    callback_->audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(),
                                                output.getNumChannels(), output.getNumSamples(),
                                                context);
    return std::chrono::steady_clock::now() - started;
}

bool PumpDevice::isStarted() const {
    return started_.load();
}

juce::StringArray PumpDevice::getOutputChannelNames() {
    juce::StringArray names;
    for (int channel = 0; channel < kPumpOutputChannels; ++channel)
        names.add("Out " + juce::String(channel + 1));
    return names;
}

juce::StringArray PumpDevice::getInputChannelNames() {
    return {};
}

juce::Array<double> PumpDevice::getAvailableSampleRates() {
    return {sampleRate_};
}

juce::Array<int> PumpDevice::getAvailableBufferSizes() {
    return {blockSize_};
}

int PumpDevice::getDefaultBufferSize() {
    return blockSize_;
}

juce::String PumpDevice::open(const juce::BigInteger&, const juce::BigInteger& outputs, double,
                              int) {
    outputs_ = outputs;
    open_ = true;
    registry_.store(this);
    return {};
}

void PumpDevice::close() {
    stop();
    open_ = false;
    auto* self = this;
    registry_.compare_exchange_strong(self, nullptr);
}

bool PumpDevice::isOpen() {
    return open_;
}

void PumpDevice::start(juce::AudioIODeviceCallback* callback) {
    if (callback == nullptr)
        return;

    callback->audioDeviceAboutToStart(this);

    const std::scoped_lock lock(callbackLock_);
    callback_ = callback;
    started_ = true;
}

void PumpDevice::stop() {
    juce::AudioIODeviceCallback* stopped = nullptr;
    {
        const std::scoped_lock lock(callbackLock_);
        stopped = std::exchange(callback_, nullptr);
        started_ = false;
    }

    if (stopped != nullptr)
        stopped->audioDeviceStopped();
}

bool PumpDevice::isPlaying() {
    return started_.load();
}

juce::String PumpDevice::getLastError() {
    return {};
}

int PumpDevice::getCurrentBufferSizeSamples() {
    return blockSize_;
}

double PumpDevice::getCurrentSampleRate() {
    return sampleRate_;
}

int PumpDevice::getCurrentBitDepth() {
    return 32;
}

juce::BigInteger PumpDevice::getActiveOutputChannels() const {
    return outputs_;
}

juce::BigInteger PumpDevice::getActiveInputChannels() const {
    return {};
}

int PumpDevice::getOutputLatencyInSamples() {
    return 0;
}

int PumpDevice::getInputLatencyInSamples() {
    return 0;
}

PumpBackend::PumpBackend(double sampleRate, int blockSize)
    : juce::AudioIODeviceType(kPumpBackend), sampleRate_(sampleRate), blockSize_(blockSize) {}

juce::StringArray PumpBackend::getDeviceNames(bool wantInputNames) const {
    return wantInputNames ? juce::StringArray{} : juce::StringArray{kPumpInterface};
}

int PumpBackend::getDefaultDeviceIndex(bool) const {
    return 0;
}

int PumpBackend::getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const {
    return !asInput && device != nullptr && device->getName() == kPumpInterface ? 0 : -1;
}

bool PumpBackend::hasSeparateInputsAndOutputs() const {
    return true;
}

juce::AudioIODevice* PumpBackend::createDevice(const juce::String& outputName,
                                               const juce::String&) {
    if (outputName != kPumpInterface)
        return nullptr;
    return new PumpDevice(sampleRate_, blockSize_, device_);
}

}  // namespace magda::parity
