#pragma once

#include <cstddef>
#include <functional>

namespace magda::daw::audio {

/**
 * Opaque identity for the host session that owns a device.
 *
 * The key is deliberately non-owning. Hosts must keep the referenced session
 * alive while services or device creation callbacks use the key.
 */
class DeviceSessionKey {
  public:
    constexpr DeviceSessionKey() = default;

    static constexpr DeviceSessionKey fromAddress(const void* address) {
        return DeviceSessionKey(address);
    }

    constexpr explicit operator bool() const {
        return address_ != nullptr;
    }

    friend constexpr bool operator==(DeviceSessionKey, DeviceSessionKey) = default;

    constexpr const void* address() const {
        return address_;
    }

  private:
    constexpr explicit DeviceSessionKey(const void* address) : address_(address) {}

    const void* address_ = nullptr;
};

struct DeviceSessionKeyLess {
    bool operator()(DeviceSessionKey lhs, DeviceSessionKey rhs) const {
        return std::less<const void*>{}(lhs.address(), rhs.address());
    }
};

}  // namespace magda::daw::audio
