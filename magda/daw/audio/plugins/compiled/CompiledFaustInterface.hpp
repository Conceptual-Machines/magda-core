#pragma once

#include <atomic>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "plugins/DeviceParameterHandle.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio::compiled {

/**
 * @brief Cross-plugin slot description. Shared between every compiled-Faust
 *        plugin (filter, saturator, delay, …) so the host processor and
 *        layout layers can iterate slots without knowing which concrete
 *        plugin they're talking to.
 */
struct CompiledHostSlotInfo {
    juce::String name;
    juce::String unit;
    magda::ParameterScale scale = magda::ParameterScale::Linear;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float scaleAnchor = std::numeric_limits<float>::quiet_NaN();
    std::vector<juce::String> choices;  // for Discrete kind

    // Gate condition harvested from `[gate:N]` / `[gate:!N]` annotations on
    // the Faust slider label. Layout reads these via ParameterInfo to grey
    // out cells whose enable condition is not met (e.g. delay's Time greys
    // when Sync is on). -1 = no gate, always enabled.
    int gateSlotIndex = -1;
    bool gateNegated = false;
};

/**
 * @brief Minimal interface every compiled-Faust plugin in MAGDA implements
 *        so CompiledFaustProcessor can ask for slot count / info / params
 *        without dynamic-casting per plugin type.
 *
 * The "engine-aware" hooks default to a single-engine plugin (one engine,
 * no per-engine mode list). MagdaFilterCompiledPlugin overrides them to
 * advertise its five filter families and per-engine mode sets.
 *
 * Lives in its own header so plugin classes can include it without pulling
 * in the Faust SDK headers (UI.h / dsp.h) that the harvest helpers in
 * CompiledFaustHost.hpp need but UI-side translation units don't.
 */
class ICompiledFaustPlugin {
  public:
    virtual ~ICompiledFaustPlugin() = default;

    virtual int hostSlotCount() const = 0;
    virtual const CompiledHostSlotInfo& hostSlotInfo(int slotIndex) const = 0;
    virtual DeviceParameterHandle hostSlotParameter(int slotIndex) const = 0;
    virtual juce::String hostSlotId(int) const {
        return {};
    }
    virtual float displayToNormalized(int slotIndex, float displayValue) const = 0;
    virtual float normalizedToDisplay(int slotIndex, float normalizedValue) const = 0;

    // Single-engine plugins return -1 / empty so the processor can skip
    // engine-aware mode-list rebuilding without special-casing.
    virtual int engineAwareModeSlot() const {
        return -1;
    }
    virtual int activeEngine() const {
        return 0;
    }
    virtual std::vector<juce::String> modeChoicesForActiveEngine() const {
        return {};
    }
    virtual bool isSlotHiddenForActiveEngine(int) const {
        return false;
    }
};

/**
 * Normalized parameter storage owned by a neutral compiled-Faust device.
 */
class CompiledParameterValue {
  public:
    CompiledParameterValue() = default;
    explicit CompiledParameterValue(float value) : value_(value) {}

    CompiledParameterValue(const CompiledParameterValue& other) : value_(other.getCurrentValue()) {}

    CompiledParameterValue& operator=(const CompiledParameterValue& other) {
        setCurrentValue(other.getCurrentValue());
        return *this;
    }

    float getCurrentValue() const {
        return value_.load(std::memory_order_relaxed);
    }

    void setCurrentValue(float value) {
        value_.store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_relaxed);
    }

    DeviceParameterHandle handle() {
        return {
            this,
            [](const void* native) {
                return static_cast<const CompiledParameterValue*>(native)->getCurrentValue();
            },
            [](const void* native) {
                return static_cast<const CompiledParameterValue*>(native)->getCurrentValue();
            },
            [](void* native, float value) {
                static_cast<CompiledParameterValue*>(native)->setCurrentValue(value);
            },
        };
    }

    DeviceParameterHandle handle() const {
        return const_cast<CompiledParameterValue*>(this)->handle();
    }

  private:
    std::atomic<float> value_{0.0f};
};

/**
 * Shared neutral parameter implementation for compiled-Faust devices.
 */
class CompiledFaustDevice : public MagdaDevice, public ICompiledFaustPlugin {
  public:
    int parameterCount() const override {
        return hostSlotCount();
    }

    ParameterInfo parameterInfo(int slotIndex) const override {
        if (slotIndex < 0 || slotIndex >= hostSlotCount())
            return {};

        const auto& slot = hostSlotInfo(slotIndex);
        ParameterInfo info;
        info.paramIndex = slotIndex;
        info.stableId = hostSlotId(slotIndex);
        info.name = slot.name;
        info.unit = slot.unit;
        info.scale = slot.scale;
        info.minValue = slot.minValue;
        info.maxValue = slot.maxValue;
        info.defaultValue = slot.defaultValue;
        info.scaleAnchor = std::isfinite(slot.scaleAnchor) ? slot.scaleAnchor : 0.0f;
        info.choices = slot.choices;
        info.gateSlotIndex = slot.gateSlotIndex;
        info.gateNegated = slot.gateNegated;
        return info;
    }

    float parameterValue(int slotIndex) const override {
        return hostSlotParameter(slotIndex).currentValue();
    }

    void setParameterValue(int slotIndex, float value) override {
        hostSlotParameter(slotIndex).setValueFromHost(value);
    }
};

}  // namespace magda::daw::audio::compiled
