// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <variant>

#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief Event to change the current view mode
 */
struct SetViewModeEvent {
    ViewMode mode;
};

/**
 * @brief Event to request the current audio profile
 */
struct RequestAudioProfileEvent {};

/**
 * @brief Union of all view mode events
 *
 * Components dispatch these events to the ViewModeController,
 * which processes them and updates the view mode state accordingly.
 */
using ViewModeEvent = std::variant<SetViewModeEvent, RequestAudioProfileEvent>;

}  // namespace magda
