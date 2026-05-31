#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
// Pulls in ExternalAutomatableParameter (used directly below) in the correct
// order behind the tracktion_engine umbrella. Do NOT include the internal
// tracktion_ExternalAutomatableParameter.h header directly here: it is not
// self-contained, and clang-format will sort it ahead of the umbrella, breaking
// the build.
#include "magda/daw/audio/plugin_manager/ExternalPluginStateUtil.hpp"
#include "magda/daw/audio/plugin_manager/PluginManager.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/ParameterInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"

namespace te = tracktion;

// ============================================================================
// External plugin state restore (#1361, fix/vst3-state-not-restored)
// ============================================================================
// For an external synth (VST3/AU) the entire voice lives in the native state
// chunk (DeviceInfo::pluginState). MAGDA also stores a per-parameter array,
// which is redundant and, when stale relative to the chunk, used to clobber the
// restored voice: loadDeviceAsPlugin/applyDevicePreset restored the chunk, then
// syncFromDeviceInfo re-applied the saved parameters and the playback-graph
// build wrote TE's stale AutomatableParameter cache back over the voice.
//
// The fix makes the chunk authoritative: after the param sync the chunk is
// re-asserted and TE's parameter cache is refreshed from the live plugin. The
// deterministic, non-flaky signature of the bug is therefore: after loading a
// device whose chunk and saved-parameter array disagree, TE's parameter cache
// must match the live plugin (== the chunk's voice), not the stale array.
//
// These tests need a real external INSTRUMENT in the scanned KnownPluginList.
// When none is available (clean CI without Dexed/any VST/AU synth) they skip.

namespace {

constexpr float kTol = 0.02f;

bool approx(float a, float b) {
    return std::abs(a - b) <= kTol;
}

// Find any external instrument in the scanned list (prefer Dexed for parity with
// the bug report). Returns true and fills `out` when one is found.
bool findExternalInstrument(te::Engine& engine, juce::PluginDescription& out) {
    bool haveFallback = false;
    for (const auto& d : engine.getPluginManager().knownPluginList.getTypes()) {
        if (!d.isInstrument)
            continue;
        if (d.pluginFormatName != "VST3" && d.pluginFormatName != "AudioUnit" &&
            d.pluginFormatName != "VST")
            continue;
        if (d.name.containsIgnoreCase("Dexed")) {
            out = d;
            return true;
        }
        if (!haveFallback) {
            out = d;
            haveFallback = true;
        }
    }
    return haveFallback;
}

magda::PluginFormat formatFor(const juce::PluginDescription& d) {
    if (d.pluginFormatName == "AudioUnit")
        return magda::PluginFormat::AU;
    if (d.pluginFormatName == "VST")
        return magda::PluginFormat::VST;
    return magda::PluginFormat::VST3;
}

magda::DeviceInfo deviceFor(const juce::PluginDescription& d) {
    magda::DeviceInfo dev;
    dev.name = d.name;
    dev.manufacturer = d.manufacturerName;
    dev.fileOrIdentifier = d.fileOrIdentifier;
    dev.uniqueId = d.createIdentifierString();
    dev.format = formatFor(d);
    dev.isInstrument = true;
    dev.deviceType = magda::DeviceType::Instrument;
    return dev;
}

}  // namespace

class ExternalPluginStateRestoreTest final : public juce::UnitTest {
  public:
    ExternalPluginStateRestoreTest()
        : juce::UnitTest("External Plugin State Restore Tests", "magda") {}

