#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/DeviceServices.hpp"
#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

// Engine-neutral internal device state (#1887): capture writes a MAGDA document,
// restore accepts both that and the pre-v2 engine XML.

namespace {

namespace audio = magda::daw::audio;
namespace ta = magda::daw::audio::tracktion_adapter;
namespace ds = magda::device_state;
namespace te = tracktion::engine;

te::Plugin::Ptr createPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

class SchemaTestServices final : public audio::DeviceIdAllocator, public audio::DeviceTrackContext {
  public:
    magda::DeviceId allocateDeviceId() override {
        return nextDeviceId_++;
    }
    void ensureDeviceIdAbove(magda::DeviceId id) override {
        nextDeviceId_ = std::max(nextDeviceId_, id + 1);
    }
    bool isChordTrackMuted() const override {
        return false;
    }
    void setDeviceParameterValueFromPlugin(magda::DeviceId, int, float) override {}

  private:
    magda::DeviceId nextDeviceId_ = 8100;
};

class DeviceStateSchemaTest final : public juce::UnitTest {
  public:
    DeviceStateSchemaTest() : juce::UnitTest("Device State Schema", "magda") {}

    void runTest() override {
        beginTest("Engine setup");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr);
        if (edit == nullptr)
            return;

        SchemaTestServices testServices;
        audio::DeviceServices services;
        services.deviceIdAllocator = &testServices;
        services.trackContext = &testServices;
        const auto sessionKey = audio::DeviceSessionKey::fromAddress(edit.get());
        audio::registerDeviceServices(sessionKey, services);

        testCaptureShape(*edit);
        testRoundTrip(*edit);
        testNestedDeviceSurvives(*edit);
        testBinaryPropertySurvives(*edit);
        testLegacyStateStillLoads(*edit);

        audio::unregisterDeviceServices(sessionKey);
    }

