#pragma once

#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * @brief The default 8x4 grid layout: device.parameters laid out
 *        contiguously in encounter order, with `[gate:N]` resolving
 *        against the same array index.
 *
 * Honours `device.visibleParameters` (user-side parameter filtering).
 * Used by every device that doesn't override with a custom layout.
 */
class StandardDeviceLayout final : public DeviceParamLayout {
  public:
    static constexpr int kCellsPerRow = 8;
    static constexpr int kRows = 4;
    static constexpr int kCellCount = kCellsPerRow * kRows;

    int cellCount() const override {
        return cellsPerRow_ * rows_;
    }
    int cellsPerRow() const override {
        return cellsPerRow_;
    }
    int maxCellCount() const override {
        return kCellCount;
    }
    bool setShape(int cellsPerRow, int rows) override {
        cellsPerRow_ = juce::jlimit(1, kCellsPerRow, cellsPerRow);
        rows_ = juce::jlimit(1, kRows, rows);
        return true;
    }
    bool wantsPagination() const override {
        return true;
    }

    int totalPages(const magda::DeviceInfo& device) const override;
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override;

  private:
    int cellsPerRow_ = kCellsPerRow;
    int rows_ = kRows;
};

}  // namespace magda::daw::ui
