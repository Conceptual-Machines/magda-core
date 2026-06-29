#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <vector>

#include "ChainNodePath.hpp"
#include "DeviceInfo.hpp"
#include "ParameterInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * Shared invalidation handle for UI callbacks that may outlive a live plugin
 * binding. Owners invalidate the token before rebuilding or destroying a
 * device UI context; async callbacks and timers can cheaply check it.
 */
class DeviceUiLifetimeToken final {
  public:
    bool isValid() const {
        return valid_;
    }

    void invalidate() {
        valid_ = false;
    }

  private:
    bool valid_ = true;
};

using DeviceUiLifetimeTokenPtr = std::shared_ptr<DeviceUiLifetimeToken>;

class DeviceTelemetrySource {
  public:
    virtual ~DeviceTelemetrySource() = default;

    /**
     * Stable provider key, e.g. "oscilloscope", "spectrum", or "levels".
     * Concrete telemetry interfaces should derive from this base.
     */
    virtual juce::String telemetryKey() const = 0;
};

class DeviceParameterController {
  public:
    virtual ~DeviceParameterController() = default;

    virtual std::vector<ParameterInfo> parameters() const = 0;
    virtual const ParameterInfo* findParameterByIndex(int paramIndex) const = 0;
    virtual void setParameterNormalised(int paramIndex, float value) = 0;
};

class DeviceStateController {
  public:
    virtual ~DeviceStateController() = default;

    virtual juce::var getStateValue(const juce::Identifier& key) const = 0;
    virtual void setStateValue(const juce::Identifier& key, const juce::var& value) = 0;
};

/**
 * Stable surface passed to MAGDA-native device UIs.
 *
 * This is an API-level contract for in-process/static builds. If premium
 * devices become runtime-loaded binary modules, this should be wrapped in a
 * separate ABI-safe facade.
 */
class DeviceUiContext {
  public:
    virtual ~DeviceUiContext() = default;

    virtual ChainNodePath path() const = 0;
    virtual DeviceId deviceId() const = 0;
    virtual bool isValid() const = 0;
    virtual DeviceUiLifetimeTokenPtr lifetimeToken() const = 0;

    virtual DeviceParameterController* parameters() const = 0;
    virtual DeviceStateController* state() const = 0;
    virtual DeviceTelemetrySource* telemetry(const juce::String& key) const = 0;
};

class BasicDeviceUiContext final : public DeviceUiContext {
  public:
    BasicDeviceUiContext(DeviceInfo device, ChainNodePath path = {})
        : device_(std::move(device)),
          path_(std::move(path)),
          lifetimeToken_(std::make_shared<DeviceUiLifetimeToken>()) {}

    ~BasicDeviceUiContext() override {
        invalidate();
    }

    ChainNodePath path() const override {
        return path_;
    }

    DeviceId deviceId() const override {
        return path_.getDeviceId() != INVALID_DEVICE_ID ? path_.getDeviceId() : device_.id;
    }

    bool isValid() const override {
        return lifetimeToken_ != nullptr && lifetimeToken_->isValid() && path_.isValid() &&
               deviceId() != INVALID_DEVICE_ID;
    }

    DeviceUiLifetimeTokenPtr lifetimeToken() const override {
        return lifetimeToken_;
    }

    DeviceParameterController* parameters() const override {
        return parameterController_.get();
    }

    DeviceStateController* state() const override {
        return stateController_.get();
    }

    DeviceTelemetrySource* telemetry(const juce::String& key) const override {
        auto it = telemetrySources_.find(key);
        return it != telemetrySources_.end() ? it->second.get() : nullptr;
    }

    const DeviceInfo& device() const {
        return device_;
    }

    void setDevice(DeviceInfo device) {
        device_ = std::move(device);
    }

    void setPath(ChainNodePath path) {
        path_ = std::move(path);
    }

    void setParameterController(std::shared_ptr<DeviceParameterController> controller) {
        parameterController_ = std::move(controller);
    }

    void setStateController(std::shared_ptr<DeviceStateController> controller) {
        stateController_ = std::move(controller);
    }

    void setTelemetrySource(std::shared_ptr<DeviceTelemetrySource> source) {
        if (source == nullptr)
            return;
        telemetrySources_[source->telemetryKey()] = std::move(source);
    }

    void invalidate() {
        if (lifetimeToken_ != nullptr)
            lifetimeToken_->invalidate();
    }

  private:
    DeviceInfo device_;
    ChainNodePath path_;
    DeviceUiLifetimeTokenPtr lifetimeToken_;
    std::shared_ptr<DeviceParameterController> parameterController_;
    std::shared_ptr<DeviceStateController> stateController_;
    std::map<juce::String, std::shared_ptr<DeviceTelemetrySource>> telemetrySources_;
};

}  // namespace magda