  private:
    void testCaptureShape(te::Edit& edit) {
        beginTest("Captured device state is a MAGDA document, not an engine tree");

        auto plugin = createPlugin(edit, audio::ArpeggiatorPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        const auto state = ta::captureInternalDeviceState(*plugin);
        expect(state.isNotEmpty());

        // The acceptance criterion for #1887.
        expect(!ds::looksLikeLegacyEngineState(state), "captured state is still engine XML");
        expect(!state.contains("<PLUGIN"), "captured state still carries the engine container");

        const auto doc = ds::decode(state);
        expect(doc.has_value(), "captured state does not decode as v2");
        if (!doc)
            return;

        expectEquals(doc->version, ds::kSchemaVersion);
        expectEquals(doc->deviceType, juce::String(audio::ArpeggiatorPlugin::xmlTypeName));
        expect(!doc->params.empty(), "no parameters captured");

        // Engine-owned plugin-state facts must not survive into the document:
        // MAGDA owns each of them in DeviceInfo.
        for (const auto* engineOwned : {"id", "type", "enabled", "process", "windowX"})
            expect(!doc->root.props.contains(juce::Identifier(engineOwned)),
                   juce::String("engine-owned property leaked into the document: ") + engineOwned);

        plugin->deleteFromParent();
    }

    void testRoundTrip(te::Edit& edit) {
        beginTest("Device state round-trips through capture and restore");

        auto source = createPlugin(edit, audio::ArpeggiatorPlugin::xmlTypeName);
        auto* sourceArp = dynamic_cast<audio::ArpeggiatorPlugin*>(source.get());
        expect(sourceArp != nullptr);
        if (sourceArp == nullptr)
            return;

        // Non-parameter property (no automatable parameter mirrors it) plus a
        // parameter, so both halves of the document are exercised.
        sourceArp->quantizeSub = 8;
        sourceArp->hardAngle = true;
        if (auto gate = source->getAutomatableParameterByID("gate"))
            gate->setParameterFromHost(0.42f, juce::sendNotificationSync);

        const auto state = ta::captureInternalDeviceState(*source);
        source->deleteFromParent();

        auto restoredTree = ta::devicePluginTreeFromState(state);
        expect(restoredTree.isValid());
        expectEquals(restoredTree.getProperty(te::IDs::type).toString(),
                     juce::String(audio::ArpeggiatorPlugin::xmlTypeName));

        auto restored = edit.getPluginCache().createNewPlugin(restoredTree);
        expect(restored != nullptr);
        if (restored == nullptr)
            return;
        ta::applyDeviceStateParameters(*restored, state);

        auto* restoredArp = dynamic_cast<audio::ArpeggiatorPlugin*>(restored.get());
        expect(restoredArp != nullptr);
        if (restoredArp != nullptr) {
            expectEquals(restoredArp->quantizeSub.get(), 8);
            expect(restoredArp->hardAngle.get());
        }

        if (auto gate = restored->getAutomatableParameterByID("gate"))
            expectWithinAbsoluteError(gate->getCurrentBaseValue(), 0.42f, 0.001f);
        else
            expect(false, "restored plugin lost its 'gate' parameter");

        restored->deleteFromParent();
    }

    void testNestedDeviceSurvives(te::Edit& edit) {
        beginTest("Nested devices keep their identity through capture and restore");

        // DrumGrid persists each pad's device as a nested PLUGIN and rebuilds it
        // with getOrCreatePluginFor, which needs the nested `type`. The engine's
        // own plugin-state vocabulary is only dropped at the document root; a
        // recursive strip would leave every pad chain empty on reload.
        auto source = createPlugin(edit, audio::DrumGridPlugin::xmlTypeName);
        auto* sourceGrid = dynamic_cast<audio::DrumGridPlugin*>(source.get());
        expect(sourceGrid != nullptr);
        if (sourceGrid == nullptr)
            return;

        sourceGrid->loadInternalPluginToPad(0, "magda_kick");
        const auto* sourceChain = sourceGrid->getChainForNote(audio::DrumGridPlugin::baseNote);
        expect(sourceChain != nullptr, "pad chain was not created");
        if (sourceChain == nullptr)
            return;
        expect(!sourceChain->plugins.empty(), "pad has no plugin to capture");

        const auto state = ta::captureInternalDeviceState(*source);
        source->deleteFromParent();

        const auto doc = ds::decode(state);
        expect(doc.has_value());
        if (!doc)
            return;

        // The nested plugin's device id must still be in the document, otherwise
        // the id allocator cannot avoid reusing it.
        bool sawNestedType = false;
        ds::forEachNode(doc->root, [&](const ds::Node& node) {
            if (node.type == "PLUGIN" && node.props.contains(juce::Identifier("type")))
                sawNestedType = true;
        });
        expect(sawNestedType, "nested plugin lost its device type in the document");

        auto restoredTree = ta::devicePluginTreeFromState(state);
        expect(restoredTree.isValid());

        // Mirror PluginManager::createPluginOnly: DrumGrid is a FreshValueTree
        // device, so it is constructed empty and then populated from the saved
        // state - the constructor rebuilds chains, but only
        // restorePluginStateFromValueTree instantiates the pad plugins.
        auto restored = createPlugin(edit, audio::DrumGridPlugin::xmlTypeName);
        expect(restored != nullptr);
        if (restored == nullptr)
            return;
        restored->restorePluginStateFromValueTree(restoredTree);

        auto* restoredGrid = dynamic_cast<audio::DrumGridPlugin*>(restored.get());
        expect(restoredGrid != nullptr);
        if (restoredGrid != nullptr) {
            const auto* chain = restoredGrid->getChainForNote(audio::DrumGridPlugin::baseNote);
            expect(chain != nullptr, "pad chain did not survive restore");
            if (chain != nullptr)
                expect(!chain->plugins.empty(), "pad lost its plugin on restore");
        }

        restored->deleteFromParent();
    }

    void testBinaryPropertySurvives(te::Edit& edit) {
        beginTest("Binary device properties survive capture and restore");

        // The IR device keeps its audio in a binary property. JUCE's JSON writer
        // has no binary case, so an untagged encode emits unquoted base64 and the
        // document stops parsing entirely - the device would lose all its state.
        auto plugin = createPlugin(edit, te::ImpulseResponsePlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        juce::MemoryBlock payload;
        for (int i = 0; i < 1024; ++i) {
            const auto byte = static_cast<char>(i & 0xff);
            payload.append(&byte, 1);
        }
        plugin->state.setProperty(te::IDs::irFileData, juce::var(payload), nullptr);

        const auto state = ta::captureInternalDeviceState(*plugin);
        plugin->deleteFromParent();

        const auto doc = ds::decode(state);
        expect(doc.has_value(), "document with a binary property failed to parse back");
        if (!doc)
            return;

        const auto* captured = doc->root.props["irFileData"].getBinaryData();
        expect(captured != nullptr, "binary property did not decode as binary");
        if (captured != nullptr)
            expect(*captured == payload, "binary property changed through the round-trip");

        auto restoredTree = ta::devicePluginTreeFromState(state);
        expect(restoredTree.isValid());
        const auto* restoredBlock = restoredTree.getProperty(te::IDs::irFileData).getBinaryData();
        expect(restoredBlock != nullptr, "restored tree lost the binary property");
        if (restoredBlock != nullptr)
            expect(*restoredBlock == payload, "restored binary property does not match");
    }

    void testLegacyStateStillLoads(te::Edit& edit) {
        beginTest("Pre-v2 engine XML still restores");

        // Shaped exactly like state saved by an older build: engine container,
        // engine object id, engine flags, plus a modifier assignment child.
        const juce::String legacy = juce::String("<PLUGIN type=\"") +
                                    audio::ArpeggiatorPlugin::xmlTypeName +
                                    "\" id=\"1042\" enabled=\"1\" arpQuantizeSub=\"8\" "
                                    "arpHardAngle=\"1\">"
                                    "<MODIFIERASSIGNMENTS><LFO source=\"9\" paramID=\"gate\" "
                                    "value=\"0.5\"/></MODIFIERASSIGNMENTS>"
                                    "</PLUGIN>";

        auto tree = ta::devicePluginTreeFromState(legacy);
        expect(tree.isValid(), "legacy state failed to parse");
        if (!tree.isValid())
            return;

        expect(!tree.hasProperty(te::IDs::id), "legacy engine object id was not stripped");
        expect(!tree.getChildWithName(te::IDs::MODIFIERASSIGNMENTS).isValid(),
               "legacy modifier assignments were not stripped");

        auto plugin = edit.getPluginCache().createNewPlugin(tree);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        auto* arp = dynamic_cast<audio::ArpeggiatorPlugin*>(plugin.get());
        expect(arp != nullptr);
        if (arp != nullptr) {
            expectEquals(arp->quantizeSub.get(), 8);
            expect(arp->hardAngle.get());
        }

        plugin->deleteFromParent();
    }
};

DeviceStateSchemaTest deviceStateSchemaTest;

}  // namespace
