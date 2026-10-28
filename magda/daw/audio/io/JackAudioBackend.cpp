// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "JackAudioBackend.hpp"

#include <algorithm>

#if defined(__linux__)

    #include <dlfcn.h>
    #include <jack/jack.h>

    #include <atomic>
    #include <vector>

namespace magda::jack {
namespace {

constexpr const char* kClientName = "MAGDA";

/// libjack, dlopened so MAGDA runs without it.
struct Api {
    decltype(&::jack_client_open) client_open;
    decltype(&::jack_client_close) client_close;
    decltype(&::jack_activate) activate;
    decltype(&::jack_deactivate) deactivate;
    decltype(&::jack_get_buffer_size) get_buffer_size;
    decltype(&::jack_get_sample_rate) get_sample_rate;
    decltype(&::jack_on_shutdown) on_shutdown;
    decltype(&::jack_set_process_callback) set_process_callback;
    decltype(&::jack_set_xrun_callback) set_xrun_callback;
    decltype(&::jack_get_ports) get_ports;
    decltype(&::jack_port_by_name) port_by_name;
    decltype(&::jack_port_flags) port_flags;
    decltype(&::jack_port_name) port_name;
    decltype(&::jack_port_register) port_register;
    decltype(&::jack_port_unregister) port_unregister;
    decltype(&::jack_port_get_buffer) port_get_buffer;
    decltype(&::jack_port_get_latency_range) port_get_latency_range;
    decltype(&::jack_connect) connect;
    decltype(&::jack_free) free;
};

/**
 * @brief PipeWire's libjack when PipeWire is running, else the system's.
 *
 * Ubuntu's pipewire-jack installs its libjack outside the library path, so a plain
 * dlopen finds the standalone JACK library, which sees no server under PipeWire (#3107).
 */
void* openLibrary() {
    const auto runtimeDir = juce::SystemStats::getEnvironmentVariable("XDG_RUNTIME_DIR", {});
    if (runtimeDir.isNotEmpty() && juce::File(runtimeDir).getChildFile("pipewire-0").exists()) {
        for (const auto* dir : {"/usr/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu",
                                "/usr/lib64", "/usr/lib"}) {
            const auto path = juce::String(dir) + "/pipewire-0.3/jack/libjack.so.0";
            if (auto* handle = dlopen(path.toRawUTF8(), RTLD_LAZY))
                return handle;
        }
    }

    if (auto* handle = dlopen("libjack.so.0", RTLD_LAZY))
        return handle;
    return dlopen("libjack.so", RTLD_LAZY);
}

template <typename Fn> bool bind(void* library, Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(dlsym(library, name));
    return fn != nullptr;
}

/** @brief libjack's functions, or nullptr when it is missing or incomplete. */
const Api* api() {
    static const auto loaded = []() -> std::unique_ptr<Api> {
        auto* library = openLibrary();
        if (library == nullptr)
            return nullptr;

        auto jack = std::make_unique<Api>();
        const auto complete =
            bind(library, jack->client_open, "jack_client_open") &&
            bind(library, jack->client_close, "jack_client_close") &&
            bind(library, jack->activate, "jack_activate") &&
            bind(library, jack->deactivate, "jack_deactivate") &&
            bind(library, jack->get_buffer_size, "jack_get_buffer_size") &&
            bind(library, jack->get_sample_rate, "jack_get_sample_rate") &&
            bind(library, jack->on_shutdown, "jack_on_shutdown") &&
            bind(library, jack->set_process_callback, "jack_set_process_callback") &&
            bind(library, jack->set_xrun_callback, "jack_set_xrun_callback") &&
            bind(library, jack->get_ports, "jack_get_ports") &&
            bind(library, jack->port_by_name, "jack_port_by_name") &&
            bind(library, jack->port_flags, "jack_port_flags") &&
            bind(library, jack->port_name, "jack_port_name") &&
            bind(library, jack->port_register, "jack_port_register") &&
            bind(library, jack->port_unregister, "jack_port_unregister") &&
            bind(library, jack->port_get_buffer, "jack_port_get_buffer") &&
            bind(library, jack->port_get_latency_range, "jack_port_get_latency_range") &&
            bind(library, jack->connect, "jack_connect") && bind(library, jack->free, "jack_free");
        return complete ? std::move(jack) : nullptr;
    }();
    return loaded.get();
}

jack_client_t* openClient(const Api& jack) {
    jack_status_t status{};
    return jack.client_open(kClientName, JackNoStartServer, &status);
}

bool isOurs(const juce::String& clientName) {
    return clientName == kClientName || clientName.startsWith(juce::String(kClientName) + "-");
}

/** @brief Every other client's audio ports with @p flags, as devices. */
std::vector<Device> listDevices(const Api& jack, jack_client_t* client, unsigned long flags) {
    const char** names = jack.get_ports(client, nullptr, JACK_DEFAULT_AUDIO_TYPE, flags);
    if (names == nullptr)
        return {};

    std::vector<Port> ports;
    for (auto** name = names; *name != nullptr; ++name) {
        const auto portName = juce::String::fromUTF8(*name);
        if (isOurs(portName.upToFirstOccurrenceOf(":", false, false)))
            continue;

        auto* port = jack.port_by_name(client, *name);
        ports.push_back(
            {portName, port != nullptr && (jack.port_flags(port) & JackPortIsPhysical) != 0});
    }
    jack.free(names);
    return groupDevices(ports);
}

Device find(const std::vector<Device>& devices, const juce::String& name) {
    for (const auto& device : devices)
        if (device.name == name)
            return device;
    return {};
}

class JackDevice final : public juce::AudioIODevice {
  public:
    JackDevice(const juce::String& inputName, const juce::String& outputName)
        : AudioIODevice(outputName.isEmpty() ? inputName : outputName, "JACK"),
          inputName_(inputName),
          outputName_(outputName),
          jack_(*api()) {
        client_ = openClient(jack_);
        if (client_ == nullptr)
            return;

        // A device's inputs are the JACK ports it sends from, its outputs those it receives on.
        const auto input = find(listDevices(jack_, client_, JackPortIsOutput), inputName_);
        const auto output = find(listDevices(jack_, client_, JackPortIsInput), outputName_);
        sources_ = input.ports;
        destinations_ = output.ports;
        inputChannelNames_ = channelNames(input);
        outputChannelNames_ = channelNames(output);
    }

