#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/GestureRouter.hpp"

using namespace magda;

namespace {

// Build a MouseWheelDetails with the given axis deltas. A plain X11 mouse wheel
// only sets deltaY (deltaX stays 0); a trackpad horizontal swipe sets deltaX.
juce::MouseWheelDetails wheel(float deltaX, float deltaY, bool reversed = false) {
    juce::MouseWheelDetails w;
    w.deltaX = deltaX;
    w.deltaY = deltaY;
    w.isReversed = reversed;
    w.isSmooth = false;
    w.isInertial = false;
    return w;
}

constexpr juce::Point<int> kAnchor{120, 40};

}  // namespace

TEST_CASE("GestureRouter: arrangement default bindings", "[gesture]") {
    auto& router = GestureRouter::getInstance();
    router.resetToDefaults();

    SECTION("plain wheel scrolls tracks vertically (matches the headers)") {
        auto g = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.195f),
                                juce::ModifierKeys(), kAnchor);
        REQUIRE(g.type == GestureActionType::ScrollVertical);
        REQUIRE(g.magnitude > 0.0f);
        REQUIRE_FALSE(g.hasAnchor);
    }

    SECTION("trackpad horizontal swipe scrolls the timeline horizontally") {
        auto g = router.resolve(GestureContext::Arrangement, wheel(0.5f, 0.0f),
                                juce::ModifierKeys(), kAnchor);
        REQUIRE(g.type == GestureActionType::ScrollHorizontal);
    }

    SECTION("Shift+wheel is the mouse-only horizontal scroll default") {
        auto g = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.195f),
                                juce::ModifierKeys(juce::ModifierKeys::shiftModifier), kAnchor);
        REQUIRE(g.type == GestureActionType::ScrollHorizontal);
    }

    SECTION("Command+wheel zooms horizontally, anchored at the cursor") {
        auto g = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.195f),
                                juce::ModifierKeys(juce::ModifierKeys::commandModifier), kAnchor);
        REQUIRE(g.type == GestureActionType::ZoomHorizontal);
        REQUIRE(g.hasAnchor);
        REQUIRE(g.anchor == kAnchor);
    }

    SECTION("Alt+wheel zooms vertically") {
        auto g = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.195f),
                                juce::ModifierKeys(juce::ModifierKeys::altModifier), kAnchor);
        REQUIRE(g.type == GestureActionType::ZoomVertical);
        REQUIRE(g.hasAnchor);
    }

    SECTION("unbound context resolves to nothing") {
        auto g = router.resolve(GestureContext::Unknown, wheel(0.0f, 0.195f), juce::ModifierKeys(),
                                kAnchor);
        REQUIRE(g.isNone());
    }
}

TEST_CASE("GestureRouter: editor context defaults (#1350)", "[gesture]") {
    auto& router = GestureRouter::getInstance();
    router.resetToDefaults();

    const juce::ModifierKeys alt(juce::ModifierKeys::altModifier);
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);

    SECTION("piano roll / drum grid: Alt+wheel zooms vertically, plain wheel is unbound") {
        REQUIRE(router.resolve(GestureContext::PianoRoll, wheel(0.0f, 0.2f), alt, kAnchor).type ==
                GestureActionType::ZoomVertical);
        REQUIRE(router
                    .resolve(GestureContext::PianoRoll, wheel(0.0f, 0.2f), juce::ModifierKeys(),
                             kAnchor)
                    .isNone());
        REQUIRE(router.resolve(GestureContext::DrumGrid, wheel(0.0f, 0.2f), alt, kAnchor).type ==
                GestureActionType::ZoomVertical);
        REQUIRE(router.resolve(GestureContext::DrumGrid, wheel(0.0f, 0.2f), cmd, kAnchor).type ==
                GestureActionType::ZoomHorizontal);
    }

    SECTION("waveform: plain wheel scrolls horizontally at the editor's sensitivity") {
        auto g = router.resolve(GestureContext::Waveform, wheel(0.0f, 1.0f), juce::ModifierKeys(),
                                kAnchor);
        REQUIRE(g.type == GestureActionType::ScrollHorizontal);
        REQUIRE(std::abs(g.magnitude) == 800.0f);  // raw delta 1.0 * sensitivity 800
    }
}

TEST_CASE("GestureRouter: magnitude sign and reversal", "[gesture]") {
    auto& router = GestureRouter::getInstance();
    router.resetToDefaults();

    auto up = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.2f), juce::ModifierKeys(),
                             kAnchor);
    auto down = router.resolve(GestureContext::Arrangement, wheel(0.0f, -0.2f),
                               juce::ModifierKeys(), kAnchor);
    REQUIRE(up.magnitude > 0.0f);
    REQUIRE(down.magnitude < 0.0f);

    SECTION("isReversed flips the sign") {
        auto rev = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.2f, true),
                                  juce::ModifierKeys(), kAnchor);
        REQUIRE(rev.magnitude < 0.0f);
    }

    SECTION("invert binding flips the sign") {
        router.setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_None,
                          {GestureActionType::ScrollHorizontal, 50.0f, true});
        auto inv = router.resolve(GestureContext::Arrangement, wheel(0.0f, 0.2f),
                                  juce::ModifierKeys(), kAnchor);
        REQUIRE(inv.magnitude < 0.0f);
        router.resetToDefaults();
    }
}

TEST_CASE("GestureRouter: persistence stores only overrides", "[gesture]") {
    auto& router = GestureRouter::getInstance();
    router.resetToDefaults();

    SECTION("defaults serialize to an empty override set") {
        auto v = router.toVar();
        REQUIRE(v.isArray());
        REQUIRE(v.getArray()->isEmpty());
    }

    SECTION("override round-trips through var") {
        router.setBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_None,
                          {GestureActionType::ScrollHorizontal, 99.0f, true});
        auto v = router.toVar();
        REQUIRE(v.getArray()->size() == 1);

        // A fresh load from the serialized var must reconstruct the override
        // on top of clean defaults.
        router.resetToDefaults();
        router.loadFromVar(v);

        const auto* b =
            router.findBinding(GestureContext::Arrangement, GestureAxis::Vertical, GestureMod_None);
        REQUIRE(b != nullptr);
        REQUIRE(b->action == GestureActionType::ScrollHorizontal);
        REQUIRE(b->sensitivity == 99.0f);
        REQUIRE(b->invert);

        // Untouched defaults survive the load.
        const auto* shift = router.findBinding(GestureContext::Arrangement, GestureAxis::Vertical,
                                               GestureMod_Shift);
        REQUIRE(shift != nullptr);
        REQUIRE(shift->action == GestureActionType::ScrollHorizontal);

        router.resetToDefaults();
    }
}

TEST_CASE("Config: gesture and keyboard binding blobs round-trip", "[gesture][config]") {
    // The opaque-blob storage added in #22: a value set on Config reads back
    // unchanged (in-memory; disk save/load is exercised by the Config tests).
    auto& config = Config::getInstance();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("probe", 7);
    juce::var blob(obj);

    config.setGestureBindings(blob);
    config.setKeyboardBindings(juce::var("<KEYMAPPINGS/>"));

    REQUIRE(config.getGestureBindings().getDynamicObject() != nullptr);
    REQUIRE(static_cast<int>(config.getGestureBindings()["probe"]) == 7);
    REQUIRE(config.getKeyboardBindings().toString() == "<KEYMAPPINGS/>");

    // Reset so we don't leak state into other tests sharing the singleton.
    config.setGestureBindings(juce::var());
    config.setKeyboardBindings(juce::var());
}
