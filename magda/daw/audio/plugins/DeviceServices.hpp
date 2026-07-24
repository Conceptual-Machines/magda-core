#pragma once

#include <atomic>
#include <memory>

#include "core/ChainNodePath.hpp"
#include "core/TypeIds.hpp"

namespace tracktion {
inline namespace engine {
class Edit;
}
}  // namespace tracktion

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

    virtual bool isChordTrackMuted() const = 0;
    virtual void setDeviceParameterValueFromPlugin(DeviceId deviceId, int paramIndex,
                                                   float value) = 0;
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
    DeviceMeteringContext* meteringContext = nullptr;
    DevicePluginDefaults defaults;
};

void registerDeviceServices(tracktion::engine::Edit& edit, DeviceServices services);
void unregisterDeviceServices(tracktion::engine::Edit& edit);
DeviceServices getDeviceServices(tracktion::engine::Edit& edit);

}  // namespace magda::daw::audio
