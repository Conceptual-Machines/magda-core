#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/agents/coder_agent.hpp"
#include "magda/agents/compact_parser.hpp"
#include "magda/agents/dsl_interpreter.hpp"
#include "magda/agents/instruction_executor.hpp"
#include "magda/agents/internal_plugins.hpp"
#include "magda/agents/sound_design_agent.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

struct ScopedLiveTrackState {
    ScopedLiveTrackState() {
        UndoManager::getInstance().clearHistory();
        TrackManager::getInstance().clearAllTracks();
    }

    ~ScopedLiveTrackState() {
        UndoManager::getInstance().clearHistory();
        TrackManager::getInstance().clearAllTracks();
    }
};

const DeviceInfo* getOnlyDevice(TrackId trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (track == nullptr || track->chain.fxChainElements.size() != 1 ||
        !isDevice(track->chain.fxChainElements.front()))
        return nullptr;
    return &getDevice(track->chain.fxChainElements.front());
}

constexpr std::array<const char*, 6> kDocumentedCompatibilityAliases = {
    "eq", "pitch shift", "pitch_shift", "ir reverb", "ir_reverb", "4osc"};

}  // namespace

TEST_CASE("Agent device catalog covers addable registry devices", "[agents][devices][catalog]") {
    const auto& catalog = getInternalPlugins();
    REQUIRE_FALSE(catalog.empty());

    for (const auto* spec : daw::audio::getAllInternalPluginSpecs()) {
        if (spec == nullptr || !spec->canCreateOnTrack ||
            spec->createMode == daw::audio::InternalPluginCreateMode::Unsupported ||
            spec->kind == InternalDeviceKind::ExternalInsert)
            continue;

        INFO("addable native device: " << spec->displayName);
        const auto catalogEntry =
            std::find_if(catalog.begin(), catalog.end(), [spec](const auto& entry) {
                return entry.pluginId.equalsIgnoreCase(spec->pluginId);
            });
        REQUIRE(catalogEntry != catalog.end());

        const auto* resolved = lookupInternalPluginByAlias(catalogEntry->primaryAlias);
        REQUIRE(resolved != nullptr);
        CHECK(resolved->pluginId.equalsIgnoreCase(spec->pluginId));

        for (int i = 0; i < spec->loadAliasCount; ++i) {
            if (spec->loadAliases[i] == nullptr)
                continue;

            INFO("native load alias: " << spec->loadAliases[i]);
            CHECK(lookupInternalPluginByAlias(spec->loadAliases[i]) != nullptr);
        }
    }

    for (const auto* spec : daw::audio::compiled::getAllCompiledPluginSpecs()) {
        REQUIRE(spec != nullptr);
        INFO("compiled device: " << spec->displayName);
        const auto catalogEntry =
            std::find_if(catalog.begin(), catalog.end(), [spec](const auto& entry) {
                return entry.pluginId.equalsIgnoreCase(spec->pluginId);
            });
        REQUIRE(catalogEntry != catalog.end());

        const auto* resolved = lookupInternalPluginByAlias(catalogEntry->primaryAlias);
        REQUIRE(resolved != nullptr);
        CHECK(resolved->pluginId.equalsIgnoreCase(spec->pluginId));
    }

    CHECK(lookupInternalPluginByAlias("external_fx") != nullptr);
    const auto* externalInstrument = lookupInternalPluginByAlias("external_instrument");
    REQUIRE(externalInstrument != nullptr);
    CHECK(externalInstrument->deviceType == DeviceType::Instrument);
}

TEST_CASE("Agent device catalog accepts documented and human-friendly aliases",
          "[agents][devices][catalog]") {
    for (const auto* alias : kDocumentedCompatibilityAliases) {
        INFO("compatibility alias: " << alias);
        CHECK(lookupInternalPluginByAlias(alias) != nullptr);
    }
    CHECK(lookupInternalPluginByAlias("EQ") == lookupInternalPluginByAlias("eq"));

    const auto* fourOsc = lookupInternalPluginByAlias("4osc");
    REQUIRE(fourOsc != nullptr);
    CHECK(fourOsc->id == InternalPlugin::FourOsc);

    const auto description = getInternalPluginCatalogDescription();
    for (const auto& entry : getInternalPlugins()) {
        if (!entry.browserVisible)
            continue;

        INFO("documented catalog alias: " << entry.primaryAlias);
        CHECK(description.contains("[" + entry.primaryAlias + "]"));
        const auto* resolved = lookupInternalPluginByAlias(entry.primaryAlias);
        REQUIRE(resolved != nullptr);
        CHECK(resolved->pluginId.equalsIgnoreCase(entry.pluginId));
    }
}

