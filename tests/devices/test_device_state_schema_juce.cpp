#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "PadSyncTestSupport.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/DeviceServices.hpp"
#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/MagdaConvolutionPlugin.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/StepSequencerPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "magda/daw/audio/processors/base/MagdaDeviceProcessor.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
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
        testModelParametersRestoreThroughWrapper(*edit);
        testAuthoredStateProjectionLeavesParametersAlone(*edit);
        testNestedDeviceSurvives(*edit);
        testBinaryPropertySurvives(*edit);
        testFutureStateIsNotOverwritten(*edit);
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

        const auto state = ta::captureInternalDeviceState(*plugin, {});
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

        // #2317: the document carries NO parameter record. DeviceInfo::parameters
        // is the sole persisted authority for automatable parameters.
        expect(doc->params.empty(), "capture wrote a duplicate parameter record");

        // Engine-owned plugin-state facts must not survive into the document:
        // MAGDA owns each of them in DeviceInfo.
        for (const auto* engineOwned : {"id", "type", "enabled", "process", "windowX"})
            expect(!doc->root.props.contains(juce::Identifier(engineOwned)),
                   juce::String("engine-owned property leaked into the document: ") + engineOwned);

        // The engine's per-parameter properties must not sneak back in as root
        // props now that the parameter record is gone.
        for (auto* param : plugin->getAutomatableParameters())
            expect(!doc->root.props.contains(juce::Identifier(param->paramID)),
                   "engine parameter property leaked into the document: " + param->paramID);

        plugin->deleteFromParent();
    }

    void testRoundTrip(te::Edit& edit) {
        beginTest("Device state round-trips through capture and restore");

        auto source = createPlugin(edit, audio::ArpeggiatorPlugin::xmlTypeName);
        auto* sourceArp = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(source.get());
        expect(sourceArp != nullptr);
        if (sourceArp == nullptr)
            return;

        // Authored (non-parameter) settings: the document's whole payload
        // since #2317. The parameter is moved too, to prove it does NOT travel
        // through the document.
        sourceArp->quantizeSub = 8;
        sourceArp->hardAngle = true;
        if (auto gate = source->getAutomatableParameterByID("gate"))
            gate->setParameterFromHost(0.42f, juce::sendNotificationSync);

        const auto state = ta::captureInternalDeviceState(*source, {});
        source->deleteFromParent();

        auto restoredTree = ta::devicePluginTreeFromState(state);
        expect(restoredTree.isValid());
        expectEquals(restoredTree.getProperty(te::IDs::type).toString(),
                     juce::String(audio::ArpeggiatorPlugin::xmlTypeName));

        auto restored = edit.getPluginCache().createNewPlugin(restoredTree);
        expect(restored != nullptr);
        if (restored == nullptr)
            return;

        auto* restoredArp = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(restored.get());
        expect(restoredArp != nullptr);
        if (restoredArp != nullptr) {
            expectEquals(restoredArp->quantizeSub.load(), 8);
            expect(restoredArp->hardAngle.load());
        }

        // The moved parameter did NOT come back from the document - parameters
        // restore from DeviceInfo::parameters alone (seated by
        // syncFromDeviceInfo; the model-authority test below covers it).
        if (auto gate = restored->getAutomatableParameterByID("gate"))
            expect(std::abs(gate->getCurrentBaseValue() - 0.42f) > 0.001f,
                   "a parameter value travelled through the state document");
        else
            expect(false, "restored plugin lost its 'gate' parameter");

        restored->deleteFromParent();
    }

    // DeviceInfo::parameters is the sole restore source for automatable
    // parameters (#2317), in display units. Restoring means seating the model
    // array through the processor; the wrapper's normalised slots receive the
    // converted values, and a recapture writes no parameter record back.
    void testModelParametersRestoreThroughWrapper(te::Edit& edit) {
        beginTest("Model parameters restore through the wrapper in display units");

        auto plugin = createPlugin(edit, audio::ArpeggiatorPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        auto* device = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(plugin.get());
        expect(device != nullptr);
        if (device == nullptr) {
            plugin->deleteFromParent();
            return;
        }

        magda::MagdaDeviceProcessor processor(9001, plugin);
        magda::DeviceInfo info;
        info.format = magda::PluginFormat::Internal;
        info.pluginId = audio::ArpeggiatorPlugin::xmlTypeName;
        processor.populateParameters(info, magda::DeviceProcessor::ValueSource::Engine);

        auto* fixedVel = info.findParameterByIndex(audio::ArpeggiatorPlugin::kFixedVel);
        auto* gate = info.findParameterByIndex(audio::ArpeggiatorPlugin::kGate);
        expect(fixedVel != nullptr && gate != nullptr);
        if (fixedVel == nullptr || gate == nullptr) {
            plugin->deleteFromParent();
            return;
        }
        fixedVel->currentValue = 100.0f;  // display units: a velocity, not a fraction
        gate->currentValue = 0.8f;

        processor.syncFromDeviceInfo(info);

        const auto displayOf = [&](int slot, const char* id) {
            auto param = plugin->getAutomatableParameterByID(id);
            expect(param != nullptr);
            return param != nullptr ? magda::ParameterUtils::normalizedToReal(
                                          param->getCurrentBaseValue(), device->parameterInfo(slot))
                                    : -1.0f;
        };
        expectWithinAbsoluteError(displayOf(audio::ArpeggiatorPlugin::kFixedVel, "fixedvel"),
                                  100.0f, 0.5f);
        expectWithinAbsoluteError(displayOf(audio::ArpeggiatorPlugin::kGate, "gate"), 0.8f, 0.005f);

        // Save writes no second copy of what the model just seated...
        const auto recaptured = ds::decode(ta::captureInternalDeviceState(*plugin, {}));
        expect(recaptured.has_value());
        if (recaptured)
            expect(recaptured->params.empty(), "recapture wrote a duplicate parameter record");

        // ...and the model-first refresh keeps the model's values rather
        // than reading them back off the engine.
        processor.populateParameters(info, magda::DeviceProcessor::ValueSource::Model);
        auto* refreshed = info.findParameterByIndex(audio::ArpeggiatorPlugin::kFixedVel);
        expect(refreshed != nullptr);
        if (refreshed != nullptr)
            expectWithinAbsoluteError(refreshed->currentValue, 100.0f, 0.001f);

        plugin->deleteFromParent();
    }

    // An authored-state edit projects the model's document into the running
    // device (#2317), and that document carries no parameters at all - the
    // model owns them and seats them itself. The projection therefore has to
    // leave them alone: treating an absent property as "reset this slot" wiped
    // every parameter out of the plugin's own state on each pattern edit
    // (#2335).
    void testAuthoredStateProjectionLeavesParametersAlone(te::Edit& edit) {
        beginTest("An authored-state projection leaves the parameters alone");

        auto plugin = createPlugin(edit, audio::StepSequencerPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        auto* device = ta::deviceFromPlugin<audio::StepSequencerPlugin>(plugin.get());
        expect(device != nullptr);
        if (device == nullptr) {
            plugin->deleteFromParent();
            return;
        }

        // Rate to 1/4 and Gate to 30%, well away from their defaults of 1/16
        // and 80%.
        const auto setSlot = [&](int slot, const char* id, float displayValue) {
            auto param = plugin->getAutomatableParameterByID(id);
            expect(param != nullptr, juce::String("no parameter ") + id);
            if (param != nullptr)
                param->setParameterFromHost(magda::ParameterUtils::realToNormalized(
                                                displayValue, device->parameterInfo(slot)),
                                            juce::sendNotificationSync);
        };
        setSlot(audio::StepSequencerPlugin::kRate, "rate", 1.0f);  // 1/4
        setSlot(audio::StepSequencerPlugin::kGateLength, "gatelength", 0.3f);
        expect(plugin->state.hasProperty("rate"));
        const auto rateBefore = static_cast<float>(plugin->state["rate"]);
        const auto gateBefore = static_cast<float>(plugin->state["gatelength"]);

        // What a step toggled in the faceplate projects: the authored document
        // and nothing else.
        ds::Doc doc;
        doc.deviceType = audio::StepSequencerPlugin::xmlTypeName;
        doc.root.props.set(audio::StepSequencerPlugin::SettingIDs::numSteps, 8);
        auto tree = ta::devicePluginTreeFromState(ds::encode(doc));
        expect(tree.isValid());
        plugin->restorePluginStateFromValueTree(tree);

        // The slots the projection said nothing about are still there, and
        // still hold what the model put in them. Before the fix the properties
        // were removed outright, so a save or a rebuild from this tree brought
        // the device back on its factory defaults.
        expect(plugin->state.hasProperty("rate"), "the projection wiped the rate property");
        expect(plugin->state.hasProperty("gatelength"),
               "the projection wiped the gate length property");
        expectWithinAbsoluteError(static_cast<float>(plugin->state["rate"]), rateBefore, 0.001f);
        expectWithinAbsoluteError(static_cast<float>(plugin->state["gatelength"]), gateBefore,
                                  0.001f);

        // The live parameter and the device agree with them.
        const auto displayOf = [&](int slot, const char* id) {
            auto param = plugin->getAutomatableParameterByID(id);
            expect(param != nullptr);
            return param != nullptr ? magda::ParameterUtils::normalizedToReal(
                                          param->getCurrentBaseValue(), device->parameterInfo(slot))
                                    : -1.0f;
        };
        expectWithinAbsoluteError(displayOf(audio::StepSequencerPlugin::kRate, "rate"), 1.0f,
                                  0.01f);
        expectWithinAbsoluteError(displayOf(audio::StepSequencerPlugin::kGateLength, "gatelength"),
                                  0.3f, 0.005f);

        // And the authored state DID land.
        expectEquals(device->pattern().playingLength(), 8);

        plugin->deleteFromParent();
    }

    void testNestedDeviceSurvives(te::Edit& edit) {
        beginTest("A Drum Grid's document carries its own properties and not its pads");

        // The pads are model state (#2207): the device's saved document is its
        // own properties alone. A second copy inside it is what the ownership
        // flip ends, and it is also what Tracktion's graph builder used to be
        // handed as nested plugins.
        auto source = createPlugin(edit, audio::DrumGridPlugin::xmlTypeName);
        auto* sourceGrid = dynamic_cast<audio::DrumGridPlugin*>(source.get());
        expect(sourceGrid != nullptr);
        if (sourceGrid == nullptr)
            return;

        magda::DeviceInfo gridDevice;
        gridDevice.id = 1;
        gridDevice.pluginId = audio::DrumGridPlugin::xmlTypeName;
        magda::test::setPadDevice(gridDevice, *sourceGrid, 0,
                                  magda::test::padDeviceFor("magda_kick", 100), edit);

        const auto* chain = sourceGrid->getChainForNote(audio::DrumGridPlugin::baseNote);
        expect(chain != nullptr, "pad chain was not created");
        expect(chain != nullptr && !chain->plugins.empty(), "pad has no plugin");

        const auto state = ta::captureInternalDeviceState(*source, {});
        source->deleteFromParent();

        const auto doc = ds::decode(state);
        expect(doc.has_value());
        if (!doc)
            return;

        bool sawPadNode = false;
        ds::forEachNode(doc->root, [&](const ds::Node& node) {
            if (node.type == "PLUGIN" || node.type == "CHAIN")
                sawPadNode = true;
        });
        expect(!sawPadNode, "the captured document carries a second copy of the pads");
        expect(!state.contains("magda_kick"), "the captured document names a pad's device");
    }

    void testBinaryPropertySurvives(te::Edit& edit) {
        beginTest("Binary device properties survive capture and restore");

        // The IR device keeps its audio in a binary property. JUCE's JSON writer
        // has no binary case, so an untagged encode emits unquoted base64 and the
        // document stops parsing entirely - the device would lose all its state.
        // A neutral property name: `irFileData` itself is owned exactly by the
        // device since the absent-means-none contract (flushState removes it
        // when no IR is held), so a blob parked there behind the device's back
        // no longer survives - any other binary property still does.
        auto plugin = createPlugin(edit, audio::MagdaConvolutionPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        juce::MemoryBlock payload;
        for (int i = 0; i < 1024; ++i) {
            const auto byte = static_cast<char>(i & 0xff);
            payload.append(&byte, 1);
        }
        static const juce::Identifier kBlobProp("testBinaryBlob");
        plugin->state.setProperty(kBlobProp, juce::var(payload), nullptr);

        const auto state = ta::captureInternalDeviceState(*plugin, {});
        plugin->deleteFromParent();

        const auto doc = ds::decode(state);
        expect(doc.has_value(), "document with a binary property failed to parse back");
        if (!doc)
            return;

        const auto* captured = doc->root.props["testBinaryBlob"].getBinaryData();
        expect(captured != nullptr, "binary property did not decode as binary");
        if (captured != nullptr)
            expect(*captured == payload, "binary property changed through the round-trip");

        auto restoredTree = ta::devicePluginTreeFromState(state);
        expect(restoredTree.isValid());
        const auto* restoredBlock =
            restoredTree.getProperty(juce::Identifier("testBinaryBlob")).getBinaryData();
        expect(restoredBlock != nullptr, "restored tree lost the binary property");
        if (restoredBlock != nullptr)
            expect(*restoredBlock == payload, "restored binary property does not match");
    }

    void testFutureStateIsNotOverwritten(te::Edit& edit) {
        beginTest("Device state from a newer schema is preserved, not downgraded");

        // A project written by a future build. decode refuses it, so the device
        // loads its defaults; capture must then hand the document straight back
        // rather than replace it with what those defaults produce. Otherwise
        // opening and saving here destroys the newer state permanently.
        const juce::String futureState =
            juce::String("{\"schema\": 99, \"device\": \"") +
            audio::ArpeggiatorPlugin::xmlTypeName +
            "\", \"props\": {\"arpQuantizeSub\": 8}, \"somethingNewer\": true}";

        expect(ds::isFutureDeviceState(futureState));
        expect(!ds::decode(futureState).has_value(), "a future document must not decode");

        auto plugin = createPlugin(edit, audio::ArpeggiatorPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        const auto captured = ta::captureInternalDeviceState(*plugin, futureState);
        expectEquals(captured, futureState, "capture overwrote state it could not read");

        // The guard must be narrow: ordinary saves still have to update state.
        const auto readable = ta::captureInternalDeviceState(*plugin, {});
        const auto recaptured = ta::captureInternalDeviceState(*plugin, readable);
        expect(ds::isDeviceStateV2(recaptured), "capture stopped updating readable state");

        plugin->deleteFromParent();
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

        auto* arp = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(plugin.get());
        expect(arp != nullptr);
        if (arp != nullptr) {
            expectEquals(arp->quantizeSub.load(), 8);
            expect(arp->hardAngle.load());
        }

        plugin->deleteFromParent();
    }
};

DeviceStateSchemaTest deviceStateSchemaTest;

}  // namespace
