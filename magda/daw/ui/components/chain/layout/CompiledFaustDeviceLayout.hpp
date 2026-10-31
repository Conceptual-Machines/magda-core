// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <map>
#include <span>
#include <utility>
#include <vector>

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
    using ParameterEnabledPredicate = bool (*)(const magda::DeviceInfo&, int slotIndex);

    CompiledFaustDeviceLayout(int cellCount, int cellsPerRow, bool columnMajor = false,
                              ParameterEnabledPredicate isParameterEnabled = nullptr,
                              std::span<const int> knobSlots = {},
                              std::span<const int> excludedSlots = {}, int maxColumns = 0,
                              int leadingColumnCells = 0,
                              std::span<const std::pair<int, const char*>> knobLabels = {},
                              std::span<const char* const> pageNames = {})
        : maxColumns_(maxColumns > 0 ? maxColumns : 2),
          leadingColumnCells_(leadingColumnCells),
          columnMajor_(columnMajor),
          isParameterEnabled_(isParameterEnabled),
          knobSlots_(knobSlots.begin(), knobSlots.end()) {
        for (const auto& [slot, label] : knobLabels)
            labels_[slot] = label;
        for (const auto* name : pageNames)
            pageNames_.add(name);
        // Slots the device shows elsewhere (its mix, its faceplate strip) leave the grid.
        if (knobSlots_.empty() && !excludedSlots.empty())
            for (int slot = 0; slot < cellCount; ++slot)
                knobSlots_.push_back(slot);
        // Paged, an excluded slot leaves a gap so the rows that are pages keep their places.
        for (const int excluded : excludedSlots) {
            if (paged())
                std::ranges::replace(knobSlots_, excluded, -1);
            else
                std::erase(knobSlots_, excluded);
        }
        cellCount_ = paged()              ? maxColumns_
                     : knobSlots_.empty() ? cellCount
                                          : static_cast<int>(knobSlots_.size());
        cellsPerRow_ = paged()              ? maxColumns_
                       : knobSlots_.empty() ? cellsPerRow
                                            : static_cast<int>(knobSlots_.size());
    }

    int cellCount() const override {
        return cellCount_;
    }
    int cellsPerRow() const override {
        return cellsPerRow_;
    }
    bool reflowsForControlStyle() const override {
        return !columnMajor_ && leadingColumnCells_ == 0 && !paged();
    }
    /// A faceplate-below device: its first cells in the left column, the rest over the faceplate.
    bool placesOwnCells() const override {
        return leadingColumnCells_ > 0;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area,
                                    bool faceplateShown) const override;
    static constexpr int kBands = 3;
    int maxColumns() const override {
        return maxColumns_;
    }
    int minRowsForStyle(int styleRows) const override {
        return columnMajor_ ? 0 : styleRows;
    }
    bool wantsPagination() const override {
        return paged();
    }
    bool wantsPageTabs() const override {
        return paged();
    }
    juce::String pageName(const magda::DeviceInfo&, int pageIndex) const override {
        return pageNames_[pageIndex];
    }
    int totalPages(const magda::DeviceInfo& device) const override;
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override;

  private:
    bool paged() const {
        return !pageNames_.isEmpty();
    }

    std::map<int, juce::String> labels_;
    juce::StringArray pageNames_;
    int cellCount_ = 0;
    int cellsPerRow_ = 0;
    int maxColumns_ = 2;
    int leadingColumnCells_ = 0;
    bool columnMajor_;
    ParameterEnabledPredicate isParameterEnabled_;
    std::vector<int> knobSlots_;  // The faceplate-first style's knobs, in order.
};

}  // namespace magda::daw::ui
