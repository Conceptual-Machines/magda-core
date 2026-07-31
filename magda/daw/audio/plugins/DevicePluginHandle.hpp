#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <utility>

#include "plugins/DeviceSessionKey.hpp"

namespace magda::daw::audio {

/**
 * Non-owning reference to a plugin instance hosted by an audio engine.
 */
class DevicePluginRef {
  public:
    constexpr DevicePluginRef() = default;
    constexpr explicit DevicePluginRef(void* plugin) : plugin_(plugin) {}

    constexpr explicit operator bool() const {
        return plugin_ != nullptr;
    }

    constexpr void* nativeHandle() const {
        return plugin_;
    }

  private:
    void* plugin_ = nullptr;
};

/**
 * Copyable, engine-neutral owning handle for a hosted plugin.
 *
 * The engine adapter supplies retain/release operations. This keeps reference
 * counting policy out of the device SDK while allowing current host adapters
 * to preserve their native plugin lifetime exactly.
 */
class DevicePluginPtr {
  public:
    using LifetimeCallback = void (*)(void*);

    DevicePluginPtr() = default;

    DevicePluginPtr(void* plugin, LifetimeCallback retainCallback, LifetimeCallback releaseCallback)
        : plugin_(plugin), retain_(retainCallback), release_(releaseCallback) {
        this->retain();
    }

    DevicePluginPtr(const DevicePluginPtr& other)
        : plugin_(other.plugin_), retain_(other.retain_), release_(other.release_) {
        retain();
    }

    DevicePluginPtr(DevicePluginPtr&& other) noexcept
        : plugin_(std::exchange(other.plugin_, nullptr)),
          retain_(std::exchange(other.retain_, nullptr)),
          release_(std::exchange(other.release_, nullptr)) {}

    DevicePluginPtr& operator=(const DevicePluginPtr& other) {
        if (this == &other)
            return *this;

        DevicePluginPtr copy(other);
        swap(copy);
        return *this;
    }

    DevicePluginPtr& operator=(DevicePluginPtr&& other) noexcept {
        if (this == &other)
            return *this;

        DevicePluginPtr moved(std::move(other));
        swap(moved);
        return *this;
    }

    ~DevicePluginPtr() {
        release();
    }

    explicit operator bool() const {
        return plugin_ != nullptr;
    }

    DevicePluginRef ref() const {
        return DevicePluginRef(plugin_);
    }

    void* nativeHandle() const {
        return plugin_;
    }

    void reset() {
        DevicePluginPtr empty;
        swap(empty);
    }

    void swap(DevicePluginPtr& other) noexcept {
        std::swap(plugin_, other.plugin_);
        std::swap(retain_, other.retain_);
        std::swap(release_, other.release_);
    }

  private:
    void retain() const {
        if (plugin_ != nullptr && retain_ != nullptr)
            retain_(plugin_);
    }

    void release() const {
        if (plugin_ != nullptr && release_ != nullptr)
            release_(plugin_);
    }

    void* plugin_ = nullptr;
    LifetimeCallback retain_ = nullptr;
    LifetimeCallback release_ = nullptr;
};

struct DevicePluginCreationContext {
    DeviceSessionKey sessionKey;
    juce::ValueTree state;
    bool isNewPlugin = false;
};

}  // namespace magda::daw::audio
