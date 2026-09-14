#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "audio/plugins/AnalysisTelemetry.hpp"
#include "audio/plugins/MagdaDevice.hpp"
#include "custom_ui/TelemetrySources.hpp"

/**
 * @file DeviceTelemetrySources.hpp
 * @brief Telemetry sources over the device the engine is rendering (#2585).
 *
 * The other implementation of the seam #1630 left: the same faceplates, fed by
 * whichever engine holds the instance that filled the ring rather than by a
 * plugin pointer only the fork can hand over.
 *
 * Every read resolves the device again rather than holding it, which is what
 * makes a rebuilt device (#2575) a rebind and a removed one an empty trace
 * instead of a read through a freed instance. The resolution is a lookup, and
 * the lease it answers with is held for the duration of the call.
 */

namespace magda::daw::ui {

/// How a source reaches the device rendering behind its slot, asked on every
/// read. Null or empty means nothing renders there now.
using RenderedDeviceQuery = std::function<std::shared_ptr<magda::daw::audio::MagdaDevice>()>;

namespace detail {

/// The device's telemetry surface for @p key, holding the device open with it.
template <typename Telemetry>
std::shared_ptr<Telemetry> telemetryFrom(const RenderedDeviceQuery& query) {
    auto device = query ? query() : nullptr;
    if (device == nullptr)
        return {};

    auto* surface = dynamic_cast<Telemetry*>(device->telemetry(Telemetry::kKey));
    return surface != nullptr ? std::shared_ptr<Telemetry>{std::move(device), surface}
                              : std::shared_ptr<Telemetry>{};
}

}  // namespace detail

/** @brief The oscilloscope faceplate's trace, off the device drawing it. */
class DeviceOscilloscopeTelemetry final : public OscilloscopeTelemetrySource {
  public:
    explicit DeviceOscilloscopeTelemetry(RenderedDeviceQuery device) : device_(std::move(device)) {}

    size_t writePosition() const override {
        auto surface = tap();
        return surface != nullptr ? surface->writePosition() : 0;
    }

    size_t readLatest(float* dest, int numSamples) const override {
        auto surface = tap();
        return surface != nullptr ? surface->readLatest(dest, numSamples) : 0;
    }

    double sampleRate() const override {
        auto surface = tap();
        return surface != nullptr ? surface->sampleRate() : 44100.0;
    }

    int traceColourIndex() const override {
        auto surface = tap();
        return surface != nullptr ? surface->traceColourIndex() : 0;
    }

    void setTraceColourIndex(int index) override {
        if (auto surface = tap())
            surface->setTraceColourIndex(index);
    }

    float timebaseMs() const override {
        auto surface = tap();
        return surface != nullptr ? surface->timebaseMs() : 10.0f;
    }

    void setTimebaseMs(float ms) override {
        if (auto surface = tap())
            surface->setTimebaseMs(ms);
    }

  private:
    std::shared_ptr<audio::OscilloscopeTelemetry> tap() const {
        return detail::telemetryFrom<audio::OscilloscopeTelemetry>(device_);
    }

    RenderedDeviceQuery device_;
};

/** @brief The spectrum faceplate's frame, off the device measuring it. */
class DeviceSpectrumTelemetry final : public SpectrumTelemetrySource {
  public:
    explicit DeviceSpectrumTelemetry(RenderedDeviceQuery device) : device_(std::move(device)) {}

    size_t writePosition() const override {
        auto surface = tap();
        return surface != nullptr ? surface->writePosition() : 0;
    }

    size_t readLatest(float* dest, int numSamples) const override {
        auto surface = tap();
        return surface != nullptr ? surface->readLatest(dest, numSamples) : 0;
    }

    double sampleRate() const override {
        auto surface = tap();
        return surface != nullptr ? surface->sampleRate() : 44100.0;
    }

    int traceColourIndex() const override {
        auto surface = tap();
        return surface != nullptr ? surface->traceColourIndex() : 0;
    }

    void setTraceColourIndex(int index) override {
        if (auto surface = tap())
            surface->setTraceColourIndex(index);
    }

    int fftOrder() const override {
        auto surface = tap();
        return surface != nullptr ? surface->fftOrder() : 11;
    }

    void setFftOrder(int order) override {
        if (auto surface = tap())
            surface->setFftOrder(order);
    }

    float slopeDbPerOct() const override {
        auto surface = tap();
        return surface != nullptr ? surface->slopeDbPerOct() : 4.5f;
    }

    void setSlopeDbPerOct(float slope) override {
        if (auto surface = tap())
            surface->setSlopeDbPerOct(slope);
    }

    float smoothing() const override {
        auto surface = tap();
        return surface != nullptr ? surface->smoothing() : 0.5f;
    }

    void setSmoothing(float smoothing) override {
        if (auto surface = tap())
            surface->setSmoothing(smoothing);
    }

  private:
    std::shared_ptr<audio::SpectrumTelemetry> tap() const {
        return detail::telemetryFrom<audio::SpectrumTelemetry>(device_);
    }

    RenderedDeviceQuery device_;
};

/** @brief Nimbus's grain-buffer view, off the device filling it. */
class DeviceNimbusTelemetry final : public NimbusTelemetrySource {
  public:
    explicit DeviceNimbusTelemetry(RenderedDeviceQuery device) : device_(std::move(device)) {}

    size_t inputEnvelopeWritePosition() const override {
        auto ring = envelope();
        return ring != nullptr ? ring->writePosition() : 0;
    }

    size_t readInputEnvelope(float* dest, int numSamples) const override {
        auto ring = envelope();
        return ring != nullptr ? ring->readLatest(dest, numSamples) : 0;
    }

  private:
    std::shared_ptr<audio::GrainEnvelopeTelemetry> envelope() const {
        return detail::telemetryFrom<audio::GrainEnvelopeTelemetry>(device_);
    }

    RenderedDeviceQuery device_;
};

}  // namespace magda::daw::ui
