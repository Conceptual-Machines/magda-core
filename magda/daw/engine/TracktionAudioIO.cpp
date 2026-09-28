#include "TracktionAudioIO.hpp"

#include <algorithm>

#include "WaveDeviceChannels.hpp"

namespace magda {

namespace {

/** @brief The channels of @p waveDevices that are enabled, each under its device's name. */
HardwareChannels::Direction enabledOf(const auto& waveDevices, juce::StringArray channelNames) {
    HardwareChannels::Direction direction{.channelNames = std::move(channelNames)};
    for (auto* device : waveDevices) {
        if (device == nullptr || !device->isEnabled())
            continue;
        for (const auto& channel : device->getChannels()) {
            direction.open.setBit(channel.indexInDevice);
            direction.routeNames[channel.indexInDevice] = device->getName();
        }
    }
    return direction;
}

std::vector<int> channelsOf(const juce::BigInteger& mask) {
    std::vector<int> channels;
    for (auto bit = mask.findNextSetBit(0); bit >= 0; bit = mask.findNextSetBit(bit + 1))
        channels.push_back(bit);
    return channels;
}

}  // namespace

TracktionAudioIO::TracktionAudioIO(tracktion::DeviceManager& devices) : devices_(devices) {
    // Wave devices are rebuilt, and their enablement saved, before this is told.
    devices_.addChangeListener(this);
}

TracktionAudioIO::~TracktionAudioIO() {
    devices_.removeChangeListener(this);
}

bool TracktionAudioIO::isOpen() const {
    return devices_.deviceManager.getCurrentAudioDevice() != nullptr;
}

HardwareChannels::Direction TracktionAudioIO::inputs() const {
    auto* device = devices_.deviceManager.getCurrentAudioDevice();
    return enabledOf(devices_.getWaveInputDevices(),
                     device != nullptr ? device->getInputChannelNames() : juce::StringArray{});
}

HardwareChannels::Direction TracktionAudioIO::outputs() const {
    auto* device = devices_.deviceManager.getCurrentAudioDevice();
    return enabledOf(devices_.getWaveOutputDevices(),
                     device != nullptr ? device->getOutputChannelNames() : juce::StringArray{});
}

AudioIOSettings TracktionAudioIO::chosen() const {
    const auto& manager = devices_.deviceManager;
    const auto setup = manager.getAudioDeviceSetup();
    auto* device = manager.getCurrentAudioDevice();
    return {.backend = manager.getCurrentAudioDeviceType().toStdString(),
            .inputInterface = setup.inputDeviceName.toStdString(),
            .outputInterface = setup.outputDeviceName.toStdString(),
            .sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0,
            .bufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0,
            .inputChannels = channelsOf(inputs().open),
            .outputChannels = channelsOf(outputs().open)};
}

juce::String TracktionAudioIO::apply(const AudioIOSettings& settings) {
    auto& manager = devices_.deviceManager;
    if (manager.getCurrentAudioDeviceType() != juce::String(settings.backend))
        manager.setCurrentAudioDeviceType(settings.backend, true);

    auto setup = manager.getAudioDeviceSetup();
    setup.inputDeviceName = settings.inputInterface;
    setup.outputDeviceName = settings.outputInterface;
    setup.sampleRate = settings.sampleRate;
    setup.bufferSize = settings.bufferSize;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange(
        0, channelNames(settings.backend, settings.inputInterface, true).size(), true);
    setup.outputChannels.setRange(
        0, channelNames(settings.backend, settings.outputInterface, false).size(), true);

    if (auto error = manager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return error;

    // The wave devices for the new layout, now rather than on the next message.
    devices_.rescanWaveDeviceList();
    devices_.dispatchPendingUpdates();

    const auto selects = [](const std::vector<int>& channels) {
        return [&channels](int channel) { return std::ranges::contains(channels, channel); };
    };
    enableDevicesForChannels(devices_.getWaveInputDevices(), selects(settings.inputChannels));
    enableDevicesForChannels(devices_.getWaveOutputDevices(), selects(settings.outputChannels));
    return {};
}

juce::StringArray TracktionAudioIO::channelNames(const juce::String& backend,
                                                 const juce::String& interfaceName, bool inputs) {
    auto& manager = devices_.deviceManager;
    if (auto* device = manager.getCurrentAudioDevice();
        device != nullptr && device->getTypeName() == backend) {
        const auto setup = manager.getAudioDeviceSetup();
        if ((inputs ? setup.inputDeviceName : setup.outputDeviceName) == interfaceName)
            return inputs ? device->getInputChannelNames() : device->getOutputChannelNames();
    }

    for (auto* type : manager.getAvailableDeviceTypes()) {
        if (type->getTypeName() != backend || interfaceName.isEmpty())
            continue;
        const std::unique_ptr<juce::AudioIODevice> probe(type->createDevice(
            inputs ? juce::String() : interfaceName, inputs ? interfaceName : juce::String()));
        if (probe != nullptr)
            return inputs ? probe->getInputChannelNames() : probe->getOutputChannelNames();
    }
    return {};
}

void TracktionAudioIO::changeListenerCallback(juce::ChangeBroadcaster*) {
    notifyChanged();
}

juce::AudioDeviceManager& TracktionAudioIO::manager() const {
    return devices_.deviceManager;
}

juce::StringArray TracktionAudioIO::backendNames() {
    juce::StringArray names;
    for (const auto* backend : manager().getAvailableDeviceTypes())
        names.add(backend->getTypeName());
    return names;
}

juce::StringArray TracktionAudioIO::interfaceNames(const juce::String& backend, bool inputs) {
    for (auto* type : manager().getAvailableDeviceTypes()) {
        if (type->getTypeName() != backend)
            continue;
        type->scanForDevices();
        return type->getDeviceNames(inputs);
    }
    return {};
}

juce::String TracktionAudioIO::defaultInterface(const juce::String& backend, bool inputs) {
    for (auto* type : manager().getAvailableDeviceTypes()) {
        if (type->getTypeName() != backend)
            continue;
        type->scanForDevices();
        return type->getDeviceNames(inputs)[type->getDefaultDeviceIndex(inputs)];
    }
    return {};
}

bool TracktionAudioIO::isSingleInterfaceBackend(const juce::String& backend) {
    for (const auto* type : manager().getAvailableDeviceTypes())
        if (type->getTypeName() == backend)
            return !type->hasSeparateInputsAndOutputs();
    return false;
}

std::vector<double> TracktionAudioIO::availableSampleRates() const {
    std::vector<double> rates;
    if (auto* device = manager().getCurrentAudioDevice())
        for (const auto rate : device->getAvailableSampleRates())
            rates.push_back(rate);
    return rates;
}

std::vector<int> TracktionAudioIO::availableBufferSizes() const {
    std::vector<int> sizes;
    if (auto* device = manager().getCurrentAudioDevice())
        for (const auto size : device->getAvailableBufferSizes())
            sizes.push_back(size);
    return sizes;
}

void TracktionAudioIO::addCallback(juce::AudioIODeviceCallback* callback) {
    manager().addAudioCallback(callback);
}

void TracktionAudioIO::removeCallback(juce::AudioIODeviceCallback* callback) {
    manager().removeAudioCallback(callback);
}

void TracktionAudioIO::setMidiInputEnabled(const juce::String& identifier, bool enabled) {
    manager().setMidiInputDeviceEnabled(identifier, enabled);
}

bool TracktionAudioIO::isMidiInputEnabled(const juce::String& identifier) const {
    return manager().isMidiInputDeviceEnabled(identifier);
}

void TracktionAudioIO::setDefaultMidiOutput(const juce::String& identifier) {
    manager().setDefaultMidiOutputDevice(identifier);
}

juce::String TracktionAudioIO::defaultMidiOutput() const {
    return manager().getDefaultMidiOutputIdentifier();
}

AudioIOControl::Status TracktionAudioIO::status() const {
    Status status;
    if (auto* device = manager().getCurrentAudioDevice()) {
        status.interfaceName = device->getName();
        status.deviceOpen = device->isOpen();
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();
    }
    status.cpuUsage = manager().getCpuUsage();
    status.xruns = manager().getXRunCount();
    return status;
}

}  // namespace magda
