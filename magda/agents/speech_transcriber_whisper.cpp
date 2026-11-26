// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file speech_transcriber_whisper.cpp
 * @brief SpeechTranscriber on whisper.cpp, for builds with local inference.
 */

#include <whisper.h>

#include "ggml_backends.hpp"
#include "llama_model_manager.hpp"
#include "speech_transcriber.hpp"

namespace magda {

bool SpeechTranscriber::isAvailable() {
    return true;
}

SpeechTranscriber::Result SpeechTranscriber::transcribe(const std::vector<float>& samples,
                                                        const std::string& prompt,
                                                        const std::atomic<bool>* abort) {
    std::scoped_lock lock(mutex_);
    if (auto error = loadLocked(); !error.empty())
        return {{}, error};
    return decodeLocked(samples, prompt, abort);
}

std::string SpeechTranscriber::preload(const std::atomic<bool>* abort) {
    std::scoped_lock lock(mutex_);
    if (warmed_)
        return {};
    if (auto error = loadLocked(); !error.empty())
        return error;

    // The first decode also builds the GPU pipelines; a second of silence pays for that here.
    const std::vector<float> silence(kSampleRate, 0.0f);
    decodeLocked(silence, {}, abort);
    warmed_ = true;
    return {};
}

void SpeechTranscriber::unload() {
    std::scoped_lock lock(mutex_);
    warmed_ = false;
    loaded_.store(false, std::memory_order_release);
    if (ctx_ != nullptr) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

std::string SpeechTranscriber::loadLocked() {
    if (ctx_ != nullptr)
        return {};

    if (auto cpuError = LlamaModelManager::currentCpuCompatibilityError(); !cpuError.empty())
        return cpuError;

    const auto path = modelFile();
    if (!path.existsAsFile())
        return "The speech model is not downloaded.";

    loadGgmlBackends();
    auto contextParams = whisper_context_default_params();
    contextParams.use_gpu = true;
    contextParams.flash_attn = true;
    ctx_ = whisper_init_from_file_with_params(path.getFullPathName().toRawUTF8(), contextParams);
    loaded_.store(ctx_ != nullptr, std::memory_order_release);
    return ctx_ != nullptr ? std::string{} : "The speech model could not be loaded.";
}

SpeechTranscriber::Result SpeechTranscriber::decodeLocked(const std::vector<float>& samples,
                                                          const std::string& prompt,
                                                          const std::atomic<bool>* abort) {
    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = "en";
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.suppress_blank = true;
    params.suppress_nst = true;
    params.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
    if (abort != nullptr) {
        params.abort_callback = [](void* flag) {
            return static_cast<const std::atomic<bool>*>(flag)->load(std::memory_order_relaxed);
        };
        params.abort_callback_user_data = const_cast<std::atomic<bool>*>(abort);
    }

    if (whisper_full(ctx_, params, samples.data(), static_cast<int>(samples.size())) != 0)
        return {{}, "Transcription failed."};

    juce::String text;
    for (int segment = 0; segment < whisper_full_n_segments(ctx_); ++segment)
        text += juce::String::fromUTF8(whisper_full_get_segment_text(ctx_, segment));
    return {text.trim().toStdString(), {}};
}

}  // namespace magda
