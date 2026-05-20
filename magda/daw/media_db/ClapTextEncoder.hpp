// CLAP text encoder (issue #768).
//
// Loads the laion/clap-htsat-unfused text encoder exported to ONNX by the
// Python prototype. Given a pre-tokenized RoBERTa input (token IDs +
// attention mask), produces a 512-dim L2-normalized embedding — the query
// side of cosine-similarity search against media_embedding rows.
//
// This module exposes a tokens-in API. The actual BPE tokenizer is a
// separate concern (Phase D3) so this layer stays focused on the ONNX
// inference path. The same pimpl pattern as ClapAudioEncoder keeps the
// onnxruntime_cxx_api.h header out of the public surface.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace magda::media {

class ClapTextEncoderError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class ClapTextEncoder {
  public:
    // Loads the ONNX model at modelPath. Throws ClapTextEncoderError on
    // missing file or session-creation failure.
    explicit ClapTextEncoder(const std::filesystem::path& modelPath);
    ~ClapTextEncoder();

    ClapTextEncoder(const ClapTextEncoder&) = delete;
    ClapTextEncoder& operator=(const ClapTextEncoder&) = delete;
    ClapTextEncoder(ClapTextEncoder&&) noexcept;
    ClapTextEncoder& operator=(ClapTextEncoder&&) noexcept;

    // Embed a pre-tokenized batch. inputIds and attentionMask must be the
    // same length (the model's expected sequence length, typically 77 for
    // CLAP). Returns a 512-dim L2-normalized vector.
    //
    // Throws ClapTextEncoderError on inference failure or size mismatch.
    std::vector<float> embedTokens(const std::vector<int64_t>& inputIds,
                                   const std::vector<int64_t>& attentionMask);

    [[nodiscard]] int dim() const noexcept;

    // Identifier suitable for media_embedding.model_id; defaults to the
    // model file's stem. The text encoder shares the same model_id as the
    // audio encoder in production (they're both from the same CLAP variant)
    // — callers can use setModelId() to make that explicit.
    [[nodiscard]] const std::string& modelId() const noexcept;
    void setModelId(std::string id);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::media
