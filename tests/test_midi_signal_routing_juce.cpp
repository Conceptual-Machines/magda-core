#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/DeviceMeteringManager.hpp"
#include "magda/daw/audio/plugins/InstrumentMeterTapPlugin.hpp"
#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/plugins/StepSequencerPlugin.hpp"
#include "magda/daw/audio/racks/InstrumentRackManager.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

namespace te = tracktion;

namespace {

te::Plugin::Ptr createCustomPlugin(te::Edit& edit, const juce::String& type) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, type, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

bool hasConnection(te::RackType& rackType, te::EditItemID src, int srcPin, te::EditItemID dst,
                   int dstPin) {
    for (auto* conn : rackType.getConnections()) {
        if (conn->sourceID.get() == src && conn->sourcePin.get() == srcPin &&
            conn->destID.get() == dst && conn->destPin.get() == dstPin) {
            return true;
        }
    }
    return false;
}

bool hasMidiOutputConnection(te::RackType& rackType) {
    for (auto* conn : rackType.getConnections()) {
        if (conn->destID.get().isInvalid() && conn->destPin.get() == 0)
            return true;
    }
    return false;
}

}  // namespace

class MidiSignalRoutingTest final : public juce::UnitTest {
  public:
    MidiSignalRoutingTest() : juce::UnitTest("MIDI Signal Routing Tests", "magda") {}

    void runTest() override {
        testInstrumentRackConsumesMidi();
        testStepSequencerDefaultsToReplacingMidi();
    }

  private:
    void testInstrumentRackConsumesMidi() {
        beginTest("Instrument wrapper passes audio through but consumes MIDI");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto instrument =
            createCustomPlugin(*edit, magda::daw::audio::MagdaSamplerPlugin::xmlTypeName);
        expect(instrument != nullptr, "Sampler plugin must be created");
        if (!instrument)
            return;

        magda::InstrumentRackManager rackManager(*edit);
        auto rackPlugin = rackManager.wrapInstrument(instrument);
        expect(rackPlugin != nullptr, "Instrument must be wrapped");

        auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
        expect(rackInstance != nullptr, "Wrapper plugin must be a RackInstance");
        if (rackInstance == nullptr || rackInstance->type == nullptr)
            return;

        auto& rackType = *rackInstance->type;
        const auto rackIO = te::EditItemID();
        const auto synthId = instrument->itemID;
        te::Plugin* meterTap = nullptr;
        for (auto* plugin : rackType.getPlugins()) {
            if (plugin && plugin->getPluginType() ==
                              magda::daw::audio::InstrumentMeterTapPlugin::xmlTypeName) {
                meterTap = plugin;
                break;
            }
        }
        expect(meterTap != nullptr, "Instrument wrapper must contain an explicit meter tap");
        if (meterTap == nullptr)
            return;
        const auto meterTapId = meterTap->itemID;

        expect(hasConnection(rackType, rackIO, 0, synthId, 0),
               "Rack MIDI input must feed the instrument");
        expect(!hasMidiOutputConnection(rackType),
               "Instrument wrapper must not expose MIDI at the rack output");

        expect(hasConnection(rackType, rackIO, 1, rackIO, 1),
               "Rack must preserve left audio passthrough");
        expect(hasConnection(rackType, rackIO, 2, rackIO, 2),
               "Rack must preserve right audio passthrough");
        expect(hasConnection(rackType, synthId, 1, meterTapId, 1),
               "Instrument left audio must feed the meter tap");
        expect(hasConnection(rackType, synthId, 2, meterTapId, 2),
               "Instrument right audio must feed the meter tap");
        expect(hasConnection(rackType, meterTapId, 1, rackIO, 1),
               "Meter tap left audio must feed rack output");
        expect(hasConnection(rackType, meterTapId, 2, rackIO, 2),
               "Meter tap right audio must feed rack output");

        constexpr magda::DeviceId deviceId = 4242;
        magda::DeviceMeteringManager metering;
        magda::DeviceMeteringManager::registerForEdit(*edit, &metering);

        rackManager.recordWrapping(deviceId, rackInstance->type, instrument, rackPlugin, false, 2);

        auto* typedTap = dynamic_cast<magda::daw::audio::InstrumentMeterTapPlugin*>(meterTap);
        expect(typedTap != nullptr, "Meter tap must be the MAGDA tap plugin");
        expect(typedTap == nullptr || typedTap->getDeviceId() == deviceId,
               "Meter tap must be bound to the wrapped device id");

        juce::AudioBuffer<float> buffer(2, 8);
        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, -0.25f);
        metering.setGain(deviceId, 0.5f);

