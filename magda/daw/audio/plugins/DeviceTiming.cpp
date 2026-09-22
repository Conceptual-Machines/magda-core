#include "plugins/DeviceTiming.hpp"

#include <atomic>

namespace magda::daw::audio {

namespace {
std::atomic<DeviceTimingSink> installed{nullptr};
}  // namespace

void setDeviceTimingSink(DeviceTimingSink sink) {
    installed.store(sink, std::memory_order_release);
}

DeviceTimingSink deviceTimingSink() {
    return installed.load(std::memory_order_acquire);
}

}  // namespace magda::daw::audio
