#pragma once

#include "DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * Compact layout for fixed, compiled Faust effects.
 *
 * Runtime Faust DSPs use a sparse 32-slot pool layout because users can load
 * arbitrary graphs. The compiled MAGDA effects currently expose curated
 * controls in stable slot order, so they can use a single row and leave room
 * for an inline visualiser below.
 */
class CompiledFaustDeviceLayout final : public DeviceParamLayout {
  public:
    static constexpr int kCellCount = 4;
    static constexpr int kCellsPerRow = 4;

    int cellCount() const override {
        return kCellCount;
    }
    int cellsPerRow() const override {
        return kCellsPerRow;
    }
    bool wantsPagination() const override {
        return false;
    }
    int totalPages(const magda::DeviceInfo& device) const override;
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override;
};

}  // namespace magda::daw::ui
