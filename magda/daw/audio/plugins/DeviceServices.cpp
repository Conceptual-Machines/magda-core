#include "plugins/DeviceServices.hpp"

#include <algorithm>
#include <map>
#include <mutex>

namespace magda::daw::audio {

namespace {

class FallbackDeviceIdAllocator final : public DeviceIdAllocator {
  public:
    DeviceId allocateDeviceId() override {
        return nextId_++;
    }

    void ensureDeviceIdAbove(DeviceId id) override {
        nextId_ = std::max(nextId_, id + 1);
    }

  private:
    DeviceId nextId_ = 1;
};

struct ServiceEntry {
    DeviceServices services;
    FallbackDeviceIdAllocator fallbackAllocator;
};

std::mutex servicesMutex;
std::map<tracktion::engine::Edit*, ServiceEntry> servicesByEdit;

DeviceServices resolve(ServiceEntry& entry) {
    auto services = entry.services;
    if (services.deviceIdAllocator == nullptr)
        services.deviceIdAllocator = &entry.fallbackAllocator;
    return services;
}

}  // namespace

void registerDeviceServices(tracktion::engine::Edit& edit, DeviceServices services) {
    std::scoped_lock lock(servicesMutex);
    servicesByEdit[&edit].services = services;
}

void unregisterDeviceServices(tracktion::engine::Edit& edit) {
    std::scoped_lock lock(servicesMutex);
    servicesByEdit.erase(&edit);
}

DeviceServices getDeviceServices(tracktion::engine::Edit& edit) {
    std::scoped_lock lock(servicesMutex);
    return resolve(servicesByEdit[&edit]);
}

}  // namespace magda::daw::audio
