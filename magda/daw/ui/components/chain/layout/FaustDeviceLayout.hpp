#pragma once

#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * @brief Device layout for FaustPlugin devices.
 *
 * Faust DSPs declare each control's grid cell explicitly via `[idx:N]`,
 * which lands in `ParameterInfo::paramIndex` (the pool slot index). This
 * layout honours that mapping directly: cell `i` on page `p` displays
 * the param whose pool index is `p * cellCount + i`. Empty pool slots
 * render as truly hidden cells (no widget at all).
 *
 * Gate references (`[gate:N]` / `[gate:!N]`) target pool slot indices,
 * so this layout looks up the gating param by `paramIndex`, not by
 * array position.
 */
class FaustDeviceLayout final : public DeviceParamLayout {
  public:
    static constexpr int kCellsPerRow = 8;
    static constexpr int kRows = 4;
    static constexpr int kCellCount = kCellsPerRow * kRows;

    int cellCount() const override {
        return kCellCount;
    }
    int cellsPerRow() const override {
        return kCellsPerRow;
    }
    bool wantsPagination() const override {
        return true;
    }

    int totalPages(const magda::DeviceInfo& device) const override;
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override;
};

}  // namespace magda::daw::ui
