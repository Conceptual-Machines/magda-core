// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file speech_transcriber.cpp
 * @brief The whisper-free half of SpeechTranscriber, compiled in every build.
 */

#include "speech_transcriber.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

#include "../daw/core/AppPaths.hpp"
#include "../daw/core/Config.hpp"

namespace magda {

namespace {

constexpr const char* kModelName = "ggml-small.en-q5_1.bin";

/// Under the 8 kHz Nyquist of the resampled signal, with room for two biquads' roll-off.
constexpr double kAntiAliasHz = 7000.0;

}  // namespace

SpeechTranscriber& SpeechTranscriber::getInstance() {
    static SpeechTranscriber instance;
    return instance;
}

SpeechTranscriber::~SpeechTranscriber() {
    unload();
}

juce::File SpeechTranscriber::modelsDir() {
    if (const auto configured = Config::getInstance().getSpeechModelDir(); !configured.empty())
        return juce::File(juce::String(configured));
    return paths::dataDir().getChildFile("SpeechToText").getChildFile("models");
}

juce::File SpeechTranscriber::modelFile() {
    return modelsDir().getChildFile(kModelName);
}

juce::String SpeechTranscriber::modelUrl() {
    return juce::String("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/") + kModelName;
}

std::vector<float> SpeechTranscriber::toWhisperRate(const float* input, int numSamples,
                                                    double sampleRate) {
    if (input == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return {};

    std::vector<float> source(input, input + numSamples);
    const double ratio = sampleRate / kSampleRate;
    if (ratio > 1.0) {
        for (int pass = 0; pass < 2; ++pass) {
            juce::IIRFilter filter;
            filter.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, kAntiAliasHz));
            filter.processSamples(source.data(), numSamples);
        }
    }

    std::vector<float> output(static_cast<std::size_t>(std::floor(numSamples / ratio)));
    juce::LagrangeInterpolator interpolator;
    interpolator.process(ratio, source.data(), output.data(), static_cast<int>(output.size()));
    return output;
}

}  // namespace magda
