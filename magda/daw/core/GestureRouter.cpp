#include "GestureRouter.hpp"

#include <unordered_set>

#include "Config.hpp"

namespace magda {

// ----------------------------------------------------------------------------
// Default tuning. These replace the magic-number sensitivities scattered across
// the ~20 ad-hoc mouseWheelMove handlers (e.g. scrollSpeed=50.0f). The
// arrangement consumer (#26) calibrates against real pixel/beat units; until a
// site migrates it keeps its own constants.
// ----------------------------------------------------------------------------
namespace {
// Scroll: magnitude is a pixel delta, so sensitivity scales the raw wheel
// delta (~0.195 per mouse tick on X11) into a sensible pixel step.
constexpr float kScrollSensitivity = 50.0f;
// Vertical track scroll matches juce::Viewport's default wheel formula
// (deltaY * 14 * singleStepSize=16), the same factor TrackHeadersPanel uses,
// so the arrangement body and the headers scroll in lockstep.
constexpr float kViewportScrollSensitivity = 14.0f * 16.0f;
// Zoom: magnitude is a power-of-two exponent (the consumer applies
// newZoom = oldZoom * 2^magnitude), so sensitivity is the zoom feel per wheel
// unit. ~0.5 gives roughly a 7% zoom step per mouse tick.
constexpr float kZoomSensitivity = 0.5f;
// Waveform editor horizontal scroll: preserves the editor's original
// raw-delta * 800 sample step.
constexpr float kWaveformScrollSensitivity = 800.0f;
// MIDI editors historically used raw wheel delta * 100 for both axes.
constexpr float kMidiEditorScrollSensitivity = 100.0f;
// MIDI editor wheel zoom previously used 1 + deltaY * 0.1. Expressed as a
// power-of-two exponent, this is roughly 0.14 for JUCE's normal wheel delta.
constexpr float kMidiEditorZoomSensitivity = 0.14f;
// Drag zoom sensitivity: pixels per power-of-two zoom step. Existing ruler and
// zoom-strip drag paths mostly used 30 px per double/halve, while "turbo" shift
// zoom used 8 px.
constexpr float kDragZoomSensitivity = 30.0f;
constexpr float kFastDragZoomSensitivity = 8.0f;
constexpr float kFineDragZoomSensitivity = 90.0f;
constexpr float kKeyboardDragZoomSensitivity = 10.0f;

GestureBinding retuneLearnedDragBinding(
    GestureContext context, GestureInput input, GestureBinding binding,
    const std::unordered_map<uint64_t, GestureBinding>& defaults) {
    if (input.kind != GestureInputKind::Drag ||
        (binding.action != GestureActionType::ZoomHorizontal &&
         binding.action != GestureActionType::ZoomVertical)) {
        return binding;
    }

    auto findMatchingDefault = [&](const GestureInput& candidate) {
        const auto it = defaults.find(GestureRouter::makeKey(context, candidate));
        return it != defaults.end() && it->second.action == binding.action ? &it->second : nullptr;
    };

    if (const auto* exact = findMatchingDefault(input)) {
        if (binding.sensitivity < 2.0f || input.axis != GestureAxis::Vertical) {
            binding.sensitivity = exact->sensitivity;
            binding.invert = exact->invert;
        }
        return binding;
    }

    auto alternateAxis = input;
    alternateAxis.axis =
        input.axis == GestureAxis::Vertical ? GestureAxis::Horizontal : GestureAxis::Vertical;
    if (const auto* alternate = findMatchingDefault(alternateAxis)) {
        binding.sensitivity = alternate->sensitivity;
        binding.invert = alternate->invert;
        return binding;
    }

    auto unmodifiedAlternate = alternateAxis;
    unmodifiedAlternate.modifiers = GestureMod_None;
    if (const auto* unmodified = findMatchingDefault(unmodifiedAlternate)) {
        binding.sensitivity = unmodified->sensitivity;
        binding.invert = unmodified->invert;
        return binding;
    }

    binding.sensitivity = kDragZoomSensitivity;
    return binding;
}
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

uint64_t GestureRouter::makeKey(GestureContext context, const GestureInput& input) {
    auto normalized = input;
    if (normalized.kind == GestureInputKind::Drag)
        normalized.axis = GestureAxis::Vertical;

    // Pack each enum into 8 bits. This keeps old wheel rows compact while
    // leaving room for more input kinds/areas without changing persistence.
    return (static_cast<uint64_t>(context) << 32) | (static_cast<uint64_t>(normalized.kind) << 24) |
           (static_cast<uint64_t>(normalized.area) << 16) |
           (static_cast<uint64_t>(normalized.axis) << 8) |
           static_cast<uint64_t>(normalized.modifiers);
}

GestureInput GestureRouter::makeWheelInput(GestureAxis axis, uint8_t modifierMask) {
    return {GestureInputKind::Wheel, GestureArea::Main, axis, modifierMask};
}

void GestureRouter::installDefaults() {
    bindings_.clear();

    // Arrangement defaults. A plain wheel scrolls the tracks vertically, the
    // same as the headers and as every system default, so it never surprises a
    // trackpad or a mouse user. A trackpad horizontal swipe (deltaX) scrolls
    // the timeline horizontally, preserving that workflow. The real gap this
    // addresses is horizontal scroll on a vertical-only mouse, which has no
    // deltaX at all: Shift+wheel is a sensible default for it, but it is just a
    // default. Every one of these is user-overridable (#22), because there is
    // no universal convention for mouse horizontal scroll. Command zooms the
    // timeline about the cursor; Alt zooms track height.
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_None,
               {GestureActionType::ScrollVertical, kViewportScrollSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Horizontal, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kScrollSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_Shift,
               {GestureActionType::ScrollHorizontal, kScrollSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_Command,
               {GestureActionType::ZoomHorizontal, kZoomSensitivity, false});
    setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_Alt,
               {GestureActionType::ZoomVertical, kZoomSensitivity, false});

    // MIDI editors share the same navigation vocabulary as arrangement:
    // ordinary wheel scrolls vertically, Shift+wheel scrolls horizontally,
    // trackpad deltaX scrolls horizontally, Command+wheel zooms the timebase,
    // and Alt+wheel zooms note/row height.
    setBinding(GestureContext::PianoRoll, GestureAxis::Vertical, GestureMod_None,
               {GestureActionType::ScrollVertical, kMidiEditorScrollSensitivity, false});
    setBinding(GestureContext::PianoRoll, GestureAxis::Horizontal, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kMidiEditorScrollSensitivity, false});
    setBinding(GestureContext::PianoRoll, GestureAxis::Vertical, GestureMod_Shift,
               {GestureActionType::ScrollHorizontal, kMidiEditorScrollSensitivity, false});
    setBinding(GestureContext::PianoRoll, GestureAxis::Vertical, GestureMod_Command,
               {GestureActionType::ZoomHorizontal, kMidiEditorZoomSensitivity, false});
    setBinding(GestureContext::PianoRoll, GestureAxis::Vertical, GestureMod_Alt,
               {GestureActionType::ZoomVertical, kZoomSensitivity, false});

