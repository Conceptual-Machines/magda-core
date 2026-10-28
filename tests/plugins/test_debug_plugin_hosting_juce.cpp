// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// The debug plugin through JUCE's real VST3 hosting (#3155).

#include <juce_audio_processors/juce_audio_processors.h>

#include "DebugPluginHost.hpp"

using namespace magda;

namespace {

std::unique_ptr<juce::AudioPluginInstance> open(juce::AudioPluginFormatManager& formats,
                                                const juce::PluginDescription& description,
                                                juce::String& error) {
    return formats.createPluginInstance(description, 48000.0, 512, error);
}

}  // namespace

class DebugPluginHostingTest final : public juce::UnitTest {
  public:
    DebugPluginHostingTest() : juce::UnitTest("Debug Plugin Hosting Tests", "magda") {}

    void runTest() override {
        const auto description = test::debugPluginDescription();
        beginTest("The debug plugin is found in the build tree");
        expect(description.has_value(), "no plugin at " + juce::String(MAGDA_DEBUG_PLUGIN_PATH));
        if (!description.has_value())
            return;

        formats_.addFormat(std::make_unique<juce::VST3PluginFormat>());

        testPlaysTheHeldNote(*description);
        testRefusesPastMaxInstances(*description);
    }

  private:
    void testPlaysTheHeldNote(const juce::PluginDescription& description) {
        beginTest("With no faults it plays the held note");

        juce::String error;
        auto instance = open(formats_, description, error);
        expect(instance != nullptr, error);
        if (instance == nullptr)
            return;

        instance->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        instance->processBlock(buffer, midi);

        expect(buffer.getMagnitude(0, 512) > 0.1f);
        expect(instance->hasEditor());
        instance->releaseResources();
    }

    void testRefusesPastMaxInstances(const juce::PluginDescription& description) {
        beginTest("max-instances refuses the instance past the limit, and only while it holds");

        test::ScopedDebugPluginFaults faults("max-instances=1");
        juce::String error;

        auto first = open(formats_, description, error);
        expect(first != nullptr, error);

        auto second = open(formats_, description, error);
        expect(second == nullptr);
        expect(error.isNotEmpty());

        first.reset();
        auto third = open(formats_, description, error);
        expect(third != nullptr, error);
    }

    juce::AudioPluginFormatManager formats_;
};

static DebugPluginHostingTest debugPluginHostingTest;
