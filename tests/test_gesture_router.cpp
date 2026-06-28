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

    SECTION("piano roll / drum grid: wheel gestures cover scroll and zoom") {
        REQUIRE(router
                    .resolve(GestureContext::PianoRoll, wheel(0.0f, 0.2f), juce::ModifierKeys(),
                             kAnchor)
                    .type == GestureActionType::ScrollVertical);
        REQUIRE(router.resolve(GestureContext::PianoRoll, wheel(0.0f, 0.2f), cmd, kAnchor).type ==
                GestureActionType::ZoomHorizontal);
        REQUIRE(router.resolve(GestureContext::PianoRoll, wheel(0.0f, 0.2f), alt, kAnchor).type ==
                GestureActionType::ZoomVertical);
        REQUIRE(
            router
                .resolve(GestureContext::DrumGrid, wheel(0.0f, 0.2f), juce::ModifierKeys(), kAnchor)
                .type == GestureActionType::ScrollVertical);
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
        REQUIRE(router.resolve(GestureContext::Waveform, wheel(0.0f, 0.2f), cmd, kAnchor).type ==
                GestureActionType::ZoomHorizontal);
    }
}

TEST_CASE("GestureRouter: drag zoom defaults", "[gesture]") {
    auto& router = GestureRouter::getInstance();
    router.resetToDefaults();

    SECTION("arrangement ruler vertical drag zooms horizontally") {
        auto g = router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                                    GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor);
        REQUIRE(g.type == GestureActionType::ZoomHorizontal);
        REQUIRE(g.hasAnchor);
        REQUIRE(g.anchor == kAnchor);
        REQUIRE(g.magnitude == 1.0f);
    }

    SECTION("MIDI editor drag zones route to horizontal and vertical zoom") {
        REQUIRE(router
                    .resolveDrag(GestureContext::PianoRoll, GestureArea::Ruler,
                                 GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor)
                    .type == GestureActionType::ZoomHorizontal);
        REQUIRE(router
                    .resolveDrag(GestureContext::PianoRoll, GestureArea::ZoomStrip,
                                 GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor)
                    .type == GestureActionType::ZoomVertical);
        REQUIRE(router
                    .resolveDrag(GestureContext::PianoRoll, GestureArea::Keyboard,
                                 GestureAxis::Horizontal, juce::ModifierKeys(), 10.0f, kAnchor)
                    .type == GestureActionType::ZoomVertical);
        REQUIRE(router
                    .resolveDrag(GestureContext::DrumGrid, GestureArea::ZoomStrip,
                                 GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor)
                    .type == GestureActionType::ZoomVertical);
    }

    SECTION("waveform ruler/header/body drag zones zoom horizontally") {
        for (auto area : {GestureArea::Ruler, GestureArea::Header, GestureArea::Body}) {
            REQUIRE(router
                        .resolveDrag(GestureContext::Waveform, area, GestureAxis::Vertical,
                                     juce::ModifierKeys(), 30.0f, kAnchor)
                        .type == GestureActionType::ZoomHorizontal);
        }
    }

    SECTION("drag zoom can be learned on the horizontal axis") {
        GestureInput input{GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Horizontal,
                           GestureMod_None};
        router.setBinding(GestureContext::Arrangement, input,
                          {GestureActionType::ZoomHorizontal, 25.0f, false});

        auto g = router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                                    GestureAxis::Horizontal, juce::ModifierKeys(), 25.0f, kAnchor);
        REQUIRE(g.type == GestureActionType::ZoomHorizontal);
        REQUIRE(g.magnitude == 1.0f);

        router.resetToDefaults();
    }
}

