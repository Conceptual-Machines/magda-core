#pragma once

#include <juce_core/juce_core.h>

/**
 * @file HostedParameterEdit.hpp
 * @brief A one-off value for a hosted plugin's parameter
 * (docs/specs/hosted-plugin-parameter-control.md).
 *
 * A command to the plugin, not a change to the document: nothing here needs
 * the parameter to be in the model, the table or the plan.
 */

namespace magda {

/// Where an edit came from. Delivery does not vary by origin.
enum class EditOrigin { Ui, Api, Controller, Undo };

/// Whether an edit was taken, or why it was not.
enum class EditStatus {
    /// Taken, and delivered on the control executor after this returns.
    Accepted,

    /// Not a finite normalised position. Refused rather than clamped.
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
 * Acceptance is not delivery: the value reaches the plugin on the control
 * executor, later.
 */
struct EditReceipt {
    EditStatus status = EditStatus::Unavailable;

    /// The position asked for, which a pending UI shows until an observation
    /// replaces it.
    float requested = 0.0f;

    bool accepted() const {
        return status == EditStatus::Accepted;
    }
};

/// @p status as something to put in a log or a message.
juce::String describeEditStatus(EditStatus status);

}  // namespace magda