        te::MidiMessageArray midi;
        te::PluginRenderContext rc(
            &buffer, juce::AudioChannelSet::canonicalChannelSet(2), 0, buffer.getNumSamples(),
            &midi, 0.0,
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0)),
            false, false, false, false);
        typedTap->applyToBuffer(rc);

        magda::DeviceMeteringManager::DeviceMeterData levels;
        metering.updateAllClients();
        expect(metering.getLatestLevels(deviceId, levels), "Meter levels should exist");
        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.25f, 0.0001f,
                                  "Meter tap should apply device gain to left channel");
        expectWithinAbsoluteError(buffer.getSample(1, 0), -0.125f, 0.0001f,
                                  "Meter tap should apply device gain to right channel");
        expectWithinAbsoluteError(levels.peakL, 0.25f, 0.0001f,
                                  "Meter tap should publish post-gain left peak");
        expectWithinAbsoluteError(levels.peakR, 0.125f, 0.0001f,
                                  "Meter tap should publish post-gain right peak");

        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, -0.25f);
        metering.clear();
        typedTap->applyToBuffer(rc);
        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.25f, 0.0001f,
                                  "Meter tap storage should survive manager entry removal");
        expectWithinAbsoluteError(buffer.getSample(1, 0), -0.125f, 0.0001f,
                                  "Meter tap storage should survive manager clear");

        typedTap->setDeviceId(magda::INVALID_DEVICE_ID);
        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, -0.25f);
        typedTap->applyToBuffer(rc);
        expectWithinAbsoluteError(buffer.getSample(0, 0), 0.5f, 0.0001f,
                                  "Unbound meter tap should pass audio through unchanged");
        expectWithinAbsoluteError(buffer.getSample(1, 0), -0.25f, 0.0001f,
                                  "Unbound meter tap should pass audio through unchanged");

        magda::DeviceMeteringManager::unregisterForEdit(*edit);
    }

    void testStepSequencerDefaultsToReplacingMidi() {
        beginTest("Step sequencer defaults to MIDI replace and restores MIDI thru state");

        auto& wrapper = magda::test::getSharedEngine();
        auto edit = te::test_utilities::createTestEdit(*wrapper.getEngine(), 1);
        expect(edit != nullptr, "Test edit must be created");
        if (!edit)
            return;

        auto plugin =
            createCustomPlugin(*edit, magda::daw::audio::StepSequencerPlugin::xmlTypeName);
        auto* seq = dynamic_cast<magda::daw::audio::StepSequencerPlugin*>(plugin.get());
        expect(seq != nullptr, "Step sequencer plugin must be created");
        if (seq == nullptr)
            return;

        expect(!seq->midiThru.get(), "Fresh step sequencers should block incoming MIDI");

        juce::ValueTree saved(te::IDs::PLUGIN);
        saved.setProperty(te::IDs::type, magda::daw::audio::StepSequencerPlugin::xmlTypeName,
                          nullptr);
        saved.setProperty("seqMidiThru", true, nullptr);
        seq->restorePluginStateFromValueTree(saved);

        expect(seq->midiThru.get(), "Saved MIDI thru state should be restored");
    }
};

static MidiSignalRoutingTest midiSignalRoutingTest;
