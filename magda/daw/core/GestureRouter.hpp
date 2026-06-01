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

/** The physical gesture kind. Older persisted rows omit this and default to
 *  Wheel for backward compatibility. */
enum class GestureInputKind {
    Wheel,
    Drag,
};

/** The interactive area a gesture originated in. Wheel gestures use Main;
 *  drag gestures use this to distinguish ruler/body/keyboard/zoom-strip paths
 *  within the same editor context. */
enum class GestureArea {
    Main,
    Ruler,
    Body,
    Header,
    Keyboard,
    ZoomStrip,
};

/** The wheel axis or drag movement axis. Part of the binding key, because the
 *  same modifier set can mean different things for horizontal vs vertical
 *  movement. */
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

/** A single gesture input identity. */
struct GestureInput {
    GestureInputKind kind = GestureInputKind::Wheel;
    GestureArea area = GestureArea::Main;
    GestureAxis axis = GestureAxis::Vertical;
    uint8_t modifiers = GestureMod_None;
};

/** A single gesture input -> action mapping plus its tuning. For wheel
 *  gestures, sensitivity multiplies the raw wheel delta. For drag gestures,
 *  sensitivity is pixels per power-of-two zoom step, so larger is slower. */
struct GestureBinding {
    GestureActionType action = GestureActionType::None;
    float sensitivity = 1.0f;
    bool invert = false;  // flips the sign of the magnitude

    bool operator==(const GestureBinding& o) const {
        return action == o.action && juce::approximatelyEqual(sensitivity, o.sensitivity) &&
               invert == o.invert;
    }
    bool operator!=(const GestureBinding& o) const {
        return !(*this == o);
    }
};

/** The result of resolving a gesture event against the active bindings. */
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

    /** Resolve a drag delta in a given context/area to a parametric action.
     *  rawDelta is the movement in the binding axis, already signed in the
     *  consumer's natural direction (e.g. drag up/right = positive zoom). */
    ResolvedGesture resolveDrag(GestureContext context, GestureArea area, GestureAxis axis,
                                const juce::ModifierKeys& mods, float rawDelta,
                                juce::Point<int> anchor) const;

    /** Look up the binding for an exact (context, axis, modifiers) key, or
     *  nullptr if none is bound. */
    const GestureBinding* findBinding(GestureContext context, GestureAxis axis,
                                      uint8_t modifierMask) const;
    const GestureBinding* findBinding(GestureContext context, const GestureInput& input) const;

    /** Look up the code-defined default binding for an exact key. */
    const GestureBinding* findDefaultBinding(GestureContext context, GestureAxis axis,
                                             uint8_t modifierMask) const;
    const GestureBinding* findDefaultBinding(GestureContext context,
                                             const GestureInput& input) const;

    /** Install or replace a single binding. */
    void setBinding(GestureContext context, GestureAxis axis, uint8_t modifierMask,
                    const GestureBinding& binding);
    void setBinding(GestureContext context, const GestureInput& input,
                    const GestureBinding& binding);

    /** Remove a non-default binding for an exact key. */
    void clearBinding(GestureContext context, GestureAxis axis, uint8_t modifierMask);
    void clearBinding(GestureContext context, const GestureInput& input);

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

    static uint64_t makeKey(GestureContext context, const GestureInput& input);

  private:
    GestureRouter();

    void installDefaults();

    static GestureInput makeWheelInput(GestureAxis axis, uint8_t modifierMask);

    std::unordered_map<uint64_t, GestureBinding> bindings_;
    std::unordered_map<uint64_t, GestureBinding> defaults_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GestureRouter)
};

}  // namespace magda
