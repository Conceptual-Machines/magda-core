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
std::map<DeviceSessionKey, ServiceEntry, DeviceSessionKeyLess> servicesBySession;

DeviceServices resolve(ServiceEntry& entry) {
    auto services = entry.services;
    if (services.deviceIdAllocator == nullptr)
        services.deviceIdAllocator = &entry.fallbackAllocator;
    return services;
}

}  // namespace

void registerDeviceServices(DeviceSessionKey sessionKey, DeviceServices services) {
    std::scoped_lock lock(servicesMutex);
    servicesBySession[sessionKey].services = services;
}

void unregisterDeviceServices(DeviceSessionKey sessionKey) {
    std::scoped_lock lock(servicesMutex);
    servicesBySession.erase(sessionKey);
}

DeviceServices getDeviceServices(DeviceSessionKey sessionKey) {
    std::scoped_lock lock(servicesMutex);
    if (const auto found = servicesBySession.find(sessionKey); found != servicesBySession.end())
        return resolve(found->second);

    static ServiceEntry fallback;
    return resolve(fallback);
}

}  // namespace magda::daw::audio
