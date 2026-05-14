#include "slot/DeviceSlotParamLayoutFactory.hpp"

#include "layout/CompiledFaustDeviceLayout.hpp"
#include "layout/FaustDeviceLayout.hpp"
#include "layout/StandardDeviceLayout.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

std::unique_ptr<DeviceParamLayout> createDeviceSlotParamLayout(const DeviceSlotTraits& traits) {
    if (traits.isFaust)
        return std::make_unique<FaustDeviceLayout>();

    if (traits.compiledPresentation != nullptr) {
        return std::make_unique<CompiledFaustDeviceLayout>(
            traits.compiledPresentation->layoutCellCount,
            traits.compiledPresentation->layoutCellsPerRow,
            traits.compiledPresentation->columnMajorGrid);
    }

    return std::make_unique<StandardDeviceLayout>();
}

}  // namespace magda::daw::ui
