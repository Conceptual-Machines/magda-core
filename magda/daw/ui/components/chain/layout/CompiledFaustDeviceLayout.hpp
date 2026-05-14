#pragma once

#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * Compact layout for fixed, compiled Faust effects.
 *
 * Runtime Faust DSPs use a sparse 32-slot pool layout because users can load
 * arbitrary graphs. Compiled MAGDA effects expose curated controls in stable
 * slot order, so they can use a single row and leave room for an inline
 * visualiser below.
 *
 * Cell count + per-row count are constructor args so each compiled device
 * (filter = 5 cells, saturator = 6, …) packs its row tightly without a
 * per-device subclass. Cell-hiding is data-driven: a ParameterInfo can mark
 * itself hidden for runtime cases, and a Discrete cell whose plugin advertises
 * ≤ 1 choice is hidden as functionally inert. That lets plugins like the
 * filter (Ladder engine has only "LP") and Dimension (Rate only applies to
 * the Dimension engine) adjust the grid without layout engine knowledge.
 */
class CompiledFaustDeviceLayout final : public DeviceParamLayout {
  public:
    CompiledFaustDeviceLayout(int cellCount, int cellsPerRow, bool columnMajor = false)
        : cellCount_(cellCount), cellsPerRow_(cellsPerRow), columnMajor_(columnMajor) {}

    int cellCount() const override {
        return cellCount_;
    }
    int cellsPerRow() const override {
        return cellsPerRow_;
    }
    bool wantsPagination() const override {
        return false;
    }
    int totalPages(const magda::DeviceInfo& device) const override;
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override;

  private:
    int cellCount_;
    int cellsPerRow_;
    bool columnMajor_;
};

}  // namespace magda::daw::ui