    ~JackDevice() override {
        close();
        if (client_ != nullptr)
            jack_.client_close(client_);
    }

    juce::StringArray getOutputChannelNames() override {
        return outputChannelNames_;
    }
    juce::StringArray getInputChannelNames() override {
        return inputChannelNames_;
    }

    juce::Array<double> getAvailableSampleRates() override {
        if (client_ == nullptr)
            return {};
        return {static_cast<double>(jack_.get_sample_rate(client_))};
    }

    juce::Array<int> getAvailableBufferSizes() override {
        if (client_ == nullptr)
            return {};
        return {static_cast<int>(jack_.get_buffer_size(client_))};
    }

    int getDefaultBufferSize() override {
        return getCurrentBufferSizeSamples();
    }

    juce::String open(const juce::BigInteger& inputChannels, const juce::BigInteger& outputChannels,
                      double, int) override {
        if (client_ == nullptr) {
            lastError_ = "No JACK server is running";
            return lastError_;
        }

        close();
        lastError_.clear();
        xruns_ = 0;

        // Ports for the opened channels only, so the graph shows what MAGDA uses.
        for (int i = 0; i < sources_.size(); ++i)
            if (inputChannels[i])
                if (auto* port = registerPort("in_" + juce::String(i + 1), JackPortIsInput)) {
                    inputPorts_.push_back(port);
                    activeInputs_.setBit(i);
                }
        for (int i = 0; i < destinations_.size(); ++i)
            if (outputChannels[i])
                if (auto* port = registerPort("out_" + juce::String(i + 1), JackPortIsOutput)) {
                    outputPorts_.push_back(port);
                    activeOutputs_.setBit(i);
                }
        inputBuffers_.resize(inputPorts_.size());
        outputBuffers_.resize(outputPorts_.size());

        jack_.set_process_callback(client_, processCallback, this);
        jack_.set_xrun_callback(client_, xrunCallback, this);
        jack_.on_shutdown(client_, shutdownCallback, this);
        if (jack_.activate(client_) != 0) {
            lastError_ = "Could not activate the JACK client";
            close();
            return lastError_;
        }
        open_ = true;

        auto port = inputPorts_.begin();
        for (int i = 0; i < sources_.size(); ++i)
            if (activeInputs_[i])
                jack_.connect(client_, sources_[i].toRawUTF8(), jack_.port_name(*port++));
        port = outputPorts_.begin();
        for (int i = 0; i < destinations_.size(); ++i)
            if (activeOutputs_[i])
                jack_.connect(client_, jack_.port_name(*port++), destinations_[i].toRawUTF8());

        return lastError_;
    }

