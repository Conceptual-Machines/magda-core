#include "slot/DeviceSlotParamLayoutFactory.hpp"

#include "layout/CompiledFaustDeviceLayout.hpp"
#include "layout/FaustDeviceLayout.hpp"
#include "layout/StandardDeviceLayout.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

std::unique_ptr<DeviceParamLayout> createDeviceSlotParamLayout(const DeviceSlotTraits& traits) {
    if (traits.isFaust || traits.isFaustInstrument) {
        const auto pageMode = traits.isFaustInstrument ? FaustDeviceLayout::PageMode::Groups
                                                       : FaustDeviceLayout::PageMode::PoolSlots;
        return std::make_unique<FaustDeviceLayout>(pageMode);
    }

    if (traits.compiledPresentation != nullptr) {
        return std::make_unique<CompiledFaustDeviceLayout>(
            traits.compiledPresentation->layoutCellCount,
            traits.compiledPresentation->layoutCellsPerRow,
            traits.compiledPresentation->columnMajorGrid,
            traits.compiledPresentation->isParameterEnabled);
    }

    return std::make_unique<StandardDeviceLayout>();
}

}  // namespace magda::daw::ui
