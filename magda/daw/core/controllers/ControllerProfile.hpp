#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "Binding.hpp"
#include "Controller.hpp"

namespace magda {

// ============================================================================
// ControllerProfile structs
// ============================================================================

/**
 * @brief Describes a single physical control on a hardware device.
 */
struct ControllerProfileControl {
    juce::String controlId;  // stable, e.g. "knob_1"
    juce::String kind;       // "knob" | "button" | "encoder"
    int cc = -1;             // MIDI CC number
    int channel = -1;        // -1 == any
    int feedbackCc = -1;     // optional, -1 == none

    bool operator==(const ControllerProfileControl& other) const {
        return controlId == other.controlId && kind == other.kind && cc == other.cc &&
               channel == other.channel && feedbackCc == other.feedbackCc;
    }
};

/**
 * @brief Describes a default binding to create when a profile is materialised.
 */
struct ControllerProfileDefaultBinding {
    juce::String controlId;      // must match a ControllerProfileControl.controlId
    juce::String resolverKind;   // e.g. "focused.macro"
    juce::StringPairArray args;  // resolver args, e.g. macroIndex=0

    bool operator==(const ControllerProfileDefaultBinding& other) const {
        return controlId == other.controlId && resolverKind == other.resolverKind &&
               args == other.args;
    }
};

/**
 * @brief Immutable hardware profile. Ships with the app as JSON, read-only after load.
 *
 * A profile is a template describing the physical capabilities of a hardware controller.
 * A Controller (in Config) is a user instance that references a profile by id.
 */
struct ControllerProfile {
    juce::String id;  // stable string id, e.g. "novation.launchkey_mk3_37"
    juce::String vendor;
    juce::String name;
    std::vector<ControllerProfileControl> controls;
    std::vector<ControllerProfileDefaultBinding> defaultBindings;

    /** Returns true when id + name are non-empty and controls is non-empty. */
    bool isValid() const;

    bool operator==(const ControllerProfile& other) const {
        return id == other.id && vendor == other.vendor && name == other.name &&
               controls == other.controls && defaultBindings == other.defaultBindings;
    }
};

// ============================================================================
// JSON round-trip
// ============================================================================

/** Encode a ControllerProfile to a juce::var object suitable for JSON storage. */
juce::var encodeControllerProfile(const ControllerProfile& p);

/**
 * @brief Decode a ControllerProfile from a juce::var object.
 *
 * Returns nullopt when required fields (id, name) are missing or controls is empty.
 * Malformed default bindings are skipped individually (logged at DBG level).
 */
std::optional<ControllerProfile> decodeControllerProfile(const juce::var& v);

/**
 * @brief A single issue found by validateControllerProfile.
 *
 * `key` is a StringTable key (e.g. "controllers.validation.duplicate_control_id");
 * the UI runs it through tr() and substitutes `{0}` with `arg`. Returning
 * structured values rather than baked English strings keeps the validator
 * usable from non-UI call sites and lets translations come through Crowdin.
 */
struct ProfileValidationIssue {
    juce::String key;
    juce::String arg;
};

/**
 * @brief Run cross-field consistency checks on an already-decoded profile.
 *
 * decodeControllerProfile catches structural problems; this catches semantic
 * ones a community-uploaded profile is likely to contain:
 *   - Duplicate controlId within controls[].
 *   - defaultBindings[*].controlId that doesn't appear in controls[].
 *
 * Returns a list of issues; an empty list means the profile is internally
 * consistent.
 */
std::vector<ProfileValidationIssue> validateControllerProfile(const ControllerProfile& p);

// ============================================================================
// Materialisation
// ============================================================================

/**
 * @brief Result of materialising a profile into a live Controller + Bindings.
 */
struct MaterialisedController {
    Controller controller;
    std::vector<Binding> bindings;
};

/**
 * @brief Create a Controller + initial Bindings from a profile.
 *
 * Assigns fresh UUIDs for the controller and each binding.
 * Does NOT add them to any registry -- caller does that.
 *
 * Default bindings whose controlId is not in controls, or whose resolverKind
 * is not registered in ResolverRegistry, are skipped (logged at DBG level).
 */
MaterialisedController materialiseControllerFromProfile(const ControllerProfile& profile,
                                                        const juce::String& inputPort,
                                                        const juce::String& outputPort = {},
                                                        const juce::String& inputPortName = {});

}  // namespace magda