    void close() override {
        stop();
        if (client_ != nullptr) {
            if (open_)
                jack_.deactivate(client_);
            for (auto* port : inputPorts_)
                jack_.port_unregister(client_, port);
            for (auto* port : outputPorts_)
                jack_.port_unregister(client_, port);
        }
        inputPorts_.clear();
        outputPorts_.clear();
        activeInputs_.clear();
        activeOutputs_.clear();
        open_ = false;
    }

    bool isOpen() override {
        return open_;
    }

    void start(juce::AudioIODeviceCallback* newCallback) override {
        if (!open_ || newCallback == callback_)
            return;

        if (newCallback != nullptr)
            newCallback->audioDeviceAboutToStart(this);

        auto* oldCallback = callback_;
        {
            const juce::ScopedLock lock(callbackLock_);
            callback_ = newCallback;
        }
        if (oldCallback != nullptr)
            oldCallback->audioDeviceStopped();
    }

    void stop() override {
        start(nullptr);
    }

    bool isPlaying() override {
        return callback_ != nullptr;
    }

    int getCurrentBufferSizeSamples() override {
        return client_ != nullptr ? static_cast<int>(jack_.get_buffer_size(client_)) : 0;
    }
    double getCurrentSampleRate() override {
        return client_ != nullptr ? static_cast<double>(jack_.get_sample_rate(client_)) : 0.0;
    }
    int getCurrentBitDepth() override {
        return 32;
    }
    juce::String getLastError() override {
        return lastError_;
    }
    int getXRunCount() const noexcept override {
        return xruns_.load(std::memory_order_relaxed);
    }

    juce::BigInteger getActiveInputChannels() const override {
        return activeInputs_;
    }
    juce::BigInteger getActiveOutputChannels() const override {
        return activeOutputs_;
    }

    int getInputLatencyInSamples() override {
        return latency(inputPorts_, JackCaptureLatency);
    }
    int getOutputLatencyInSamples() override {
        return latency(outputPorts_, JackPlaybackLatency);
    }

    const juce::String inputName_, outputName_;

