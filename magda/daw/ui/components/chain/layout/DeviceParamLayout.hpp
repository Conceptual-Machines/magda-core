#pragma once

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief A single cell assignment produced by a DeviceParamLayout.
 *
 * Tells the host component what to draw in one grid cell:
 *   - `Filled`      — bind to a real parameter and render the widget
 *   - `Placeholder` — render an inert "empty" cell ("-", disabled)
 *   - `Hidden`      — don't render anything (cell is truly blank)
 *
 * `paramArrayIndex` is the index into `DeviceInfo::parameters`.
 * `targetParamIndex` is the identity used for automation / mod / MIDI Learn
 * binding. For most devices these are the same; Faust uses the pool slot
 * index from `[idx:N]` as the binding identity.
 */
struct ParamCell {
    enum class Mode { Filled, Placeholder, Hidden };

    Mode mode = Mode::Hidden;
    int paramArrayIndex = -1;
    int targetParamIndex = -1;
    bool enabled = true;
    /// How many consecutive cells this one occupies, starting here. Always 1
    /// for layouts that do not pack by width. A spanning cell is followed by
    /// `span - 1` Hidden cells, so the host can keep iterating cell by cell
    /// and simply draw nothing for the ones that were absorbed.
    int span = 1;
};

/**
 * @brief Strategy that owns all device-specific design decisions for the
 *        parameter grid.
 *
 * The host component (ParamHostComponent) is intentionally dumb: it owns
 * a fixed set of ParamSlotComponent instances and asks the layout, per
 * cell, what to put there. The layout decides:
 *   - which params go in which cell
 *   - gate / enabled state per cell
 *   - pagination shape (page size, total pages)
 *
 * Concrete implementations live next to the host (StandardDeviceLayout,
 * FaustDeviceLayout, …).
 */
class DeviceParamLayout {
  public:
    virtual ~DeviceParamLayout() = default;

    /// Total cells in the grid (e.g. 32 for 8x4).
    virtual int cellCount() const = 0;

    /// Cells per row — used by the host for setBounds() positioning.
    virtual int cellsPerRow() const = 0;

    /// Whether this layout uses pagination at all.
    virtual bool wantsPagination() const = 0;

    /// Total number of pages required to show every active param.
    virtual int totalPages(const magda::DeviceInfo& device) const = 0;

    /// Whether pagination should render as a named tab strip instead of the
    /// generic previous/next arrows.
    virtual bool wantsPageTabs() const {
        return false;
    }

    /// User-facing name for a page. The default keeps generic layouts numeric.
    virtual juce::String pageName(const magda::DeviceInfo&, int pageIndex) const {
        return juce::String(pageIndex + 1);
    }

    /// What to render in cell `cellIndex` on the current page.
    virtual ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                              int currentPage) const = 0;

    /// Resolve a stable parameter identity to the page that displays it.
    /// Generic implementation deliberately asks cellFor(), so custom layouts
    /// do not need a second mapping implementation.
    virtual int pageForParameter(const magda::DeviceInfo& device, int paramIndex) const {
        const int pages = totalPages(device);
        for (int page = 0; page < pages; ++page) {
            for (int cellIndex = 0; cellIndex < cellCount(); ++cellIndex) {
                const auto cell = cellFor(device, cellIndex, page);
                if (cell.mode == ParamCell::Mode::Filled && cell.targetParamIndex == paramIndex)
                    return page;
            }
        }
        return -1;
    }
};

}  // namespace magda::daw::ui
