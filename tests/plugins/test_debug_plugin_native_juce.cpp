// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// The debug plugin through the MAGDA engine's loader (#3155).

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

#include "DebugPluginHost.hpp"
#include "magda/daw/engine/host/ExternalPluginLoader.hpp"

using namespace magda;

class DebugPluginNativeTest final : public juce::UnitTest {
  public:
    DebugPluginNativeTest() : juce::UnitTest("Debug Plugin Native Engine Tests", "magda") {}

    void runTest() override {
        beginTest("MAGDA engine: a second instance the plugin refuses is reported as failed");

        const auto description = test::debugPluginDescription();
        expect(description.has_value());
        if (!description.has_value())
            return;

        test::ScopedDebugPluginFaults faults("max-instances=1");

        juce::AudioPluginFormatManager formats;
        formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
        juce::KnownPluginList known;
        known.addType(*description);

        std::map<engine::DeviceKey, DeviceInfo> devices{
            {{ChainSegment::Fx, 1}, test::debugPluginDevice(*description, 1)},
            {{ChainSegment::Fx, 2}, test::debugPluginDevice(*description, 2)}};

        int loaded = 0;
        std::vector<engine::DeviceKey> failed;
        daw::engine_host::ExternalPluginLoader loader(
            [&devices](engine::DeviceKey key) {
                const auto found = devices.find(key);
                return found != devices.end() ? &found->second : nullptr;
            },
            [&loaded](engine::DeviceKey, const DeviceInfo&, const std::vector<RestoredParameter>&) {
                ++loaded;
            });
        loader.onFailed(
            [&failed](engine::DeviceKey key, const juce::String&) { failed.push_back(key); });

        loader.setServices(&formats, &known);
        loader.setContext({.sampleRate = 48000.0, .maxBlockSize = 512, .numChannels = 2});
        loader.syncAssignments(devices);

        for (const auto& [key, device] : devices)
            loader.device(key, device);
        for (int turn = 0; turn < 20 && loaded + static_cast<int>(failed.size()) < 2; ++turn)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

        expectEquals(loaded, 1);
        expectEquals(static_cast<int>(failed.size()), 1);
        expectEquals(static_cast<int>(loader.held()), 1);
    }
};

static DebugPluginNativeTest debugPluginNativeTest;
