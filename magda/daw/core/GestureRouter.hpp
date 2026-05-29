#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace magda {

// ============================================================================
// GESTURE ROUTER (epic:command-registry, #21)
// ============================================================================
//
// The analog/contextual counterpart to juce::ApplicationCommandManager.
//
// A keypress is *discrete*: it fires one action, and the command manager
// already models it. A mouse-wheel gesture is *analog + axis-bearing +
// contextual*: it carries a magnitude (deltaX/deltaY), an input axis, active
// modifiers and a cursor anchor, and it means different things in different
// views. That does not fit ApplicationCommandInfo, so gestures get their own
// resolver: (context, input axis, modifiers) -> parametric action.
//
// ## Flow
//
//   1. A component receives a mouseWheelMove(MouseEvent, MouseWheelDetails).
//   2. It forwards the raw details + its GestureContext + the cursor position
//      to GestureRouter::resolve().
//   3. The router looks up the matching GestureBinding and returns a
//      ResolvedGesture: an action type, a signed magnitude (post-sensitivity,
//      post-invert) and an optional cursor anchor.
//   4. The component (or, later, a per-context sink such as TimelineController)
//      applies the action.
//
// ## Defaults & persistence
//
// Code holds the canonical default bindings (installDefaults()). Config stores
// only the user's overrides as an opaque blob (#22): toVar() emits the diff
// against defaults, loadFromVar() resets to defaults then applies the diff.
//
// This is the foundation only (#21). The arrangement becomes the first real
// consumer in #26; remaining mouse-gesture sites migrate in #1350.
// ============================================================================

/** The view a gesture originated in. Determines which binding set applies. */
enum class GestureContext {
    Arrangement,
    PianoRoll,
    CurveEditor,
    Waveform,
    DrumGrid,
    ValueLabel,
    Chain,
    Unknown,
};

/** The wheel axis an event arrived on. Part of the binding key, because the
 *  same modifier set can mean different things for a horizontal vs vertical
 *  wheel (and X11 mice only ever emit Vertical). */
enum class GestureAxis {
    Vertical,    // deltaY (plain mouse wheel; the only axis X11 emits)
    Horizontal,  // deltaX (trackpad horizontal swipe)
};

/** The parametric action a gesture resolves to. */
enum class GestureActionType {
    None,
    ScrollHorizontal,
    ScrollVertical,
    ZoomHorizontal,
    ZoomVertical,
    Pan,
};

/** Normalized modifier bitmask. Command is the platform primary modifier
 *  (Cmd on macOS, Ctrl on Windows/Linux), mirroring juce::ModifierKeys. */
enum GestureModifier : uint8_t {
    GestureMod_None = 0,
    GestureMod_Shift = 1 << 0,
    GestureMod_Command = 1 << 1,
    GestureMod_Alt = 1 << 2,
};

/** Derive the normalized modifier mask from a JUCE modifier state. */
uint8_t gestureModifierMaskFrom(const juce::ModifierKeys& mods);

/** A single (context, axis, modifiers) -> action mapping plus its tuning. */
struct GestureBinding {
    GestureActionType action = GestureActionType::None;
    float sensitivity = 1.0f;  // multiplies the raw wheel delta
    bool invert = false;       // flips the sign of the magnitude

    bool operator==(const GestureBinding& o) const {
        return action == o.action && juce::approximatelyEqual(sensitivity, o.sensitivity) &&
               invert == o.invert;
    }
    bool operator!=(const GestureBinding& o) const {
        return !(*this == o);
    }
};

/** The result of resolving a wheel event against the active bindings. */
struct ResolvedGesture {
    GestureActionType type = GestureActionType::None;
    float magnitude = 0.0f;   // signed, post-sensitivity, post-invert
    juce::Point<int> anchor;  // cursor position (valid when hasAnchor)
    bool hasAnchor = false;   // true for cursor-anchored actions (zoom)

    bool isNone() const {
        return type == GestureActionType::None;
    }
};

class GestureRouter {
  public:
    static GestureRouter& getInstance();

    /** Resolve a raw wheel event in a given context to a parametric action.
     *  position is the cursor location in the component's local coordinates,
     *  used as the anchor for cursor-anchored actions (zoom). */
    ResolvedGesture resolve(GestureContext context, const juce::MouseWheelDetails& wheel,
                            const juce::ModifierKeys& mods, juce::Point<int> position) const;

    /** Look up the binding for an exact (context, axis, modifiers) key, or
     *  nullptr if none is bound. */
    const GestureBinding* findBinding(GestureContext context, GestureAxis axis,
                                      uint8_t modifierMask) const;

    /** Install or replace a single binding. */
    void setBinding(GestureContext context, GestureAxis axis, uint8_t modifierMask,
                    const GestureBinding& binding);

    /** Restore all bindings to the code-defined defaults. */
    void resetToDefaults();

    // --- Persistence (#22) ----------------------------------------------
    // toVar() emits only the bindings that differ from the defaults, as a
    // juce::Array of {context, axis, mods, action, sensitivity, invert}
    // objects. loadFromVar() resets to defaults then applies the overrides.

    juce::var toVar() const;
    void loadFromVar(const juce::var& v);

    /** Load overrides from Config::getGestureBindings(). Call once after
     *  Config has been loaded from disk. */
    void loadFromConfig();

    /** Persist the current override diff into Config and save it. */
    void saveToConfig() const;

  private:
    GestureRouter();

    void installDefaults();

    static uint32_t makeKey(GestureContext context, GestureAxis axis, uint8_t modifierMask);

    std::unordered_map<uint32_t, GestureBinding> bindings_;
    std::unordered_map<uint32_t, GestureBinding> defaults_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GestureRouter)
};

}  // namespace magda
