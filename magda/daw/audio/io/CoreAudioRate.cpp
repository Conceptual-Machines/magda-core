#include "CoreAudioRate.hpp"

#if defined(__APPLE__)

    #include <CoreAudio/CoreAudio.h>

    #include <chrono>
    #include <cmath>
    #include <condition_variable>
    #include <mutex>
    #include <vector>

namespace magda::coreaudio {
namespace {

AudioObjectPropertyAddress globalAddress(AudioObjectPropertySelector selector) {
    return {selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

template <typename T>
std::vector<T> arrayProperty(AudioObjectID object, const AudioObjectPropertyAddress& address) {
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) != noErr || size == 0)
        return {};

    std::vector<T> values(size / sizeof(T));
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, values.data()) != noErr)
        return {};
    values.resize(size / sizeof(T));
    return values;
}

std::string nameOf(AudioObjectID device) {
    auto address = globalAddress(kAudioObjectPropertyName);
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) != noErr ||
        name == nullptr)
        return {};

    char buffer[512] = {};
    const auto ok = CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(name);
    return ok ? std::string(buffer) : std::string();
}

AudioObjectID deviceNamed(const std::string& interfaceName) {
    if (interfaceName.empty())
        return kAudioObjectUnknown;

    for (const auto device : arrayProperty<AudioObjectID>(
             kAudioObjectSystemObject, globalAddress(kAudioHardwarePropertyDevices)))
        if (nameOf(device) == interfaceName)
            return device;
    return kAudioObjectUnknown;
}

double deviceRate(AudioObjectID device) {
    auto address = globalAddress(kAudioDevicePropertyNominalSampleRate);
    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    return AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &rate) == noErr ? rate
                                                                                           : 0.0;
}

std::vector<AudioObjectID> streamsOf(AudioObjectID device) {
    const AudioObjectPropertyAddress address{kAudioDevicePropertyStreams,
                                             kAudioObjectPropertyScopeWildcard,
                                             kAudioObjectPropertyElementMain};
    return arrayProperty<AudioObjectID>(device, address);
}

bool sameRate(double a, double b) {
    return std::abs(a - b) < 1.0;
}

bool settled(AudioObjectID device, double sampleRate) {
    if (!sameRate(deviceRate(device), sampleRate))
        return false;

    for (const auto stream : streamsOf(device)) {
        auto address = globalAddress(kAudioStreamPropertyPhysicalFormat);
        AudioStreamBasicDescription format{};
        UInt32 size = sizeof(format);
        if (AudioObjectGetPropertyData(stream, &address, 0, nullptr, &size, &format) == noErr &&
            !sameRate(format.mSampleRate, sampleRate))
            return false;
    }
    return true;
}

/// Woken by any of the properties settled() reads, so the wait needs no polling.
struct Wake {
    std::mutex mutex;
    std::condition_variable changed;
    bool pending = false;

    static OSStatus notify(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* self) {
        auto* wake = static_cast<Wake*>(self);
        {
            const std::lock_guard lock(wake->mutex);
            wake->pending = true;
        }
        wake->changed.notify_all();
        return noErr;
    }
};

}  // namespace

double nominalSampleRate(const std::string& interfaceName) {
    const auto device = deviceNamed(interfaceName);
    return device == kAudioObjectUnknown ? 0.0 : deviceRate(device);
}

int settleSampleRate(const std::string& interfaceName, double sampleRate, int timeoutMs) {
    const auto device = deviceNamed(interfaceName);
    if (device == kAudioObjectUnknown || sampleRate <= 0.0)
        return -1;

    Wake wake;
    const auto rateAddress = globalAddress(kAudioDevicePropertyNominalSampleRate);
    const auto formatAddress = globalAddress(kAudioStreamPropertyPhysicalFormat);
    const auto streams = streamsOf(device);
    AudioObjectAddPropertyListener(device, &rateAddress, &Wake::notify, &wake);
    for (const auto stream : streams)
        AudioObjectAddPropertyListener(stream, &formatAddress, &Wake::notify, &wake);

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds(timeoutMs);

    Float64 wanted = sampleRate;
    const auto set = AudioObjectSetPropertyData(device, &rateAddress, 0, nullptr, sizeof(wanted),
                                                &wanted) == noErr;

    auto done = set && settled(device, sampleRate);
    while (set && !done) {
        std::unique_lock lock(wake.mutex);
        if (!wake.changed.wait_until(lock, deadline, [&] { return wake.pending; }))
            break;
        wake.pending = false;
        lock.unlock();
        done = settled(device, sampleRate);
    }

    AudioObjectRemovePropertyListener(device, &rateAddress, &Wake::notify, &wake);
    for (const auto stream : streams)
        AudioObjectRemovePropertyListener(stream, &formatAddress, &Wake::notify, &wake);

    if (!done)
        return -1;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count());
}

}  // namespace magda::coreaudio

#else

namespace magda::coreaudio {

double nominalSampleRate(const std::string&) {
    return 0.0;
}

int settleSampleRate(const std::string&, double, int) {
    return -1;
}

}  // namespace magda::coreaudio

#endif
