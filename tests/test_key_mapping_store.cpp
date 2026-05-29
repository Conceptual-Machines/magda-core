#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/Config.hpp"
#include "magda/daw/ui/state/KeyMappingStore.hpp"

using namespace magda;

namespace {

enum TestCommand { cmdFoo = 0x9001, cmdBar = 0x9002 };

// Minimal command target with two commands, each with a default keypress.
// cmdFoo defaults to 'F', cmdBar to 'B'.
class TestTarget : public juce::ApplicationCommandTarget {
  public:
    juce::ApplicationCommandTarget* getNextCommandTarget() override {
        return nullptr;
    }

    void getAllCommands(juce::Array<juce::CommandID>& c) override {
        c.add(cmdFoo);
        c.add(cmdBar);
    }

    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
        if (id == cmdFoo) {
            info.setInfo("Foo", "Foo", "Test", 0);
            info.addDefaultKeypress('f', juce::ModifierKeys::noModifiers);
        } else if (id == cmdBar) {
            info.setInfo("Bar", "Bar", "Test", 0);
            info.addDefaultKeypress('b', juce::ModifierKeys::noModifiers);
        }
    }

    bool perform(const InvocationInfo&) override {
        return true;
    }
};

}  // namespace

TEST_CASE("KeyMappingStore: restore re-applies a saved remap on top of defaults", "[keymap]") {
    juce::ScopedJuceInitialiser_GUI gui;  // ApplicationCommandManager needs the MM

    TestTarget target;
    auto& config = Config::getInstance();
    config.setKeyboardBindings(juce::var());  // start clean

    // Produce a "diff" override XML the way the app would: remap cmdFoo from 'f'
    // to 'g' on a separate (unlistened) command manager and serialize the diff.
    {
        juce::ApplicationCommandManager producer;
        producer.registerAllCommandsForTarget(&target);
        auto* km = producer.getKeyMappings();
        km->removeKeyPress(juce::KeyPress('f'));
        km->addKeyPress(cmdFoo, juce::KeyPress('g'));

        auto xml = km->createXml(/*saveDifferencesFromDefaultSet=*/true);
        REQUIRE(xml != nullptr);
        config.setKeyboardBindings(xml->toString());
    }

    // A fresh command manager starts at defaults; KeyMappingStore::restore()
    // must layer the saved override back on.
    juce::ApplicationCommandManager consumer;
    consumer.registerAllCommandsForTarget(&target);
    KeyMappingStore store(consumer);

    // Before restore: cmdFoo is still on its default 'f'.
    REQUIRE(consumer.getKeyMappings()->containsMapping(cmdFoo, juce::KeyPress('f')));

    store.restore();

    // After restore: cmdFoo is remapped to 'g', and the untouched cmdBar
    // default survives.
    REQUIRE(consumer.getKeyMappings()->containsMapping(cmdFoo, juce::KeyPress('g')));
    REQUIRE_FALSE(consumer.getKeyMappings()->containsMapping(cmdFoo, juce::KeyPress('f')));
    REQUIRE(consumer.getKeyMappings()->containsMapping(cmdBar, juce::KeyPress('b')));

    config.setKeyboardBindings(juce::var());  // don't leak into other tests
}

TEST_CASE("KeyMappingStore: empty/non-string config leaves defaults intact", "[keymap]") {
    juce::ScopedJuceInitialiser_GUI gui;

    TestTarget target;
    auto& config = Config::getInstance();

    juce::ApplicationCommandManager cm;
    cm.registerAllCommandsForTarget(&target);
    KeyMappingStore store(cm);

    config.setKeyboardBindings(juce::var());  // void
    store.restore();
    REQUIRE(cm.getKeyMappings()->containsMapping(cmdFoo, juce::KeyPress('f')));

    config.setKeyboardBindings(juce::var(""));  // empty string
    store.restore();
    REQUIRE(cm.getKeyMappings()->containsMapping(cmdFoo, juce::KeyPress('f')));
}
