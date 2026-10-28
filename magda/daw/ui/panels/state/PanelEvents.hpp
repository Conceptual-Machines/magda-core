// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <variant>

#include "PanelState.hpp"

namespace magda::daw::ui {

/**
 * @brief Event to set the active tab in a panel
 */
struct SetActiveTabEvent {
    PanelLocation panel;
    int tabIndex;
};

/**
 * @brief Event to set active tab by content type
 */
struct SetActiveTabByTypeEvent {
    PanelLocation panel;
    PanelContentType contentType;
};

/**
 * @brief Event to toggle panel collapsed state
 */
struct TogglePanelCollapsedEvent {
    PanelLocation panel;
};

/**
 * @brief Event to set panel collapsed state explicitly
 */
struct SetPanelCollapsedEvent {
    PanelLocation panel;
    bool collapsed;
};

/**
 * @brief Event to set panel size (width for left/right, height for bottom)
 */
struct SetPanelSizeEvent {
    PanelLocation panel;
    int size;
};

/**
 * @brief Event to add a tab to a panel
 */
struct AddTabEvent {
    PanelLocation panel;
    PanelContentType contentType;
    int insertIndex = -1;  // -1 = append at end
};

/**
 * @brief Event to remove a tab from a panel
 */
struct RemoveTabEvent {
    PanelLocation panel;
    int tabIndex;
};

/**
 * @brief Event to reorder tabs within a panel
 */
struct ReorderTabsEvent {
    PanelLocation panel;
    std::vector<PanelContentType> newOrder;
};

/**
 * @brief Event to reset all panels to default configuration
 */
struct ResetPanelsToDefaultEvent {};

/**
 * @brief Union of all panel events
 */
using PanelEvent =
    std::variant<SetActiveTabEvent, SetActiveTabByTypeEvent, TogglePanelCollapsedEvent,
                 SetPanelCollapsedEvent, SetPanelSizeEvent, AddTabEvent, RemoveTabEvent,
                 ReorderTabsEvent, ResetPanelsToDefaultEvent>;

}  // namespace magda::daw::ui
