// Basic Pitch ONNX backend for audio-to-MIDI transcription (issue #1168).
//
// Wraps the ICASSP-2022 Basic Pitch model (Spotify, Apache-2.0) exported to
// ONNX (nmp.onnx, ~230 KB). Pimpl over Ort::Session, mirroring
// media_db/ClapAudioEncoder so onnxruntime headers don't escape to callers
// and so the same MAGDA_HAVE_CLAP gate decides availability.
//
// Pipeline: resample mono input to 22050 Hz -> window into overlapping
// 43844-sample frames -> run the model per window -> stitch the per-window
// posteriorgrams (BasicPitchPostprocess geometry) -> decodeNotes -> map the
// frame-domain note events to seconds-domain TranscribedNotes.

#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "Transcriber.hpp"

namespace magda::transcription {

class BasicPitchTranscriber : public Transcriber {
  public:
    // Loads the ONNX model at modelPath. On failure (missing file, bad model,
    // or a build without ONNX) construction leaves the backend unusable and
    // transcribe() returns empty; check isLoaded() to distinguish.
    explicit BasicPitchTranscriber(const std::filesystem::path& modelPath);
    ~BasicPitchTranscriber() override;

    BasicPitchTranscriber(const BasicPitchTranscriber&) = delete;
    BasicPitchTranscriber& operator=(const BasicPitchTranscriber&) = delete;

    [[nodiscard]] bool isLoaded() const noexcept;

    std::vector<TranscribedNote> transcribe(const float* mono, int numSamples,
                                            int sampleRate) override;

    [[nodiscard]] const char* modelId() const noexcept override {
        return "basic_pitch";
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::transcription
