// CLAP audio encoder (issue #768).
//
// Loads the laion/clap-htsat-unfused audio encoder exported to ONNX by the
// Python prototype's media-db export-onnx command. Given mono 48 kHz PCM,
// produces a 512-dim L2-normalized embedding vector — the same one the
// prototype stores in media_embedding.vector_blob.
//
// Pimpl wraps Ort::Session so onnxruntime headers don't escape to callers.
// This also means a future switch from compile-time-linked ORT to a
// dlopened runtime library is a localized change (rewrite the Impl, leave
// the public API alone).

#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace magda::media {

class ClapEncoderError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class ClapAudioEncoder {
  public:
    // Loads the ONNX model at modelPath. Throws ClapEncoderError on missing
    // file or session-creation failure.
    explicit ClapAudioEncoder(const std::filesystem::path& modelPath);
    ~ClapAudioEncoder();

    ClapAudioEncoder(const ClapAudioEncoder&) = delete;
    ClapAudioEncoder& operator=(const ClapAudioEncoder&) = delete;
    ClapAudioEncoder(ClapAudioEncoder&&) noexcept;
    ClapAudioEncoder& operator=(ClapAudioEncoder&&) noexcept;

    // Embed mono float PCM at the encoder's expected sample rate (48 kHz).
    // Audio longer than the model's nominal window (10 s) is chunked,
    // each chunk's L2-normalized embedding is averaged, then renormalized —
    // same scheme as the Python ClapEmbedder.
    //
    // Returns a vector of length `dim()`, L2-normalized so cosine similarity
    // is a dot product. Throws ClapEncoderError on inference failure.
    std::vector<float> embed(const float* mono, int numSamples);

    [[nodiscard]] int dim() const noexcept;
    [[nodiscard]] int sampleRate() const noexcept;

    // Identifier suitable for media_embedding.model_id; defaults to the
    // model's filename stem. Override via setModelId() if you want a
    // stable string independent of disk path.
    [[nodiscard]] const std::string& modelId() const noexcept;
    void setModelId(std::string id);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::media