TEST_CASE("Agent device capabilities declare UI and dedicated-agent routing",
          "[agents][devices][capabilities]") {
    for (const auto& entry : getInternalPlugins()) {
        INFO("catalog device: " << entry.pluginId);
        CHECK(entry.capabilities.addable);
        CHECK(entry.capabilities.automatable == !entry.pluginId.equalsIgnoreCase("insert"));
    }

    const auto& fourOsc = getInternalPluginCapabilities("4OSC");
    CHECK(fourOsc.soundDesignAgent == SoundDesignAgentKind::FourOsc);
    CHECK(fourOsc.supportsDeviceAI());
    CHECK(createSoundDesignAgentFor("4osc") != nullptr);

    const auto& step = getInternalPluginCapabilities("stepsequencer");
    CHECK(step.soundDesignAgent == SoundDesignAgentKind::StepSequencer);
    CHECK(createSoundDesignAgentFor("stepsequencer") != nullptr);

    const auto& poly = getInternalPluginCapabilities("polystepsequencer");
    CHECK(poly.soundDesignAgent == SoundDesignAgentKind::PolyStepSequencer);
    CHECK(createSoundDesignAgentFor("polystepsequencer") != nullptr);

    const auto& faust = getInternalPluginCapabilities("faust");
    CHECK(faust.coderAgent == CoderAgentKind::Faust);
    CHECK(createCoderAgentFor("faust") != nullptr);

    const auto& drumGrid = getInternalPluginCapabilities("drumgrid");
    CHECK(drumGrid.drumRoleProvider);
    CHECK_FALSE(drumGrid.supportsDeviceAI());

    const auto& eq = getInternalPluginCapabilities("magda_eq");
    CHECK_FALSE(eq.parameterAliases.empty());

    const auto& unsupported = getInternalPluginCapabilities("third.party.unknown");
    CHECK_FALSE(unsupported.addable);
    CHECK_FALSE(unsupported.automatable);
    CHECK_FALSE(unsupported.drumRoleProvider);
    CHECK_FALSE(unsupported.supportsDeviceAI());
    CHECK(createDeviceAIAgentFor("third.party.unknown") == nullptr);

    DeviceInfo external;
    external.name = "Third Party Synth";
    external.pluginId = "third.party.synth";
    external.uniqueId = "VST3-third-party-synth";
    external.format = PluginFormat::VST3;
    CHECK_FALSE(isSoundDesignSupported(external));
    CHECK(createSoundDesignAgentFor(external) == nullptr);

    external.aiSoundDesignerParameters = {1, 4, 7};
    external.aiSoundDesignerPrompt = "Use oscillator 2 only for subtle detuning.";
    CHECK(isSoundDesignSupported(external));
    CHECK(isDeviceAISupported(external));
    CHECK(createSoundDesignAgentFor(external) != nullptr);
    CHECK(createDeviceAIAgentFor(external) != nullptr);
}

