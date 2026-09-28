#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/InsertConfigBridge.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

/**
 * @file test_insert_config_bridge_juce.cpp
 * @brief What an insert is, moved between the model and the fork (#2245).
 *
 * The incumbent keeps an insert's configuration inside a te::InsertPlugin's
 * ValueTree, so "what does this insert send to" was a question only the fork
 * could answer. The native engine compiles a send op and a return op from the
 * model, which means the model has to carry the same facts, and the bridge is
 * the only place the two representations meet.
 *
 * Both directions are tested against a real plugin rather than described,
 * because the half that is easy to get wrong is not the copying: it is which
 * end is which. The fork names its ends for what they are to the machine --
 * outputDevice is the send, inputDevice is the return -- and a bridge that read
 * them the other way round would compile a plan that sent audio to an input.
 */

namespace {

namespace audio = magda::daw::audio;
namespace te = tracktion::engine;

/// Kept as a pair: the cache owns the plugin through the Ptr, and the test
/// works through the typed pointer into it.
struct CreatedInsert {
    te::Plugin::Ptr owned;
    te::InsertPlugin* insert = nullptr;
};

CreatedInsert createInsert(te::Edit& edit) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, te::InsertPlugin::xmlTypeName, nullptr);

    CreatedInsert created;
    created.owned = edit.getPluginCache().createNewPlugin(state);
    created.insert = dynamic_cast<te::InsertPlugin*>(created.owned.get());
    return created;
}

class InsertConfigBridgeTest final : public juce::UnitTest {
  public:
    InsertConfigBridgeTest() : juce::UnitTest("Insert Config Bridge", "magda") {}

    void runTest() override {
        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);

        beginTest("An insert's ends reach the model the way round they are");
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        auto created = createInsert(*edit);
        auto* insert = created.insert;
        expect(insert != nullptr);
        if (insert == nullptr)
            return;

        // Named rather than resolved: on a machine with no interface these
        // resolve to nothing, which is the case the second half of this test is
        // about. What is being checked here is which field is which.
        insert->outputDevice = "Some Send";
        insert->inputDevice = "Some Return";
        insert->manualAdjustMs = -2.5;

        const auto config = audio::insertConfigOf(*insert);
        expectEquals(config.sendDevice, juce::String("Some Send"));
        expectEquals(config.returnDevice, juce::String("Some Return"));
        expectWithinAbsoluteError(config.manualAdjustMs, -2.5, 1.0e-9);

        beginTest("The types are the fork's derivation rather than the model's guess");

        // This machine has no interface with these ports on it, so the fork
        // resolves both ends to nothing -- and that is the honest answer, not a
        // gap to fill in. A bridge that read the names and called a named send
        // an audio send would hand the plan an insert this machine cannot make,
        // and the plan would compile a send op for hardware that is not there.
        expect(config.sendType == magda::InsertConfig::Endpoint::None);
        expect(config.returnType == magda::InsertConfig::Endpoint::None);
        expect(!config.isActive());

        beginTest("Writing the model back names the same ends");

        magda::InsertConfig wanted;
        wanted.sendType = magda::InsertConfig::Endpoint::Audio;
        wanted.returnType = magda::InsertConfig::Endpoint::Audio;
        wanted.sendDevice = "Written Send";
        wanted.returnDevice = "Written Return";
        wanted.manualAdjustMs = 4.0;

        audio::applyInsertConfig(*insert, wanted);

        expectEquals(insert->outputDevice.get(), juce::String("Written Send"));
        expectEquals(insert->inputDevice.get(), juce::String("Written Return"));
        expectWithinAbsoluteError(insert->manualAdjustMs.get(), 4.0, 1.0e-9);

        beginTest("And the model's own types are not written across");

        // The pair above claimed both ends are audio. The fork derives the
        // types from the names it was just handed, and on this machine those
        // resolve to nothing; a bridge that wrote the model's types across
        // would have told it that a device it cannot resolve is an audio send.
        expect(insert->getSendDeviceType() == te::InsertPlugin::noDevice);
        expect(insert->getReturnDeviceType() == te::InsertPlugin::noDevice);

        beginTest("Every device type maps to an endpoint and back");
        expect(audio::endpointOf(te::InsertPlugin::noDevice) ==
               magda::InsertConfig::Endpoint::None);
        expect(audio::endpointOf(te::InsertPlugin::audioDevice) ==
               magda::InsertConfig::Endpoint::Audio);
        expect(audio::endpointOf(te::InsertPlugin::midiDevice) ==
               magda::InsertConfig::Endpoint::MIDI);
    }
};

InsertConfigBridgeTest insertConfigBridgeTest;

}  // namespace
