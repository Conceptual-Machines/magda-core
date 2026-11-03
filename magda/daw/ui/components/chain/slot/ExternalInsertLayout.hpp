// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "core/InsertCcKnobs.hpp"
#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * @brief The External Insert's body: the routing row, the MIDI CC row, a page of CC knobs, and the
 *        status line at the foot (#3164).
 *
 * At the chain's 172 px body the knobs get 84 px (#3212); more height grows them to the largest
 * knob, then stands them centred. The faceplate and the param grid both place from this, over the
 * same bounds.
 */
struct ExternalInsertGeometry {
    juce::Rectangle<int> routing;
    juce::Rectangle<int> ccHeader;
    std::array<juce::Rectangle<int>, insert_cc::kKnobsPerPage> cells;
    juce::Rectangle<int> status;

    static constexpr int kSide = 8;
    static constexpr int kGap = 4;
    static constexpr int kGroupGap = 12;
    static constexpr int kRowHeight = 22;
    static constexpr int kStatusHeight = 20;
    /// ParamSlotComponent's largest knob, with its name and value.
    static constexpr int kMaxCellHeight = 98;

    /// Wide enough for "MIDI CC", which shares the send caption's column.
    static constexpr int kSendCaptionWidth = 58;
    static constexpr int kReturnCaptionWidth = 78;
    static constexpr int kLatencyCaptionWidth = 56;
    static constexpr int kLatencyFieldWidth = 72;
    static constexpr int kChannelWidth = 56;

    /** @brief Each routing picker's width in a row of @p rowWidth, with a channel picker or not. */
    static int pickerWidth(int rowWidth, bool withChannel) {
        const int fixed = kSendCaptionWidth + kReturnCaptionWidth + kLatencyCaptionWidth +
                          kLatencyFieldWidth + 2 * kGroupGap +
                          (withChannel ? kChannelWidth + kGap : 0);
        return (rowWidth - fixed) / 2;
    }

    static ExternalInsertGeometry of(juce::Rectangle<int> bounds) {
        ExternalInsertGeometry g;
        auto area = bounds.reduced(kSide, 0).withTrimmedTop(kGap).withTrimmedBottom(kGap);
        g.status = area.removeFromBottom(kStatusHeight);
        area.removeFromBottom(kGap);

        g.routing = area.removeFromTop(kRowHeight);
        area.removeFromTop(2 * kGap);
        g.ccHeader = area.removeFromTop(kRowHeight);
        area.removeFromTop(kGap);

        auto knobRow = area.withSizeKeepingCentre(area.getWidth(),
                                                  juce::jmin(kMaxCellHeight, area.getHeight()));
        const int count = insert_cc::kKnobsPerPage;
        const int width = (knobRow.getWidth() - (count - 1) * kGap) / count;
        for (auto& cell : g.cells) {
            cell = knobRow.removeFromLeft(width);
            knobRow.removeFromLeft(kGap);
        }
        return g;
    }
};

/// A page of the insert's CC knobs, in the order they were added. The page after a full one holds
/// only the add tile, so it is counted too.
class ExternalInsertLayout final : public DeviceParamLayout {
  public:
    int cellCount() const override {
        return insert_cc::kKnobsPerPage;
    }
    int cellsPerRow() const override {
        return insert_cc::kKnobsPerPage;
    }
    bool wantsPagination() const override {
        return true;
    }
    int totalPages(const magda::DeviceInfo& device) const override {
        return pagesFor(static_cast<int>(device.insert.ccSlots.size()));
    }
    bool placesOwnCells() const override {
        return true;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area, bool) const override {
        return ExternalInsertGeometry::of(area).cells[static_cast<size_t>(cell)];
    }

    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex, int page) const override {
        ParamCell cell;
        const auto& slots = device.insert.ccSlots;
        const auto knob = static_cast<size_t>(page * insert_cc::kKnobsPerPage + cellIndex);
        if (cellIndex < 0 || cellIndex >= cellCount() || knob >= slots.size())
            return cell;

        const int paramIndex = slots[knob].paramIndex;
        for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
            if (device.parameters[static_cast<size_t>(k)].paramIndex != paramIndex)
                continue;
            cell.mode = ParamCell::Mode::Filled;
            cell.paramArrayIndex = k;
            cell.targetParamIndex = paramIndex;
            return cell;
        }
        return cell;
    }

    static int pagesFor(int knobs) {
        const int withAddTile = juce::jmin(knobs + 1, insert_cc::kMaxKnobs);
        return juce::jmax(1,
                          (withAddTile + insert_cc::kKnobsPerPage - 1) / insert_cc::kKnobsPerPage);
    }
};

}  // namespace magda::daw::ui