TEST_CASE("Command state exposes bounded selected-track and selected-device context",
          "[agents][devices][context]") {
    test::MockMagdaApi api;
    TrackInfo track;
    track.id = 42;
    track.name = "Selected Synth";
    track.type = TrackType::Audio;

    DeviceInfo synth;
    synth.id = 7;
    synth.name = "4OSC Synth";
    synth.pluginId = "4osc";
    synth.deviceType = DeviceType::Instrument;
    synth.isInstrument = true;
    for (int i = 0; i < 30; ++i) {
        ParameterInfo parameter;
        parameter.paramIndex = i;
        parameter.name = "Parameter " + juce::String(i);
        parameter.unit = i == 0 ? "Hz" : "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 100.0f;
        parameter.currentValue = static_cast<float>(i);
        synth.parameters.push_back(parameter);
    }
    track.chain.fxChainElements.push_back(makeDeviceElement(synth));

    RackInfo rack;
    rack.id = 11;
    rack.name = "Parallel";
    ChainInfo chain;
    chain.id = 12;
    chain.name = "Wet";
    DeviceInfo reverb;
    reverb.id = 8;
    reverb.name = "Reverb";
    reverb.pluginId = "magda_reverb";
    chain.elements.push_back(makeDeviceElement(reverb));
    rack.chains.push_back(std::move(chain));
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    api.tracks_.tracks.push_back(std::move(track));
    api.selection_.selectedTrack = 42;
    api.selection_.selectedChainNode = ChainNodePath::topLevelDevice(42, 7);
    dsl::Interpreter::setContextEnabled(true);

    const auto state = juce::JSON::parse(dsl::Interpreter::buildStateSnapshot(api));
    const auto* root = state.getDynamicObject();
    REQUIRE(root != nullptr);
    CHECK(static_cast<int>(root->getProperty("selected_track_id")) == 1);

    const auto* selectedTrack = root->getProperty("selected_track").getDynamicObject();
    REQUIRE(selectedTrack != nullptr);
    CHECK(static_cast<int>(selectedTrack->getProperty("model_id")) == 42);
    const auto* devices = selectedTrack->getProperty("devices").getArray();
    REQUIRE(devices != nullptr);
    REQUIRE(devices->size() == 2);
    CHECK((*devices)[1].getDynamicObject()->getProperty("path").toString().contains("Rack[11]"));

    const auto* selectedDevice = root->getProperty("selected_device").getDynamicObject();
    REQUIRE(selectedDevice != nullptr);
    CHECK(selectedDevice->getProperty("plugin_id").toString() == "4osc");
    CHECK(static_cast<bool>(selectedDevice->getProperty("parameters_truncated")));
    const auto* parameters = selectedDevice->getProperty("parameters").getArray();
    REQUIRE(parameters != nullptr);
    CHECK(parameters->size() == 24);

    const auto* capabilities = selectedDevice->getProperty("capabilities").getDynamicObject();
    REQUIRE(capabilities != nullptr);
    CHECK(static_cast<bool>(capabilities->getProperty("sound_design_agent")));
    CHECK_FALSE(static_cast<bool>(capabilities->getProperty("coder_agent")));
}

TEST_CASE("DSL fx.add executes documented internal-device compatibility aliases",
          "[agents][devices][aliases]") {
    ScopedLiveTrackState state;
    auto& tracks = TrackManager::getInstance();
    MagdaApiLive api;
    dsl::Interpreter interpreter(api);

    for (size_t i = 0; i < kDocumentedCompatibilityAliases.size(); ++i) {
        const auto* alias = kDocumentedCompatibilityAliases[i];
        const auto* expected = lookupInternalPluginByAlias(alias);
        REQUIRE(expected != nullptr);

        const auto trackId =
            tracks.createTrack("DSL alias " + juce::String(static_cast<int>(i)), TrackType::Audio);
        const auto command = "track(id=" + juce::String(static_cast<int>(i) + 1) +
                             ").fx.add(name=\"" + alias + "\")";

        INFO("DSL alias: " << alias);
        REQUIRE(interpreter.execute(command.toRawUTF8()));
        const auto* device = getOnlyDevice(trackId);
        REQUIRE(device != nullptr);
        CHECK(device->pluginId.equalsIgnoreCase(expected->pluginId));
    }
}

TEST_CASE("InstructionExecutor executes documented internal-device compatibility aliases",
          "[agents][devices][aliases][compact]") {
    ScopedLiveTrackState state;
    auto& tracks = TrackManager::getInstance();
    MagdaApiLive api;
    CompactParser parser;
    InstructionExecutor executor(api);

    for (size_t i = 0; i < kDocumentedCompatibilityAliases.size(); ++i) {
        const auto* alias = kDocumentedCompatibilityAliases[i];
        const auto* expected = lookupInternalPluginByAlias(alias);
        REQUIRE(expected != nullptr);

        const auto trackId = tracks.createTrack(
            "Compact alias " + juce::String(static_cast<int>(i)), TrackType::Audio);
        const auto compact = "FX " + juce::String(static_cast<int>(i) + 1) + " " + alias;
        const auto instructions = parser.parse(compact);

        INFO("compact alias: " << alias);
        REQUIRE(parser.getLastError().isEmpty());
        REQUIRE(executor.execute(instructions));
        const auto* device = getOnlyDevice(trackId);
        REQUIRE(device != nullptr);
        CHECK(device->pluginId.equalsIgnoreCase(expected->pluginId));
    }
}
