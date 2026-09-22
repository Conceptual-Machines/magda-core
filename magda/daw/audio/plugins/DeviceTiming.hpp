#pragma once

#include <chrono>

/**
 * @file DeviceTiming.hpp
 * @brief Where a host reports how long a device took, when something is measuring.
 *
 * Both engines' adapters time their devices through here, so one profile compares the same
 * device under each host (#2786). Nothing is installed outside a measuring tool, and then a
 * scope costs one load and one branch.
 */

namespace magda::daw::audio {

using DeviceTimingSink = void (*)(const char* name, std::chrono::steady_clock::duration elapsed);

/// Install @p sink, or null to stop measuring. Before any device renders.
void setDeviceTimingSink(DeviceTimingSink sink);

/// The installed sink, or null.
DeviceTimingSink deviceTimingSink();

/// Reports the time from construction to destruction under @p name, when a sink is installed.
class DeviceTimingScope {
  public:
    explicit DeviceTimingScope(const char* name)
        : name_(name),
          sink_(deviceTimingSink()),
          started_(sink_ != nullptr ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{}) {}

    ~DeviceTimingScope() {
        if (sink_ != nullptr)
            sink_(name_, std::chrono::steady_clock::now() - started_);
    }

    DeviceTimingScope(const DeviceTimingScope&) = delete;
    DeviceTimingScope& operator=(const DeviceTimingScope&) = delete;

  private:
    const char* name_;
    DeviceTimingSink sink_;
    std::chrono::steady_clock::time_point started_;
};

}  // namespace magda::daw::audio
