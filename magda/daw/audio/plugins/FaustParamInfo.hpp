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
 * @brief The runtime Faust instrument's two host-owned parameters.
 *
 * Voice Mode and Glide belong to the device rather than to any patch: they
 * drive voice allocation, which is the host's job, so every runtime Faust
 * instrument has them whether or not its .dsp knows anything about them.
 *
 * They deliberately sit past the end of the `[idx:N]` pool rather than in
 * reserved slots inside it. A patch is free to use all 64 slots, and a host
 * control squatting on two of them would silently shadow an author's controls.
 *
 * @param hostIndex 0 for Voice Mode, 1 for Glide.
 */
magda::ParameterInfo faustInstrumentHostParamInfo(int hostIndex);

/**
 * @brief Build a `magda::MeterInfo` from a populated `FaustOutputSlot`.
 *
 * The output counterpart to paramInfoFromSlot. Carries the description only,
 * the reading is polled from the pool by the cell, because a value copied into
 * a snapshot is stale by the time it is drawn.
 */
magda::MeterInfo meterInfoFromOutput(const FaustOutputSlot& output);

}  // namespace magda::daw::audio
