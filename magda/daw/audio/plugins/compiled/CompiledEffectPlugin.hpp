#pragma once

#include <algorithm>
#include <atomic>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "devices/faust/CompiledEffect.hpp"
#include "plugins/compiled/CompiledFaustInterface.hpp"

namespace magda::daw::audio::compiled {

/**
 * @brief A compiled effect as the app holds it: the JUCE-free effect, plus the slot surface the
 *        parameter list and the device views read (#2940).
 *
 * Both bases are sdk::Devices; every Device call is overridden here, so either path reaches the
 * effect.
 */
template <class Effect>
class CompiledEffectPlugin final : public Effect, public CompiledFaustDevice {
    static_assert(std::is_base_of_v<devices::faust::CompiledEffect, Effect>);

  public:
    using HostSlotInfo = CompiledHostSlotInfo;

    CompiledEffectPlugin() {
        slots_.reserve(static_cast<size_t>(Effect::hostSlotCount()));
        for (int slot = 0; slot < Effect::hostSlotCount(); ++slot) {
            const auto& info = Effect::slotInfo(slot);
            CompiledHostSlotInfo converted{.name = juce::String(info.name),
                                           .unit = juce::String(info.unit),
                                           .scale = info.scale,
                                           .minValue = info.minValue,
                                           .maxValue = info.maxValue,
                                           .defaultValue = info.defaultValue,
                                           .scaleAnchor = info.scaleAnchor,
                                           .gateSlotIndex = info.gateSlotIndex,
                                           .gateNegated = info.gateNegated};
            for (const auto& choice : info.choices)
                converted.choices.emplace_back(choice);
            slots_.push_back(std::move(converted));
        }
    }

    void setHost(sdk::DeviceHost* host) override {
        MagdaDevice::setHost(host);
        Effect::setHost(host);
    }
    DeviceProperties properties() const override {
        return Effect::properties();
    }
    void prepare(const DevicePrepareContext& context) override {
        Effect::prepare(context);
    }
    void release() override {
        Effect::release();
    }
    void reset() override {
        Effect::reset();
    }
    int latencySamples() const override {
        return Effect::latencySamples();
    }
    std::int64_t tailSamples() const override {
        return Effect::tailSamples();
    }
    void process(DeviceProcessContext& context) override {
        Effect::process(context);
    }

    int parameterCount() const override {
        return Effect::parameterCount();
    }
    bool offersParameter(int slot) const override {
        return Effect::offersParameter(slot);
    }
    sdk::ParameterDescriptor parameterDescriptor(int slot) const override {
        return Effect::parameterDescriptor(slot);
    }
    float parameterValue(int slot) const override {
        return Effect::parameterValue(slot);
    }
    void setParameterValue(int slot, float normalized) override {
        Effect::setParameterValue(slot, normalized);
    }
    void setParameterSegments(int slot, std::span<const sdk::ParameterSegment> segments) override {
        Effect::setParameterSegments(slot, segments);
    }
    sdk::RestoreResult restoreState(const sdk::StateNode& state) override {
        return Effect::restoreState(state);
    }
    DeviceTelemetry* telemetry(std::string_view key) override {
        return Effect::telemetry(key);
    }
    const DeviceTelemetry* telemetry(std::string_view key) const override {
        return Effect::telemetry(key);
    }

    int hostSlotCount() const override {
        return Effect::hostSlotCount();
    }
    const CompiledHostSlotInfo& hostSlotInfo(int slot) const override {
        static const CompiledHostSlotInfo kEmpty;
        return slot >= 0 && slot < static_cast<int>(slots_.size())
                   ? slots_[static_cast<size_t>(slot)]
                   : kEmpty;
    }
    DeviceParameterHandle hostSlotParameter(int slot) const override {
        auto* value = Effect::slotValue(slot);
        if (value == nullptr)
            return {};
        return {value, &readValue, &readValue, &writeValue};
    }
    juce::String hostSlotId(int slot) const override {
        return juce::String(Effect::hostSlotId(slot));
    }
    float displayToNormalized(int slot, float displayValue) const override {
        return Effect::displayToNormalized(slot, displayValue);
    }
    float normalizedToDisplay(int slot, float normalizedValue) const override {
        return Effect::normalizedToDisplay(slot, normalizedValue);
    }
    int engineAwareModeSlot() const override {
        return Effect::engineAwareModeSlot();
    }
    int activeEngine() const override {
        return Effect::activeEngine();
    }
    std::vector<juce::String> modeChoicesForActiveEngine() const override {
        std::vector<juce::String> choices;
        for (const auto& choice : Effect::engineModeChoices())
            choices.emplace_back(choice);
        return choices;
    }
    bool isSlotHiddenForActiveEngine(int slot) const override {
        return Effect::isSlotHiddenForActiveEngine(slot);
    }

    /// What the device views read.
    DeviceParameterHandle getSlotParameter(int slot) const {
        return hostSlotParameter(slot);
    }
    const CompiledHostSlotInfo& getSlotInfo(int slot) const {
        return hostSlotInfo(slot);
    }

  private:
    static float readValue(const void* value) {
        return static_cast<const std::atomic<float>*>(value)->load(std::memory_order_relaxed);
    }
    static void writeValue(void* value, float normalized) {
        static_cast<std::atomic<float>*>(value)->store(std::clamp(normalized, 0.0f, 1.0f),
                                                       std::memory_order_relaxed);
    }

    std::vector<CompiledHostSlotInfo> slots_;
};

}  // namespace magda::daw::audio::compiled