    setBinding(GestureContext::DrumGrid, GestureAxis::Vertical, GestureMod_None,
               {GestureActionType::ScrollVertical, kMidiEditorScrollSensitivity, false});
    setBinding(GestureContext::DrumGrid, GestureAxis::Horizontal, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kMidiEditorScrollSensitivity, false});
    setBinding(GestureContext::DrumGrid, GestureAxis::Vertical, GestureMod_Shift,
               {GestureActionType::ScrollHorizontal, kMidiEditorScrollSensitivity, false});
    setBinding(GestureContext::DrumGrid, GestureAxis::Vertical, GestureMod_Command,
               {GestureActionType::ZoomHorizontal, kMidiEditorZoomSensitivity, false});
    setBinding(GestureContext::DrumGrid, GestureAxis::Vertical, GestureMod_Alt,
               {GestureActionType::ZoomVertical, kZoomSensitivity, false});

    // Waveform editor: the wheel scrolls the sample view horizontally. The
    // sensitivity preserves the editor's original step (raw delta * 800).
    setBinding(GestureContext::Waveform, GestureAxis::Vertical, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kWaveformScrollSensitivity, false});
    setBinding(GestureContext::Waveform, GestureAxis::Horizontal, GestureMod_None,
               {GestureActionType::ScrollHorizontal, kWaveformScrollSensitivity, false});
    setBinding(GestureContext::Waveform, GestureAxis::Vertical, GestureMod_Command,
               {GestureActionType::ZoomHorizontal, kZoomSensitivity, false});

    // Drag zoom defaults. These make the existing ruler/body/keyboard/strip
    // click-drag zoom paths visible to preferences and routeable through the
    // same binding model as wheel gestures.
    setBinding(GestureContext::Arrangement,
               {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical, GestureMod_None},
               {GestureActionType::ZoomHorizontal, kDragZoomSensitivity, false});
    setBinding(
        GestureContext::Arrangement,
        {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical, GestureMod_Shift},
        {GestureActionType::ZoomHorizontal, kFastDragZoomSensitivity, false});
    setBinding(GestureContext::Arrangement,
               {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical, GestureMod_Alt},
               {GestureActionType::ZoomHorizontal, kFineDragZoomSensitivity, false});

    // Copy-on-drag: holding Alt while dragging a clip body duplicates it
    // instead of moving it. Applies identically to single and multi-clip
    // drags; the modifier is user-customisable like every other gesture.
    setBinding(GestureContext::Arrangement,
               {GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical, GestureMod_Alt},
               {GestureActionType::DuplicateOnDrag, 1.0f, false});

    for (auto context : {GestureContext::PianoRoll, GestureContext::DrumGrid}) {
        setBinding(
            context,
            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical, GestureMod_None},
            {GestureActionType::ZoomHorizontal, kDragZoomSensitivity, false});
        setBinding(context,
                   {GestureInputKind::Drag, GestureArea::ZoomStrip, GestureAxis::Vertical,
                    GestureMod_None},
                   {GestureActionType::ZoomVertical, kDragZoomSensitivity, false});
    }
    setBinding(
        GestureContext::PianoRoll,
        {GestureInputKind::Drag, GestureArea::Keyboard, GestureAxis::Horizontal, GestureMod_None},
        {GestureActionType::ZoomVertical, kKeyboardDragZoomSensitivity, false});

    setBinding(GestureContext::Waveform,
               {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical, GestureMod_None},
               {GestureActionType::ZoomHorizontal, kDragZoomSensitivity, false});
    setBinding(
        GestureContext::Waveform,
        {GestureInputKind::Drag, GestureArea::Header, GestureAxis::Vertical, GestureMod_None},
        {GestureActionType::ZoomHorizontal, kDragZoomSensitivity, false});
    setBinding(GestureContext::Waveform,
               {GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical, GestureMod_None},
               {GestureActionType::ZoomHorizontal, kDragZoomSensitivity, false});

    // Snapshot the defaults so toVar() can emit only the user's overrides.
    defaults_ = bindings_;
}

