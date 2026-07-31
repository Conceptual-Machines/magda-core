#pragma once

#include "../../core/DeviceInfo.hpp"
#include "../../core/ParameterInfo.hpp"
#include "FaustParamSlot.hpp"

namespace magda::daw::audio {

/**
 * @brief Build a `magda::ParameterInfo` from a populated `FaustParamSlot`.
 *
 * The returned ParameterInfo is what `DeviceInfo.parameters` ends up
 * filled from for Faust devices, which in turn drives every
 * `ParamSlotComponent` widget choice (text slider / dropdown / toggle)
 * and the automation lane axis. Mapping rules:
 *
 *   - Kind::Boolean   → ParameterScale::Boolean,
 *                       min=0 / max=1 / default rounded to {0,1}.
 *   - Kind::Trigger   → ParameterScale::Boolean with momentary interaction.
 *   - Kind::Discrete  → ParameterScale::Discrete with `choices` set
 *                       to the slot's menu labels sorted by underlying
 *                       value. min=0 / max=N-1 (UI side); the live
 *                       zone write still uses the original mapping.
 *   - Kind::Continuous + logScale → ParameterScale::Logarithmic.
 *   - Kind::Continuous (linear)   → ParameterScale::Linear.
 *
 * Caller passes the slot they want to bridge; works for inactive
 * slots too (returns an empty-name placeholder so the index slot
 * stays addressable for automation lane lookups).
 */
magda::ParameterInfo paramInfoFromSlot(const FaustParamSlot& slot);

/**
 * @brief Build a `magda::MeterInfo` from a populated `FaustOutputSlot`.
 *
 * The output counterpart to paramInfoFromSlot. Carries the description only,
 * the reading is polled from the pool by the cell, because a value copied into
 * a snapshot is stale by the time it is drawn.
 */
magda::MeterInfo meterInfoFromOutput(const FaustOutputSlot& output);

}  // namespace magda::daw::audio
