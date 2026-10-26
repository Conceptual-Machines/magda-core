#pragma once

#include <array>

#include "core/InsertCcKnobs.hpp"
#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * @brief The External Insert's body: send, return and latency rows, the MIDI CC row, a page of
 *        CC knobs, and the status line at the foot (#3164).
 *
 * Rows give up height before the knobs do, so the body fits at the top of a chain and in a rack.
 * The faceplate and the param grid both place from this, over the same bounds.
 */
struct ExternalInsertGeometry {
    std::array<juce::Rectangle<int>, 3> rows;
    juce::Rectangle<int> ccHeader;
    std::array<juce::Rectangle<int>, insert_cc::kKnobsPerPage> cells;
    juce::Rectangle<int> status;

    static constexpr int kSide = 8;
    static constexpr int kGap = 4;
    static constexpr int kLabelWidth = 84;
    static constexpr int kMaxRowHeight = 26;
    static constexpr int kMinRowHeight = 20;
    static constexpr int kHeaderHeight = 20;
    static constexpr int kStatusHeight = 20;

    static int cellHeight(ParamControlStyle style) {
        switch (style) {
            case ParamControlStyle::Knobs:
                return 72;
            case ParamControlStyle::Sliders:
                return 44;
            case ParamControlStyle::Text:
                break;
        }
        return 48;
    }

    static ExternalInsertGeometry of(juce::Rectangle<int> bounds, ParamControlStyle style) {
        ExternalInsertGeometry g;
        auto area = bounds.reduced(kSide, 0).withTrimmedTop(kGap).withTrimmedBottom(kGap);
        g.status = area.removeFromBottom(kStatusHeight);
        area.removeFromBottom(kGap);

        const int knobs = cellHeight(style);
        const int fixed = 2 * kGap + 2 * kGap + kHeaderHeight + kGap + knobs;
        const int rowHeight =
            juce::jlimit(kMinRowHeight, kMaxRowHeight, (area.getHeight() - fixed) / 3);
        for (auto& row : g.rows) {
            row = area.removeFromTop(rowHeight);
            area.removeFromTop(kGap);
        }
        area.removeFromTop(kGap);
        g.ccHeader = area.removeFromTop(kHeaderHeight);
        area.removeFromTop(kGap);

        auto knobRow = area.removeFromTop(juce::jmin(knobs, area.getHeight()));
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
        return ExternalInsertGeometry::of(area, style_).cells[static_cast<size_t>(cell)];
    }
    void setControlStyle(ParamControlStyle style) override {
        style_ = style;
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

  private:
    ParamControlStyle style_ = ParamControlStyle::Text;
};

}  // namespace magda::daw::ui
