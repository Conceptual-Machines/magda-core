#pragma once

#include <cstdint>
#include <vector>

#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * @brief Device layout for FaustPlugin devices.
 *
 * Faust author groups define named pages. Controls are packed into each page
 * in pool order, while `[idx:N]` remains the stable parameter identity used
 * for automation, modulation, MIDI Learn, and gate references.
 *
 * Gate references (`[gate:N]` / `[gate:!N]`) target pool slot indices,
 * so this layout looks up the gating param by `paramIndex`, not by array
 * position.
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
    bool wantsPageTabs() const override {
        return true;
    }

    int totalPages(const magda::DeviceInfo& device) const override;
    juce::String pageName(const magda::DeviceInfo& device, int pageIndex) const override;
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override;
    int pageForParameter(const magda::DeviceInfo& device, int paramIndex) const override;

  private:
    struct GroupPage {
        juce::String name;
        std::vector<int> paramArrayIndices;
        int chunkIndex = 0;
        int chunkCount = 1;
    };

    const std::vector<GroupPage>& pagesFor(const magda::DeviceInfo& device) const;

    mutable std::uint64_t cachedSignature_ = 0;
    mutable bool cacheValid_ = false;
    mutable std::vector<GroupPage> cachedPages_;
};

}  // namespace magda::daw::ui