  private:
    jack_port_t* registerPort(const juce::String& name, unsigned long flags) {
        return jack_.port_register(client_, name.toRawUTF8(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
    }

    int latency(const std::vector<jack_port_t*>& ports, jack_latency_callback_mode_t mode) {
        jack_nframes_t most = 0;
        for (auto* port : ports) {
            jack_latency_range_t range{};
            jack_.port_get_latency_range(port, mode, &range);
            most = std::max(most, range.max);
        }
        return static_cast<int>(most);
    }

    void process(jack_nframes_t frames) {
        for (size_t i = 0; i < inputPorts_.size(); ++i)
            inputBuffers_[i] = static_cast<float*>(jack_.port_get_buffer(inputPorts_[i], frames));
        for (size_t i = 0; i < outputPorts_.size(); ++i)
            outputBuffers_[i] = static_cast<float*>(jack_.port_get_buffer(outputPorts_[i], frames));

        const auto numSamples = static_cast<int>(frames);
        const juce::ScopedLock lock(callbackLock_);
        if (callback_ != nullptr) {
            callback_->audioDeviceIOCallbackWithContext(
                inputBuffers_.data(), static_cast<int>(inputBuffers_.size()), outputBuffers_.data(),
                static_cast<int>(outputBuffers_.size()), numSamples, {});
            return;
        }
        for (auto* buffer : outputBuffers_)
            juce::FloatVectorOperations::clear(buffer, numSamples);
    }

    static int processCallback(jack_nframes_t frames, void* device) {
        static_cast<JackDevice*>(device)->process(frames);
        return 0;
    }

    static int xrunCallback(void* device) {
        static_cast<JackDevice*>(device)->xruns_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    /// The server is gone, and with it the client, which must not be closed again.
    static void shutdownCallback(void* device) {
        auto* self = static_cast<JackDevice*>(device);
        self->client_ = nullptr;
        self->inputPorts_.clear();
        self->outputPorts_.clear();
        self->close();
    }

    const Api& jack_;
    jack_client_t* client_ = nullptr;
    juce::StringArray sources_, destinations_;
    juce::StringArray inputChannelNames_, outputChannelNames_;
    std::vector<jack_port_t*> inputPorts_, outputPorts_;
    std::vector<const float*> inputBuffers_;
    std::vector<float*> outputBuffers_;
    juce::BigInteger activeInputs_, activeOutputs_;
    bool open_ = false;
    juce::String lastError_;
    std::atomic<int> xruns_{0};
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::CriticalSection callbackLock_;
};

class JackBackend final : public juce::AudioIODeviceType {
  public:
    JackBackend() : AudioIODeviceType("JACK") {}

    void scanForDevices() override {
        inputs_.clear();
        outputs_.clear();
        const auto* jack = api();
        if (jack == nullptr)
            return;
        auto* client = openClient(*jack);
        if (client == nullptr)
            return;

        inputs_ = listDevices(*jack, client, JackPortIsOutput);
        outputs_ = listDevices(*jack, client, JackPortIsInput);
        jack->client_close(client);
    }

    juce::StringArray getDeviceNames(bool wantInputNames) const override {
        juce::StringArray names;
        for (const auto& device : wantInputNames ? inputs_ : outputs_)
            names.add(device.name);
        return names;
    }

    /** @brief The first interface rather than whichever app's client JACK lists first. */
    int getDefaultDeviceIndex(bool forInput) const override {
        const auto& devices = forInput ? inputs_ : outputs_;
        const auto hardware = std::find_if(devices.begin(), devices.end(),
                                           [](const Device& device) { return device.physical; });
        return hardware == devices.end() ? 0 : static_cast<int>(hardware - devices.begin());
    }

    int getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const override {
        auto* jackDevice = dynamic_cast<JackDevice*>(device);
        if (jackDevice == nullptr)
            return -1;
        return getDeviceNames(asInput).indexOf(asInput ? jackDevice->inputName_
                                                       : jackDevice->outputName_);
    }

    bool hasSeparateInputsAndOutputs() const override {
        return true;
    }

    juce::AudioIODevice* createDevice(const juce::String& outputDeviceName,
                                      const juce::String& inputDeviceName) override {
        if (api() == nullptr)
            return nullptr;
        if (!getDeviceNames(true).contains(inputDeviceName) &&
            !getDeviceNames(false).contains(outputDeviceName))
            return nullptr;
        return new JackDevice(inputDeviceName, outputDeviceName);
    }

  private:
    std::vector<Device> inputs_, outputs_;
};

}  // namespace

std::unique_ptr<juce::AudioIODeviceType> createBackend() {
    return std::make_unique<JackBackend>();
}

}  // namespace magda::jack

#else

namespace magda::jack {

std::unique_ptr<juce::AudioIODeviceType> createBackend() {
    return nullptr;
}

}  // namespace magda::jack

#endif

namespace magda::jack {

std::vector<Device> groupDevices(const std::vector<Port>& ports) {
    std::vector<Device> devices;
    for (const auto& port : ports) {
        const auto client = port.name.upToFirstOccurrenceOf(":", false, false);
        auto device = std::find_if(devices.begin(), devices.end(),
                                   [&](const Device& d) { return d.name == client; });
        if (device == devices.end())
            device = devices.insert(devices.end(), Device{client, {}, false});
        device->physical = device->physical || port.physical;
    }

    for (auto& device : devices)
        for (const auto& port : ports)
            if (port.name.upToFirstOccurrenceOf(":", false, false) == device.name &&
                (port.physical || !device.physical))
                device.ports.add(port.name);
    return devices;
}

juce::StringArray channelNames(const Device& device) {
    juce::StringArray names;
    for (int i = 0; i < device.ports.size(); ++i)
        names.add(device.physical ? "channel " + juce::String(i + 1)
                                  : device.ports[i].fromFirstOccurrenceOf(":", false, false));
    return names;
}

void addBackend(juce::AudioDeviceManager& manager) {
    auto backend = createBackend();
    if (backend == nullptr)
        return;

    // JUCE creates the platform's backends only while the manager has none.
    manager.getAvailableDeviceTypes();
    backend->scanForDevices();
    manager.addAudioDeviceType(std::move(backend));
}

}  // namespace magda::jack
