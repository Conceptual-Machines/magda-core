#pragma once

#include <juce_core/juce_core.h>

/**
 * @file HostedParameterEdit.hpp
 * @brief A one-off value for a hosted plugin's parameter
 * (docs/specs/hosted-plugin-parameter-control.md).
 *
 * A hosted plugin owns its ordinary patch, so editing one of its parameters is
 * a command to the plugin rather than a change to the document. Nothing here
 * requires the parameter to be in DeviceInfo::parameters, in the compiled
 * table, or in the render plan.
 */

namespace magda {

/// Where an edit came from. What each origin is allowed to do is the service's
/// to decide; delivery does not vary by origin.
enum class EditOrigin { Ui, Api, Controller, Undo };

/// Whether an edit was taken, or why it was not.
enum class EditStatus {
    /// Taken, and delivered on the control executor after this returns.
    Accepted,

    /// Not a finite normalised position. Refused rather than clamped, so a
    /// caller working in the wrong units hears about it.
    InvalidValue,

    /// No such parameter on the plugin, or a slot the model owns instead.
    UnknownParameter,

    /// No instance to deliver to: not loaded yet, or gone.
    Unavailable,

    /// The engine is shutting down and accepted nothing.
    Closing,
};

/**
 * @brief The immediate answer to an edit.
 *
 * Acceptance is not delivery. The value reaches the plugin on the control
 * executor, later; what this says is that the request was well formed and had
 * somewhere to go.
 */
struct EditReceipt {
    EditStatus status = EditStatus::Unavailable;

    /// The position asked for, which is what a pending UI shows until the
    /// plugin's own observation replaces it.
    float requested = 0.0f;

    bool accepted() const {
        return status == EditStatus::Accepted;
    }
};

/// @p status as something to put in a log or a message.
juce::String describeEditStatus(EditStatus status);

}  // namespace magda
