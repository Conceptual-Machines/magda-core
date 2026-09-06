#pragma once

#include <atomic>
#include <memory>

#include "core/ChainNodePath.hpp"
#include "core/TypeIds.hpp"
#include "plugins/DeviceSessionKey.hpp"

namespace magda::daw::audio {

class DeviceIdAllocator {
  public:
    virtual ~DeviceIdAllocator() = default;

    virtual DeviceId allocateDeviceId() = 0;
    virtual void ensureDeviceIdAbove(DeviceId id) = 0;
};

class DeviceTrackContext {
  public:
    virtual ~DeviceTrackContext() = default;

    virtual void setDeviceParameterValueFromPlugin(DeviceId deviceId, int paramIndex,
                                                   float value) = 0;
};

enum class DeviceTriggerSource {
    Midi,
    Audio,
};

/**
 * Real-time-safe callbacks supplied by the host for device-side routing.
 *
 * Device packs depend on this interface rather than the DAW's PluginManager.
 * Implementations must remain allocation-free and safe to call from the audio
 * thread.
 */
class DeviceRealtimeContext {
  public:
    virtual ~DeviceRealtimeContext() = default;

    virtual void triggerSidechain(TrackId sourceTrackId, DeviceTriggerSource source) = 0;
    virtual void gateSidechain(TrackId sourceTrackId) = 0;
    virtual void pushFollowerSourceBuffer(TrackId sourceTrackId, const float* mono, int numSamples,
                                          double sampleRate) = 0;
};

class DeviceSessionContext {
  public:
    virtual ~DeviceSessionContext() = default;

    /// Advances host-owned session state from a device's audio callback.
    virtual void processSessionBlock(double transportPositionSeconds) = 0;
};

struct DeviceMeteringTapStorage {
    std::atomic<float> peakL{0.0f};
    std::atomic<float> peakR{0.0f};
    std::atomic<float> gainLinear{1.0f};
};

struct DeviceMeteringTap {
    std::shared_ptr<DeviceMeteringTapStorage> storage;
    std::atomic<float>* peakL = nullptr;
    std::atomic<float>* peakR = nullptr;
    std::atomic<float>* gainLinear = nullptr;

    bool isValid() const {
        return storage != nullptr && peakL != nullptr && peakR != nullptr && gainLinear != nullptr;
    }
};

class DeviceMeteringContext {
  public:
    virtual ~DeviceMeteringContext() = default;

    virtual DeviceMeteringTap getRealtimeTap(const ChainNodePath& devicePath) = 0;
    virtual DeviceMeteringTap getRealtimeTap(DeviceId deviceId) = 0;
};

struct DevicePluginDefaults {
    struct Oscilloscope {
        float timebaseMs = 10.0f;
    } oscilloscope;

    struct Spectrum {
        int fftOrder = 11;
        float slopeDbPerOct = 4.5f;
        float smoothing = 0.5f;
    } spectrum;

    struct MidiReceive {
        TrackId sourceTrackId = INVALID_TRACK_ID;
        bool replaceExistingMidi = false;
    } midiReceive;
};

struct DeviceServices {
    DeviceIdAllocator* deviceIdAllocator = nullptr;
    DeviceTrackContext* trackContext = nullptr;
    DeviceRealtimeContext* realtimeContext = nullptr;
    DeviceSessionContext* sessionContext = nullptr;
    DeviceMeteringContext* meteringContext = nullptr;
    DevicePluginDefaults defaults;
};

void registerDeviceServices(DeviceSessionKey sessionKey, DeviceServices services);
void unregisterDeviceServices(DeviceSessionKey sessionKey);
DeviceServices getDeviceServices(DeviceSessionKey sessionKey);

}  // namespace magda::daw::audio