TEST_CASE("GestureRouter: copy-on-drag gesture", "[gesture]") {
    auto& router = GestureRouter::getInstance();
    router.resetToDefaults();

    SECTION("Alt is the default copy-drag modifier in the arrangement") {
        REQUIRE(router.isDuplicateOnDrag(GestureContext::Arrangement,
                                         juce::ModifierKeys(juce::ModifierKeys::altModifier)));
    }

    SECTION("a plain drag does not copy") {
        REQUIRE_FALSE(router.isDuplicateOnDrag(GestureContext::Arrangement, juce::ModifierKeys()));
    }

    SECTION("other modifiers do not trigger copy by default") {
        REQUIRE_FALSE(router.isDuplicateOnDrag(
            GestureContext::Arrangement, juce::ModifierKeys(juce::ModifierKeys::shiftModifier)));
        REQUIRE_FALSE(router.isDuplicateOnDrag(
            GestureContext::Arrangement, juce::ModifierKeys(juce::ModifierKeys::commandModifier)));
    }

    SECTION("the copy modifier is customisable and survives persistence") {
        // Rebind copy-on-drag from Alt to Command and round-trip through the
        // override blob, exactly as Preferences → Gestures would.
        const GestureInput altBody{GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                                   GestureMod_Alt};
        const GestureInput cmdBody{GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                                   GestureMod_Command};
        // Disabling a default means setting it to None (clearBinding would not
        // persist — toVar only stores diffs from defaults), exactly as the
        // preferences page does when a gesture's modifier is reassigned.
        router.setBinding(GestureContext::Arrangement, altBody,
                          {GestureActionType::None, 1.0f, false});
        router.setBinding(GestureContext::Arrangement, cmdBody,
                          {GestureActionType::DuplicateOnDrag, 1.0f, false});

        const auto blob = router.toVar();
        router.resetToDefaults();
        router.loadFromVar(blob);

        REQUIRE(router.isDuplicateOnDrag(GestureContext::Arrangement,
                                         juce::ModifierKeys(juce::ModifierKeys::commandModifier)));
        REQUIRE_FALSE(router.isDuplicateOnDrag(
            GestureContext::Arrangement, juce::ModifierKeys(juce::ModifierKeys::altModifier)));

        router.resetToDefaults();
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

    SECTION("drag override round-trips through var") {
        GestureInput input{GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                           GestureMod_None};
        router.setBinding(GestureContext::Waveform, input,
                          {GestureActionType::ZoomHorizontal, 44.0f, true});

        auto v = router.toVar();
        REQUIRE(v.getArray()->size() == 1);

        router.resetToDefaults();
        router.loadFromVar(v);

        const auto* b = router.findBinding(GestureContext::Waveform, input);
        REQUIRE(b != nullptr);
        REQUIRE(b->action == GestureActionType::ZoomHorizontal);
        REQUIRE(b->sensitivity == 44.0f);
        REQUIRE(b->invert);

        router.resetToDefaults();
    }

    SECTION("drag binding axis is ignored") {
        GestureInput horizontal{GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Horizontal,
                                GestureMod_None};
        router.setBinding(GestureContext::Arrangement, horizontal,
                          {GestureActionType::ZoomHorizontal, 30.0f, false});

        const auto fromHorizontal =
            router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                               GestureAxis::Horizontal, juce::ModifierKeys(), 30.0f, kAnchor);
        const auto fromVertical =
            router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                               GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor);

        REQUIRE(fromHorizontal.type == GestureActionType::ZoomHorizontal);
        REQUIRE(fromVertical.type == GestureActionType::ZoomHorizontal);
        REQUIRE(fromHorizontal.magnitude == 1.0f);
        REQUIRE(fromVertical.magnitude == 1.0f);

        router.resetToDefaults();
    }

    SECTION("learned drag zoom overrides using wheel sensitivity are retuned on load") {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("context", static_cast<int>(GestureContext::Arrangement));
        obj->setProperty("kind", static_cast<int>(GestureInputKind::Drag));
        obj->setProperty("area", static_cast<int>(GestureArea::Ruler));
        obj->setProperty("axis", static_cast<int>(GestureAxis::Horizontal));
        obj->setProperty("mods", static_cast<int>(GestureMod_None));
        obj->setProperty("action", static_cast<int>(GestureActionType::ZoomHorizontal));
        obj->setProperty("sensitivity", 0.5);
        obj->setProperty("invert", false);

        juce::Array<juce::var> overrides;
        overrides.add(juce::var(obj));
        router.loadFromVar(juce::var(overrides));

        const auto g =
            router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                               GestureAxis::Horizontal, juce::ModifierKeys(), 30.0f, kAnchor);
        const auto vertical =
            router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                               GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor);
        REQUIRE(g.type == GestureActionType::ZoomHorizontal);
        REQUIRE(vertical.type == GestureActionType::ZoomHorizontal);
        REQUIRE(g.magnitude == 1.0f);
        REQUIRE(vertical.magnitude == 1.0f);

        router.resetToDefaults();
    }

    SECTION("collapsed drag axis prefers learned binding over disabled legacy axis") {
        juce::Array<juce::var> overrides;

        auto* learned = new juce::DynamicObject();
        learned->setProperty("context", static_cast<int>(GestureContext::Arrangement));
        learned->setProperty("kind", static_cast<int>(GestureInputKind::Drag));
        learned->setProperty("area", static_cast<int>(GestureArea::Ruler));
        learned->setProperty("axis", static_cast<int>(GestureAxis::Horizontal));
        learned->setProperty("mods", static_cast<int>(GestureMod_None));
        learned->setProperty("action", static_cast<int>(GestureActionType::ZoomHorizontal));
        learned->setProperty("sensitivity", 8.0);
        learned->setProperty("invert", false);
        overrides.add(juce::var(learned));

        auto* disabled = new juce::DynamicObject();
        disabled->setProperty("context", static_cast<int>(GestureContext::Arrangement));
        disabled->setProperty("kind", static_cast<int>(GestureInputKind::Drag));
        disabled->setProperty("area", static_cast<int>(GestureArea::Ruler));
        disabled->setProperty("axis", static_cast<int>(GestureAxis::Vertical));
        disabled->setProperty("mods", static_cast<int>(GestureMod_None));
        disabled->setProperty("action", static_cast<int>(GestureActionType::None));
        disabled->setProperty("sensitivity", 30.0);
        disabled->setProperty("invert", false);
        overrides.add(juce::var(disabled));

        router.loadFromVar(juce::var(overrides));

        const auto g =
            router.resolveDrag(GestureContext::Arrangement, GestureArea::Ruler,
                               GestureAxis::Vertical, juce::ModifierKeys(), 30.0f, kAnchor);
        REQUIRE(g.type == GestureActionType::ZoomHorizontal);
        REQUIRE(g.magnitude == 1.0f);

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