void GestureRouter::setBinding(GestureContext context, GestureAxis axis, uint8_t modifierMask,
                               const GestureBinding& binding) {
    setBinding(context, makeWheelInput(axis, modifierMask), binding);
}

void GestureRouter::setBinding(GestureContext context, const GestureInput& input,
                               const GestureBinding& binding) {
    bindings_[makeKey(context, input)] = binding;
}

void GestureRouter::clearBinding(GestureContext context, GestureAxis axis, uint8_t modifierMask) {
    clearBinding(context, makeWheelInput(axis, modifierMask));
}

void GestureRouter::clearBinding(GestureContext context, const GestureInput& input) {
    bindings_.erase(makeKey(context, input));
}

const GestureBinding* GestureRouter::findBinding(GestureContext context, GestureAxis axis,
                                                 uint8_t modifierMask) const {
    return findBinding(context, makeWheelInput(axis, modifierMask));
}

const GestureBinding* GestureRouter::findBinding(GestureContext context,
                                                 const GestureInput& input) const {
    auto it = bindings_.find(makeKey(context, input));
    return it != bindings_.end() ? &it->second : nullptr;
}

const GestureBinding* GestureRouter::findDefaultBinding(GestureContext context, GestureAxis axis,
                                                        uint8_t modifierMask) const {
    return findDefaultBinding(context, makeWheelInput(axis, modifierMask));
}

