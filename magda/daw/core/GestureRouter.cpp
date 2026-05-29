#include "GestureRouter.hpp"

#include "Config.hpp"

namespace magda {

// ----------------------------------------------------------------------------
// Default tuning. These replace the magic-number sensitivities scattered across
// the ~20 ad-hoc mouseWheelMove handlers (e.g. scrollSpeed=50.0f). The
// arrangement consumer (#26) calibrates against real pixel/beat units; until a
// site migrates it keeps its own constants.
// ----------------------------------------------------------------------------
namespace {
constexpr float kScrollSensitivity = 50.0f;
constexpr float kZoomSensitivity = 1.0f;
}  // namespace

uint8_t gestureModifierMaskFrom(const juce::ModifierKeys& mods) {
    uint8_t mask = GestureMod_None;
    if (mods.isShiftDown())
        mask |= GestureMod_Shift;
    if (mods.isCommandDown())  // Cmd on macOS, Ctrl on Windows/Linux
        mask |= GestureMod_Command;
    if (mods.isAltDown())
        mask |= GestureMod_Alt;
    return mask;
}

GestureRouter& GestureRouter::getInstance() {
    static GestureRouter instance;
    return instance;
}

GestureRouter::GestureRouter() {
    installDefaults();
}

uint32_t GestureRouter::makeKey(GestureContext context, GestureAxis axis, uint8_t modifierMask) {
    // Pack (context, axis, modifiers) into a single key. 8 bits each is ample:
    // a handful of contexts, two axes, three modifier bits.
    return (static_cast<uint32_t>(context) << 16) | (static_cast<uint32_t>(axis) << 8) |
           static_cast<uint32_t>(modifierMask);
}

void GestureRouter::installDefaults() {
    bindings_.clear();

    // Arrangement: the Linux-friendly defaults. A plain wheel (vertical input,
    // the only axis X11 emits) scrolls the timeline horizontally; a trackpad
    // horizontal swipe (deltaX) does the same. Shift routes to vertical scroll,
    // Command zooms horizontally about the cursor, Alt zooms track height.
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kScrollSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Horizontal, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kScrollSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_Shift,
               {GestureActionType::ScrollVertical, kScrollSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_Command,
               {GestureActionType::ZoomHorizontal, kZoomSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_Alt,
               {GestureActionType::ZoomVertical, kZoomSensitivity, false});

    // Snapshot the defaults so toVar() can emit only the user's overrides.
    defaults_ = bindings_;
}

void GestureRouter::setBinding(GestureContext context, GestureAxis axis, uint8_t modifierMask,
                               const GestureBinding& binding) {
    bindings_[makeKey(context, axis, modifierMask)] = binding;
}

const GestureBinding* GestureRouter::findBinding(GestureContext context, GestureAxis axis,
                                                 uint8_t modifierMask) const {
    auto it = bindings_.find(makeKey(context, axis, modifierMask));
    return it != bindings_.end() ? &it->second : nullptr;
}

ResolvedGesture GestureRouter::resolve(GestureContext context, const juce::MouseWheelDetails& wheel,
                                       const juce::ModifierKeys& mods,
                                       juce::Point<int> position) const {
    // Pick the dominant input axis. A plain mouse wheel only carries deltaY
    // (X11 never sets deltaX); a trackpad may carry both, so the larger
    // magnitude wins.
    const bool horizontalInput = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    const GestureAxis axis = horizontalInput ? GestureAxis::Horizontal : GestureAxis::Vertical;
    const float rawDelta = horizontalInput ? wheel.deltaX : wheel.deltaY;

    const auto* binding = findBinding(context, axis, gestureModifierMaskFrom(mods));
    if (binding == nullptr || binding->action == GestureActionType::None)
        return {};

    ResolvedGesture out;
    out.type = binding->action;
    out.magnitude = rawDelta * binding->sensitivity * (binding->invert ? -1.0f : 1.0f);
    if (wheel.isReversed)
        out.magnitude = -out.magnitude;

    // Cursor-anchored actions (zoom) carry the anchor; scroll/pan do not.
    if (binding->action == GestureActionType::ZoomHorizontal ||
        binding->action == GestureActionType::ZoomVertical) {
        out.anchor = position;
        out.hasAnchor = true;
    }

    return out;
}

void GestureRouter::resetToDefaults() {
    bindings_ = defaults_;
}

// ----------------------------------------------------------------------------
// Persistence (#22)
// ----------------------------------------------------------------------------

juce::var GestureRouter::toVar() const {
    // Emit only bindings that differ from (or are absent in) the defaults, so
    // config.json stores user overrides and code stays the source of truth.
    juce::Array<juce::var> overrides;
    for (const auto& [key, binding] : bindings_) {
        auto def = defaults_.find(key);
        if (def != defaults_.end() && def->second == binding)
            continue;

        auto* obj = new juce::DynamicObject();
        obj->setProperty("context", static_cast<int>((key >> 16) & 0xFF));
        obj->setProperty("axis", static_cast<int>((key >> 8) & 0xFF));
        obj->setProperty("mods", static_cast<int>(key & 0xFF));
        obj->setProperty("action", static_cast<int>(binding.action));
        obj->setProperty("sensitivity", binding.sensitivity);
        obj->setProperty("invert", binding.invert);
        overrides.add(juce::var(obj));
    }
    return overrides;
}

void GestureRouter::loadFromVar(const juce::var& v) {
    resetToDefaults();

    if (auto* arr = v.getArray()) {
        for (const auto& entry : *arr) {
            auto* obj = entry.getDynamicObject();
            if (obj == nullptr)
                continue;

            const auto context =
                static_cast<GestureContext>(static_cast<int>(obj->getProperty("context")));
            const auto axis = static_cast<GestureAxis>(static_cast<int>(obj->getProperty("axis")));
            const auto mask = static_cast<uint8_t>(static_cast<int>(obj->getProperty("mods")));

            GestureBinding binding;
            binding.action =
                static_cast<GestureActionType>(static_cast<int>(obj->getProperty("action")));
            binding.sensitivity =
                static_cast<float>(static_cast<double>(obj->getProperty("sensitivity")));
            binding.invert = static_cast<bool>(obj->getProperty("invert"));
            setBinding(context, axis, mask, binding);
        }
    }
}

void GestureRouter::loadFromConfig() {
    loadFromVar(Config::getInstance().getGestureBindings());
}

void GestureRouter::saveToConfig() const {
    auto& config = Config::getInstance();
    config.setGestureBindings(toVar());
    config.save();
}

}  // namespace magda