    // Spin the message loop until the (async-loaded) external plugin for `path`
    // is fully instantiated, or the timeout elapses.
    te::ExternalPlugin* waitForExternalPlugin(magda::PluginManager& pm,
                                              const magda::ChainNodePath& path, int timeoutMs) {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline) {
            if (auto p = pm.getPlugin(path))
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p.get()))
                    if (!ext->isInitialisingAsync() && ext->getAudioPluginInstance() != nullptr)
                        return ext;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
        }
        return nullptr;
    }

    te::ExternalAutomatableParameter* findExtParam(te::ExternalPlugin* ext, int paramIndex) {
        for (auto* p : ext->getAutomatableParameters())
            if (auto* ep = dynamic_cast<te::ExternalAutomatableParameter*>(p))
                if (ep->getParameterIndex() == paramIndex)
                    return ep;
        return nullptr;
    }

    // Capture the current normalized values of every automatable parameter, in
    // the shape syncFromDeviceInfo consumes (paramIndex + currentValue).
    std::vector<magda::ParameterInfo> captureParams(te::ExternalPlugin* ext) {
        std::vector<magda::ParameterInfo> out;
        if (auto* inst = ext->getAudioPluginInstance()) {
            auto& ps = inst->getParameters();
            for (int j = 0; j < ps.size(); ++j) {
                if (!ps[j]->isAutomatable())
                    continue;
                magda::ParameterInfo pi;
                pi.paramIndex = j;
                pi.currentValue = ps[j]->getValue();
                pi.minValue = 0.0f;
                pi.maxValue = 1.0f;
                pi.teMinValue = 0.0f;
                pi.teMaxValue = 1.0f;
                out.push_back(pi);
            }
        }
        return out;
    }

    // Pick an automatable parameter we can move clearly away from its default,
    // and apply that change to the live plugin. Returns {paramIndex, defaultVal,
    // changedVal} or paramIndex == -1 if none found.
    struct ParamChange {
        int paramIndex = -1;
        float defaultVal = 0.0f;
        float changedVal = 0.0f;
    };
    ParamChange makeDistinctVoice(te::ExternalPlugin* ext) {
        auto* inst = ext->getAudioPluginInstance();
        if (!inst)
            return {};
        auto& ps = inst->getParameters();
        for (int j = 0; j < ps.size(); ++j) {
            if (!ps[j]->isAutomatable())
                continue;
            auto* ep = findExtParam(ext, j);
            if (!ep)
                continue;
            const float d = ps[j]->getValue();
            const float target = d < 0.5f ? 0.9f : 0.1f;
            ep->setParameterFromHost(target, juce::sendNotificationSync);
            const float actual = ps[j]->getValue();
            if (std::abs(actual - d) > 0.1f)
                return {j, d, actual};
        }
        return {};
    }

    // Create a genuinely different voice by switching to another program (the
    // realistic "load a different patch" case, which reliably refreshes the live
    // edit buffer). Leaves the plugin on the new program. Returns a parameter that
    // differs clearly between the original and new program. paramIndex == -1 if
    // the instrument has too few programs or no clearly-different parameter.
    ParamChange makeProgramVoice(te::ExternalPlugin* ext) {
        auto* inst = ext->getAudioPluginInstance();
        if (!inst)
            return {};
        auto& ps = inst->getParameters();
        const int np = inst->getNumPrograms();
        if (np < 2)
            return {};
        const int orig = inst->getCurrentProgram();
        std::vector<float> base;
        for (auto* p : ps)
            base.push_back(p->getValue());
        for (int prog = 0; prog < np; ++prog) {
            if (prog == orig)
                continue;
            inst->setCurrentProgram(prog);
            for (int j = 0; j < ps.size(); ++j) {
                if (!ps[j]->isAutomatable())
                    continue;
                const float v = ps[j]->getValue();
                if (std::abs(v - base[j]) > 0.2f)
                    return {j, base[j], v};
            }
        }
        return {};
    }

    void runTest() override {
        testProjectReloadKeepsChunkVoice();
        testRackInstrumentKeepsChunkVoice();
        testDevicePresetKeepsChunkVoice();
    }

    // Load a fresh instance of `desc` on a throwaway track, capture its default
    // parameters, push it to a distinct voice, and return the resulting state
    // chunk. Removes the throwaway track before returning. Returns false if the
    // instrument could not be instantiated or no movable parameter was found.
    bool captureModifiedVoice(magda::TrackManager& tm, magda::AudioBridge* bridge,
                              magda::PluginManager& pm, const juce::PluginDescription& desc,
                              std::vector<magda::ParameterInfo>& outDefaults,
                              juce::String& outChunk, ParamChange& outChange) {
        const auto srcTrack = tm.createTrack("VoiceSrc");
        const auto srcDevId = tm.addDeviceToTrack(srcTrack, deviceFor(desc));
        const auto srcPath = magda::ChainNodePath::topLevelDevice(srcTrack, srcDevId);
        bridge->syncTrackPlugins(srcTrack);

        auto* src = waitForExternalPlugin(pm, srcPath, 15000);
        bool ok = false;
        if (src != nullptr) {
            outDefaults = captureParams(src);
            outChange = makeDistinctVoice(src);
            if (outChange.paramIndex >= 0) {
                src->flushPluginStateToValueTree();
                outChunk = src->state.getProperty(te::IDs::state).toString();
                ok = outChunk.isNotEmpty();
            }
        }

        // Drop the throwaway track so it can't interfere with the test track.
        for (const auto& t : tm.getTracks()) {
            if (t.id == srcTrack) {
                tm.deleteTrack(srcTrack);
                break;
            }
        }
        return ok;
    }

    // ----------------------------------------------------------------------
    // Project-load path: loadDeviceAsPlugin must let the chunk win over a stale
    // saved parameter array.
    // ----------------------------------------------------------------------
    void testProjectReloadKeepsChunkVoice() {
        beginTest("Project reload: chunk voice survives a stale parameter array");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        auto* engine = wrapper.getEngine();
        expect(bridge != nullptr && engine != nullptr, "Engine + bridge must exist");
        if (!bridge || !engine)
            return;

        juce::PluginDescription desc;
        if (!findExternalInstrument(*engine, desc)) {
            logMessage("SKIP: no external instrument in the scanned plugin list");
            return;
        }
        logMessage("Using external instrument: " + desc.name + " (" + desc.pluginFormatName + ")");

        auto& tm = magda::TrackManager::getInstance();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);
        auto& pm = bridge->getPluginManager();

        // 1. Load a fresh instance to capture the default voice + chunk for the
        //    chosen "modified" voice.
        const auto srcTrack = tm.createTrack("Src");
        const auto srcDevId = tm.addDeviceToTrack(srcTrack, deviceFor(desc));
        const auto srcPath = magda::ChainNodePath::topLevelDevice(srcTrack, srcDevId);
        bridge->syncTrackPlugins(srcTrack);

        auto* src = waitForExternalPlugin(pm, srcPath, 15000);
        if (src == nullptr) {
            logMessage("SKIP: instrument failed to instantiate in time");
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        const auto defaults = captureParams(src);
        const auto change = makeDistinctVoice(src);
        if (change.paramIndex < 0) {
            logMessage("SKIP: could not find a movable automatable parameter");
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        src->flushPluginStateToValueTree();
        const auto chunkVoiceB = src->state.getProperty(te::IDs::state).toString();
        expect(chunkVoiceB.isNotEmpty(), "Instrument must expose a state chunk");

        // 2. Build a "buggy saved project" device: chunk encodes voice B, but the
        //    saved parameter array holds the defaults (voice A). This is exactly
        //    the stale state a project saved by the pre-fix build contains.
        auto staleDevice = deviceFor(desc);
        staleDevice.parameters = defaults;
        staleDevice.pluginState = chunkVoiceB;

        // 3. Load it on a fresh track (the project-reload path).
        const auto dstTrack = tm.createTrack("Dst");
        const auto dstDevId = tm.addDeviceToTrack(dstTrack, staleDevice);
        const auto dstPath = magda::ChainNodePath::topLevelDevice(dstTrack, dstDevId);
        bridge->syncTrackPlugins(dstTrack);

        auto* dst = waitForExternalPlugin(pm, dstPath, 15000);
        expect(dst != nullptr, "Reloaded instrument must instantiate");
        if (dst == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        // Let any deferred graph build / param writeback run.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        auto* inst = dst->getAudioPluginInstance();
        expect(inst != nullptr, "Reloaded instance must exist");
        if (inst == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        const float pluginVal = inst->getParameters()[change.paramIndex]->getValue();
        auto* cacheParam = findExtParam(dst, change.paramIndex);
        expect(cacheParam != nullptr, "Reloaded TE automatable parameter must exist");
        const float cacheVal = cacheParam ? cacheParam->getCurrentValue() : -1.0f;

        // The chunk (voice B) must win over the stale default array (voice A)...
        expect(approx(pluginVal, change.changedVal),
               "Plugin parameter should match the chunk voice (" +
                   juce::String(change.changedVal, 3) + "), got " + juce::String(pluginVal, 3));
        // ...and TE's parameter cache must agree with the live plugin, so the
        // playback-graph build cannot later write a stale value back. This is the
        // assertion that fails on the unfixed sync load path.
        expect(approx(cacheVal, pluginVal), "TE parameter cache (" + juce::String(cacheVal, 3) +
                                                ") should match the live plugin (" +
                                                juce::String(pluginVal, 3) + ") after restore");
        expect(!approx(change.changedVal, change.defaultVal),
               "Sanity: modified voice must differ from the default");

        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
    }

    // ----------------------------------------------------------------------
    // Rack/master load path (createPluginOnly + registerRackPluginProcessor):
    // integration coverage. An external instrument inside a MAGDA rack must end
    // up on the chunk's voice with a consistent parameter cache, despite a stale
    // saved parameter array.
    //
    // NOTE: not a discriminating regression guard. Empirically the MAGDA-rack
    // graph ends up refreshing the inner plugin's parameter cache from the
    // restored chunk even without the registerRackPluginProcessor reassert, so
    // this passes with or without that fix (unlike the top-level reload test,
    // where the deferred graph writeback clobbers a stale cache). The reassert in
    // registerRackPluginProcessor is kept as a defensive mirror of the validated
    // top-level fix; this test guards against crashes and gross regressions in
    // the rack load path and documents the expected end state.
    // ----------------------------------------------------------------------
    void testRackInstrumentKeepsChunkVoice() {
        beginTest("Rack-hosted external instrument: chunk voice survives a stale parameter array");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        auto* engine = wrapper.getEngine();
        if (!bridge || !engine)
            return;

        juce::PluginDescription desc;
        if (!findExternalInstrument(*engine, desc)) {
            logMessage("SKIP: no external instrument in the scanned plugin list");
            return;
        }

        auto& tm = magda::TrackManager::getInstance();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);
        auto& pm = bridge->getPluginManager();

        // Capture a modified voice (chunk B) + the default-program parameter array.
        std::vector<magda::ParameterInfo> defaults;
        juce::String chunkVoiceB;
        ParamChange change;
        if (!captureModifiedVoice(tm, bridge, pm, desc, defaults, chunkVoiceB, change)) {
            logMessage("SKIP: could not capture a distinct voice");
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        // Build the "buggy saved project" device (chunk = voice B, params = stale
        // defaults) and place it inside a MAGDA rack chain.
        auto staleDevice = deviceFor(desc);
        staleDevice.parameters = defaults;
        staleDevice.pluginState = chunkVoiceB;

        const auto track = tm.createTrack("RackHost");
        const auto rackId = tm.addRackToTrack(track, "Rack");
        auto* rack = tm.getRack(track, rackId);
        expect(rack != nullptr && !rack->chains.empty(), "Rack with a default chain must exist");
        if (rack == nullptr || rack->chains.empty()) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        const auto chainId = rack->chains[0].id;
        const auto chainPath = magda::ChainNodePath::chain(track, rackId, chainId);
        const auto devId = tm.addDeviceToChainByPath(chainPath, staleDevice);
        const auto devPath = magda::ChainNodePath::chainDevice(track, rackId, chainId, devId);
        bridge->syncTrackPlugins(track);

        auto* dst = waitForExternalPlugin(pm, devPath, 15000);
        expect(dst != nullptr, "Rack-hosted instrument must instantiate");
        if (dst == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        auto* inst = dst->getAudioPluginInstance();
        if (inst == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        const float pluginVal = inst->getParameters()[change.paramIndex]->getValue();
        auto* cacheParam = findExtParam(dst, change.paramIndex);
        const float cacheVal = cacheParam ? cacheParam->getCurrentValue() : -1.0f;

        expect(approx(pluginVal, change.changedVal),
               "Rack plugin parameter should match the chunk voice (" +
                   juce::String(change.changedVal, 3) + "), got " + juce::String(pluginVal, 3));
        expect(approx(cacheVal, pluginVal), "TE parameter cache (" + juce::String(cacheVal, 3) +
                                                ") should match the live rack plugin (" +
                                                juce::String(pluginVal, 3) + ")");

        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
    }

    // ----------------------------------------------------------------------
    // Device-preset path (applyDevicePreset): integration/smoke coverage.
    //
    // NOTE: this exercises applyDevicePreset end-to-end with a real external
    // instrument and asserts the preset's voice is applied and that TE's
    // parameter cache stays consistent with the live plugin. It is NOT a
    // discriminating regression guard for the stale-parameter-array clobber:
    // a program-change preset is refreshed by TE's own setCurrentProgram path,
    // so it passes with or without the applyDevicePreset cache refresh. The
    // discriminating guard for the underlying bug is the project-reload test
    // above. Kept to catch crashes/asserts and gross regressions in the preset
    // path (the ".mgda preset" flow).
    // ----------------------------------------------------------------------
    void testDevicePresetKeepsChunkVoice() {
        beginTest("Device preset: applies voice and keeps parameter cache consistent");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);
        auto* bridge = wrapper.getAudioBridge();
        auto* engine = wrapper.getEngine();
        if (!bridge || !engine)
            return;

        juce::PluginDescription desc;
        if (!findExternalInstrument(*engine, desc)) {
            logMessage("SKIP: no external instrument in the scanned plugin list");
            return;
        }

        auto& tm = magda::TrackManager::getInstance();
        tm.clearAllTracks();
        tm.setAudioEngine(&wrapper);
        auto& pm = bridge->getPluginManager();

        const auto track = tm.createTrack("Preset Target");
        const auto devId = tm.addDeviceToTrack(track, deviceFor(desc));
        const auto path = magda::ChainNodePath::topLevelDevice(track, devId);
        bridge->syncTrackPlugins(track);

        auto* ext = waitForExternalPlugin(pm, path, 15000);
        if (ext == nullptr) {
            logMessage("SKIP: instrument failed to instantiate in time");
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }

        // Capture the default-program parameters, then switch to a different
        // program to get a genuinely different voice (a realistic preset).
        const auto defaults = captureParams(ext);
        const auto change = makeProgramVoice(ext);
        if (change.paramIndex < 0) {
            logMessage("SKIP: instrument has too few programs / no distinct voice");
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        ext->flushPluginStateToValueTree();
        const auto chunkVoiceB = ext->state.getProperty(te::IDs::state).toString();

        // Build the preset off the live device so identity (pluginId/format) matches,
        // then give it voice B's chunk but the stale default-program parameter array.
        // syncFromDeviceInfo would write those stale params over the restored voice;
        // the fix re-derives the parameters from the chunk so the voice wins.
        auto* liveInfo = tm.getDeviceInChainByPath(path);
        expect(liveInfo != nullptr, "Live device info must exist");
        if (!liveInfo) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        auto preset = *liveInfo;
        preset.parameters = defaults;
        preset.pluginState = chunkVoiceB;

        const bool applied = tm.applyDevicePreset(path, preset);
        expect(applied, "applyDevicePreset should succeed");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        auto* inst = ext->getAudioPluginInstance();
        if (inst == nullptr) {
            tm.clearAllTracks();
            tm.setAudioEngine(nullptr);
            return;
        }
        const float pluginVal = inst->getParameters()[change.paramIndex]->getValue();
        auto* cacheParam = findExtParam(ext, change.paramIndex);
        const float cacheVal = cacheParam ? cacheParam->getCurrentValue() : -1.0f;

        expect(approx(pluginVal, change.changedVal),
               "Preset plugin parameter should match the chunk voice (" +
                   juce::String(change.changedVal, 3) + "), got " + juce::String(pluginVal, 3));
        expect(approx(cacheVal, pluginVal), "TE parameter cache (" + juce::String(cacheVal, 3) +
                                                ") should match the live plugin (" +
                                                juce::String(pluginVal, 3) +
                                                ") after applying the preset");

        tm.clearAllTracks();
        tm.setAudioEngine(nullptr);
    }
};

static ExternalPluginStateRestoreTest externalPluginStateRestoreTest;
