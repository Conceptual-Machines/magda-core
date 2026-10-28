// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "DebugPluginFaults.hpp"

namespace magda::debug_plugin {

namespace {

class DebugPluginEditor final : public juce::AudioProcessorEditor {
  public:
    DebugPluginEditor(juce::AudioProcessor& processor, juce::String faults)
        : juce::AudioProcessorEditor(processor), faults_(std::move(faults)) {
        setSize(360, 120);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::darkslategrey);
        g.setColour(juce::Colours::white);
        g.setFont(18.0f);
        g.drawText("MAGDA Debug Plugin", getLocalBounds().removeFromTop(60),
                   juce::Justification::centred);
        g.setFont(14.0f);
        g.drawText(faults_, getLocalBounds().removeFromBottom(60), juce::Justification::centred);
    }

  private:
    juce::String faults_;
};

/// A sine on the last held note, so a host can tell an instance that plays from one that
/// doesn't.
class DebugPluginProcessor final : public juce::AudioProcessor {
  public:
    DebugPluginProcessor()
        : juce::AudioProcessor(
              BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          faults_(Faults::fromEnvironment()) {
        ++liveInstances();
        if (faults_.slowLoadMs > 0)
            juce::Thread::sleep(faults_.slowLoadMs);
    }

    ~DebugPluginProcessor() override {
        --liveInstances();
    }

    const juce::String getName() const override {
        return "MAGDA Debug Plugin";
    }

    void prepareToPlay(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        phase_ = 0.0;
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (faults_.latencyChange > 0 && getLatencySamples() != faults_.latencyChange)
            setLatencySamples(faults_.latencyChange);

        buffer.clear();

        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
                note_ = message.getNoteNumber();
            else if (message.isNoteOff() && message.getNoteNumber() == note_)
                note_ = -1;
        }

        if (faults_.silent || note_ < 0)
            return;

        const auto increment = juce::MathConstants<double>::twoPi *
                               juce::MidiMessage::getMidiNoteInHertz(note_) / sampleRate_;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const auto value = static_cast<float>(0.25 * std::sin(phase_));
            phase_ = std::fmod(phase_ + increment, juce::MathConstants<double>::twoPi);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample(channel, sample, value);
        }
    }

    bool hasEditor() const override {
        return !faults_.noEditor;
    }

    juce::AudioProcessorEditor* createEditor() override {
        // Manual runs only: the point is the host's crash path.
        if (faults_.crashOnEditor)
            std::abort();
        return new DebugPluginEditor(*this, faults_.describe());
    }

    bool acceptsMidi() const override {
        return true;
    }
    bool producesMidi() const override {
        return false;
    }
    double getTailLengthSeconds() const override {
        return 0.0;
    }

    int getNumPrograms() override {
        return 1;
    }
    int getCurrentProgram() override {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {
        return {};
    }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override {
        destination.replaceAll("debug", 5);
    }

    void setStateInformation(const void*, int) override {
        if (faults_.throwOnState)
            throw std::runtime_error("MAGDA Debug Plugin: throw-on-state");
    }

  private:
    const Faults faults_;
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    int note_ = -1;
};

}  // namespace

}  // namespace magda::debug_plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new magda::debug_plugin::DebugPluginProcessor();
}
