#pragma once

#include <memory>
#include <utility>

#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

/// A MagdaDevice over a JUCE-free sdk::Device, one that also builds for the SDK hosts (#2940).
class SdkDeviceAdapter : public MagdaDevice {
  public:
    explicit SdkDeviceAdapter(std::unique_ptr<sdk::Device> device) : device_(std::move(device)) {}

    void setHost(sdk::DeviceHost* host) override {
        MagdaDevice::setHost(host);
        device_->setHost(host);
    }

    DeviceProperties properties() const override {
        return device_->properties();
    }
    void prepare(const DevicePrepareContext& context) override {
        device_->prepare(context);
    }
    void release() override {
        device_->release();
    }
    void reset() override {
        device_->reset();
    }
    int latencySamples() const override {
        return device_->latencySamples();
    }
    std::int64_t tailSamples() const override {
        return device_->tailSamples();
    }
    void process(DeviceProcessContext& context) override {
        device_->process(context);
    }

    int parameterCount() const override {
        return device_->parameterCount();
    }
    bool offersParameter(int slot) const override {
        return device_->offersParameter(slot);
    }
    sdk::ParameterDescriptor parameterDescriptor(int slot) const override {
        return device_->parameterDescriptor(slot);
    }
    float parameterValue(int slot) const override {
        return device_->parameterValue(slot);
    }
    void setParameterValue(int slot, float normalized) override {
        device_->setParameterValue(slot, normalized);
    }
    void setParameterSegments(int slot, std::span<const sdk::ParameterSegment> segments) override {
        device_->setParameterSegments(slot, segments);
    }

    sdk::RestoreResult restoreState(const sdk::StateNode& state) override {
        return device_->restoreState(state);
    }

    DeviceTelemetry* telemetry(std::string_view key) override {
        return device_->telemetry(key);
    }
    const DeviceTelemetry* telemetry(std::string_view key) const override {
        return std::as_const(*device_).telemetry(key);
    }

  private:
    std::unique_ptr<sdk::Device> device_;
};

}  // namespace magda::daw::audio
