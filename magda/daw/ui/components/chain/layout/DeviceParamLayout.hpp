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

    /// What to render in cell `cellIndex` on the current page.
    virtual ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                              int currentPage) const = 0;
};

}  // namespace magda::daw::ui
