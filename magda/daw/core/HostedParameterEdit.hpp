#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>

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
    /// Taken, and applied at the plugin's next block after this returns.
    Accepted,

    /// Not a finite normalised position. Refused rather than clamped.
    InvalidValue,

    /// No such parameter on the plugin, or a slot the model owns instead.
    UnknownParameter,

    /// No instance to deliver to: not loaded yet, or gone.
    Unavailable,

    /// The engine is shutting down and accepted nothing.
    Closing,

    /// As many edits are outstanding as the control plane holds (#2651).
    Busy,
};

/**
 * @brief The immediate answer to an edit.
 *
 * Acceptance is not delivery: the value reaches the plugin at its next block,
 * or from the control executor when nothing renders it.
 */
struct EditReceipt {
    EditStatus status = EditStatus::Unavailable;

    /// The position asked for, which a dragged control shows until the drag
    /// ends and an observation replaces it.
    float requested = 0.0f;

    bool accepted() const {
        return status == EditStatus::Accepted;
    }
};

/**
 * @brief How an accepted edit ended, on the message thread.
 *
 * @ref observed is read off the parameter after the attempt, whether or not the
 * write was taken, so a display can drop the value it asked for.
 */
struct EditCompletion {
    bool delivered = false;

    /// A later edit to the same slot reached the plugin first, so this one never
    /// did; its completion reads nothing, since the later one's will (#2651).
    bool superseded = false;

    /// Absent when there was no parameter left to read.
    std::optional<float> observed;
};

/** @brief What a reported parameter value is, as far as the host can tell. */
enum class ObservationSource : std::uint8_t {
    /// Inside the plugin's own begin/end gesture: a person moving it in its editor.
    EditorGesture,

    /// No gesture around it: readback of a host write, a program change, or
    /// the plugin's own modulation. Never a base update.
    Readback,

    /// The host was driving the slot, so this is its own output coming back.
    Driven,

    /// Read by the host after delivering a command, not reported by the plugin.
    CommandReadback,
};

/// @p status as something to put in a log or a message.
juce::String describeEditStatus(EditStatus status);

}  // namespace magda
