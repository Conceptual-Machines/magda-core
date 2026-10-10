#include "slot/DeviceSlotParamLayoutFactory.hpp"

#include <vector>

#include "layout/CompiledFaustDeviceLayout.hpp"
#include "layout/FaustDeviceLayout.hpp"
#include "layout/StandardDeviceLayout.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

std::unique_ptr<DeviceParamLayout> createDeviceSlotParamLayout(const DeviceSlotTraits& traits,
                                                               std::span<const int> excludedSlots) {
    if (traits.isFaust || traits.isFaustInstrument) {
        return std::make_unique<FaustDeviceLayout>();
    }

    if (const auto* spec = traits.compiledPresentation; spec != nullptr && spec->createLayout)
        return spec->createLayout();

    if (const auto* spec = traits.compiledPresentation; spec != nullptr) {
        // A faceplate-below device lists its column knobs, then its band knobs.
        std::vector<int> knobs(spec->knobSlots.begin(), spec->knobSlots.end());
        knobs.insert(knobs.end(), spec->bandSlots.begin(), spec->bandSlots.end());
        const int leading = spec->bandSlots.empty() ? 0 : static_cast<int>(spec->knobSlots.size());
        return std::make_unique<CompiledFaustDeviceLayout>(
            spec->layoutCellCount, spec->layoutCellsPerRow, spec->columnMajorGrid,
            spec->isParameterEnabled, knobs, excludedSlots, spec->knobColumns, leading);
    }

    return std::make_unique<StandardDeviceLayout>();
}

}  // namespace magda::daw::ui