const GestureBinding* GestureRouter::findDefaultBinding(GestureContext context,
                                                        const GestureInput& input) const {
    auto it = defaults_.find(makeKey(context, input));
    return it != defaults_.end() ? &it->second : nullptr;
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

ResolvedGesture GestureRouter::resolveDrag(GestureContext context, GestureArea area,
                                           GestureAxis axis, const juce::ModifierKeys& mods,
                                           float rawDelta, juce::Point<int> anchor) const {
    const GestureInput input{GestureInputKind::Drag, area, axis, gestureModifierMaskFrom(mods)};
    const auto* binding = findBinding(context, input);
    if (binding == nullptr || binding->action == GestureActionType::None)
        return {};

    ResolvedGesture out;
    out.type = binding->action;
    const float divisor = juce::jmax(0.01f, binding->sensitivity);
    out.magnitude = (rawDelta / divisor) * (binding->invert ? -1.0f : 1.0f);
    if (binding->action == GestureActionType::ZoomHorizontal ||
        binding->action == GestureActionType::ZoomVertical) {
        out.anchor = anchor;
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

bool GestureRouter::isDuplicateOnDrag(GestureContext context,
                                      const juce::ModifierKeys& mods) const {
    const GestureInput input{GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                             gestureModifierMaskFrom(mods)};
    const auto* binding = findBinding(context, input);
    return binding != nullptr && binding->action == GestureActionType::DuplicateOnDrag;
}

juce::var GestureRouter::toVar() const {
    // Emit only bindings that differ from (or are absent in) the defaults, so
    // config.json stores user overrides and code stays the source of truth.
    juce::Array<juce::var> overrides;
    for (const auto& [key, binding] : bindings_) {
        auto def = defaults_.find(key);
        if (def != defaults_.end() && def->second == binding)
            continue;

        auto* obj = new juce::DynamicObject();
        obj->setProperty("context", static_cast<int>((key >> 32) & 0xFF));
        obj->setProperty("kind", static_cast<int>((key >> 24) & 0xFF));
        obj->setProperty("area", static_cast<int>((key >> 16) & 0xFF));
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

    auto* arr = v.getArray();
    if (arr == nullptr)
        return;

    auto decode = [](const juce::DynamicObject& obj) {
        const auto context =
            static_cast<GestureContext>(static_cast<int>(obj.getProperty("context")));
        const auto kindValue = obj.getProperty("kind");
        const auto areaValue = obj.getProperty("area");
        const GestureInput input{
            static_cast<GestureInputKind>(kindValue.isVoid() ? 0 : static_cast<int>(kindValue)),
            static_cast<GestureArea>(areaValue.isVoid() ? 0 : static_cast<int>(areaValue)),
            static_cast<GestureAxis>(static_cast<int>(obj.getProperty("axis"))),
            static_cast<uint8_t>(static_cast<int>(obj.getProperty("mods")))};
        return std::pair{context, input};
    };

    // Drag keys collapse the axis (makeKey), so a learned binding on one axis
    // and a disabled-legacy binding on the other land on the same key. Collect
    // the keys that a non-None override claims first, so a None override never
    // clobbers a sibling that maps to the same key. A None override whose key
    // no one else claims still applies — that is the user disabling a default
    // drag gesture (e.g. moving copy-on-drag off Alt).
    std::unordered_set<uint64_t> claimedKeys;
    for (const auto& entry : *arr) {
        if (auto* obj = entry.getDynamicObject()) {
            const auto action =
                static_cast<GestureActionType>(static_cast<int>(obj->getProperty("action")));
            if (action != GestureActionType::None) {
                const auto [context, input] = decode(*obj);
                claimedKeys.insert(makeKey(context, input));
            }
        }
    }

    for (const auto& entry : *arr) {
        auto* obj = entry.getDynamicObject();
        if (obj == nullptr)
            continue;

        const auto [context, input] = decode(*obj);

        GestureBinding binding;
        binding.action =
            static_cast<GestureActionType>(static_cast<int>(obj->getProperty("action")));
        binding.sensitivity =
            static_cast<float>(static_cast<double>(obj->getProperty("sensitivity")));
        binding.invert = static_cast<bool>(obj->getProperty("invert"));
        binding = retuneLearnedDragBinding(context, input, binding, defaults_);

        if (input.kind == GestureInputKind::Drag && binding.action == GestureActionType::None &&
            claimedKeys.count(makeKey(context, input)) != 0) {
            continue;
        }
        setBinding(context, input, binding);
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
