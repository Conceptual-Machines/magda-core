#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <limits>
#include <string>
#include <vector>

#include "core/ParameterInfo.hpp"

namespace magda::daw::audio::compiled {

namespace te = tracktion::engine;

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
    virtual te::AutomatableParameter* hostSlotParameter(int slotIndex) const = 0;
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

}  // namespace magda::daw::audio::compiled
