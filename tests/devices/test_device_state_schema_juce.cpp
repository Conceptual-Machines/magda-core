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
#include "magda/daw/audio/plugins/SidechainPlugin.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
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
        testDisplayDomainDocsRestore(*edit);
        testParametersReseatByIdWhenIndexMoves(*edit);
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
        auto* sourceArp = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(source.get());
        expect(sourceArp != nullptr);
        if (sourceArp == nullptr)
            return;

        // Non-parameter property (no automatable parameter mirrors it) plus a
        // parameter, so both halves of the document are exercised.
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
        ta::applyDeviceStateParameters(*restored, state);

        auto* restoredArp = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(restored.get());
        expect(restoredArp != nullptr);
        if (restoredArp != nullptr) {
            expectEquals(restoredArp->quantizeSub.load(), 8);
            expect(restoredArp->hardAngle.load());
        }

        if (auto gate = restored->getAutomatableParameterByID("gate"))
            expectWithinAbsoluteError(gate->getCurrentBaseValue(), 0.42f, 0.001f);
        else
            expect(false, "restored plugin lost its 'gate' parameter");

        restored->deleteFromParent();
    }

    // Documents for the devices that crossed to MagdaDevice carry parameter
    // values in the device's display domain - which is what every document
    // captured from the retired display-ranged plugins already held. A released
    // project's arpeggiator doc says "fixed velocity 100", and applying it to
    // the wrapper's normalised slot has to land on 100, not clamp to the top of
    // [0,1] (#2312 review).
    void testDisplayDomainDocsRestore(te::Edit& edit) {
        beginTest("A display-domain document restores through the wrapper unclamped");

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

        ds::Doc doc;
        doc.deviceType = audio::ArpeggiatorPlugin::xmlTypeName;
        doc.params.push_back({audio::ArpeggiatorPlugin::kFixedVel, "fixedvel", 100.0f});
        doc.params.push_back({audio::ArpeggiatorPlugin::kGate, "gate", 0.8f});
        ta::applyDeviceStateParameters(*plugin, ds::encode(doc));

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

        // And the capture side writes display values back, marked as such, so
        // the document is stable across a save/load cycle.
        const auto recaptured = ds::decode(ta::captureInternalDeviceState(*plugin, {}));
        expect(recaptured.has_value());
        if (recaptured) {
            expect(recaptured->paramsAreDisplayDomain,
                   "wrapper capture did not mark the parameter domain");
            for (const auto& saved : recaptured->params)
                if (saved.id == "fixedvel")
                    expectWithinAbsoluteError(saved.value, 100.0f, 0.5f);
        }

        plugin->deleteFromParent();

        beginTest("An unmarked normalised document applies raw to an already-wrapped device");

        // The sidechain was behind the wrapper before the domain marker
        // existed, so its unmarked v2 documents hold normalised values and must
        // not be reinterpreted as display: a normalised attack of 0.5 means
        // half the knob (~25 ms), not 0.5 ms.
        auto sidechain = createPlugin(edit, audio::SidechainPlugin::xmlTypeName);
        expect(sidechain != nullptr);
        if (sidechain == nullptr)
            return;

        ds::Doc sidechainDoc;
        sidechainDoc.deviceType = audio::SidechainPlugin::xmlTypeName;
        sidechainDoc.params.push_back({audio::SidechainPlugin::kAttackParamIndex, "attack", 0.5f});
        expect(!ds::decode(ds::encode(sidechainDoc))->paramsAreDisplayDomain);
        ta::applyDeviceStateParameters(*sidechain, ds::encode(sidechainDoc));

        if (auto attack = sidechain->getAutomatableParameterByID("attack"))
            expectWithinAbsoluteError(attack->getCurrentBaseValue(), 0.5f, 0.001f);
        else
            expect(false, "sidechain lost its 'attack' parameter");

        beginTest("A v0.19 display-domain sidechain document still restores in ms");

        // The other unmarked era: captured from the host-native sidechain,
        // whose parameters ran in display ranges. The release value of 15 ms is
        // what discriminates it - nothing normalised can sit outside [0, 1].
        auto* sidechainDevice = ta::deviceFromPlugin<audio::SidechainPlugin>(sidechain.get());
        expect(sidechainDevice != nullptr);
        if (sidechainDevice == nullptr) {
            sidechain->deleteFromParent();
            return;
        }

        ds::Doc releasedDoc;
        releasedDoc.deviceType = audio::SidechainPlugin::xmlTypeName;
        releasedDoc.params.push_back({audio::SidechainPlugin::kGainParamIndex, "gain", 0.8f});
        releasedDoc.params.push_back({audio::SidechainPlugin::kAttackParamIndex, "attack", 1.0f});
        releasedDoc.params.push_back(
            {audio::SidechainPlugin::kReleaseParamIndex, "release", 15.0f});
        expect(!ds::decode(ds::encode(releasedDoc))->paramsAreDisplayDomain);
        ta::applyDeviceStateParameters(*sidechain, ds::encode(releasedDoc));

        const auto sidechainDisplay = [&](int slot, const char* id) {
            auto param = sidechain->getAutomatableParameterByID(id);
            expect(param != nullptr);
            return param != nullptr
                       ? magda::ParameterUtils::normalizedToReal(
                             param->getCurrentBaseValue(), sidechainDevice->parameterInfo(slot))
                       : -1.0f;
        };
        expectWithinAbsoluteError(
            sidechainDisplay(audio::SidechainPlugin::kReleaseParamIndex, "release"), 15.0f, 0.1f);
        expectWithinAbsoluteError(
            sidechainDisplay(audio::SidechainPlugin::kAttackParamIndex, "attack"), 1.0f, 0.05f);
        expectWithinAbsoluteError(sidechainDisplay(audio::SidechainPlugin::kGainParamIndex, "gain"),
                                  0.8f, 0.005f);

        sidechain->deleteFromParent();
    }

    // The frozen index is authoritative, and the stable id is what re-seats a
    // value if a device ever renumbers its parameters. Round-trip tests always
    // have the two agree, so drive them apart deliberately here.
    void testParametersReseatByIdWhenIndexMoves(te::Edit& edit) {
        beginTest("A saved parameter follows its id when the index no longer matches");

        auto plugin = createPlugin(edit, audio::ArpeggiatorPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        const auto& params = plugin->getAutomatableParameters();
        expect(params.size() >= 2, "need two parameters to swap between");
        if (params.size() < 2) {
            plugin->deleteFromParent();
            return;
        }

        auto gatePtr = plugin->getAutomatableParameterByID("gate");
        expect(gatePtr != nullptr, "arpeggiator lost its 'gate' parameter");
        if (gatePtr == nullptr) {
            plugin->deleteFromParent();
            return;
        }
        auto* gate = gatePtr.get();

        const int gateIndex = params.indexOf(gate);
        expect(gateIndex >= 0);
        // Any in-range index that is not gate's own, so the id has to override.
        const int wrongIndex = gateIndex == 0 ? 1 : 0;

        ds::Doc doc;
        doc.deviceType = audio::ArpeggiatorPlugin::xmlTypeName;
        doc.params.push_back({wrongIndex, "gate", 0.31f});
        // Also prove an out-of-range index (a parameter removed since the save)
        // still re-seats rather than being dropped or misapplied.
        doc.params.push_back({params.size() + 5, "gate", 0.77f});

        ta::applyDeviceStateParameters(*plugin, ds::encode(doc));

        // Doc values are display-domain for wrapper devices, so read the
        // restored value back through the same conversion.
        auto* gateDevice = ta::deviceFromPlugin<audio::ArpeggiatorPlugin>(plugin.get());
        expect(gateDevice != nullptr);
        if (gateDevice != nullptr)
            expectWithinAbsoluteError(
                magda::ParameterUtils::normalizedToReal(
                    gate->getCurrentBaseValue(),
                    gateDevice->parameterInfo(audio::ArpeggiatorPlugin::kGate)),
                0.77f, 0.001f);

        auto* other = params[wrongIndex];
        expect(other != nullptr);
        if (other != nullptr && other != gate)
            expect(std::abs(other->getCurrentBaseValue() - 0.31f) > 0.001f,
                   "the value landed on the parameter the stale index pointed at");

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
        auto plugin = createPlugin(edit, audio::MagdaConvolutionPlugin::xmlTypeName);
        expect(plugin != nullptr);
        if (plugin == nullptr)
            return;

        juce::MemoryBlock payload;
        for (int i = 0; i < 1024; ++i) {
            const auto byte = static_cast<char>(i & 0xff);
            payload.append(&byte, 1);
        }
        plugin->state.setProperty(te::IDs::irFileData, juce::var(payload), nullptr);

        const auto state = ta::captureInternalDeviceState(*plugin, {});
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
