// Pretrained-encoder command model, ONNX Runtime backend (#1847).
//
// Same job as CommandModel — natural language → validated MAGDA DSL — with the
// perception half swapped for a fine-tuned transformer encoder. On held-out
// phrasing the conv net scores 46.5% and this scores 91.5%; the conv net has no
// language prior, so it names tracks after whichever word sat in the name
// position during training ("can you mute the guitar" → track(name="Can You")).
//
// The deterministic half is shared, not duplicated: this produces a
// CommandModel::Prediction (intent + one BIO tag per word) and hands it to
// CommandModel::renderPrediction. The model still never emits DSL text, so
// malformed DSL remains structurally impossible on both paths.
//
// Assets are an on-demand download, not bundled — the graph is ~736 MB fp32.
// int8 quantization is NOT usable here: quantizing the attention/FFN MatMuls
// drops accuracy to 0.9% because DeBERTa-v3's disentangled attention is
// quantization-hostile. Quantizing only the embedding Gather is safe (96.1%).
//
// The pimpl keeps onnxruntime_cxx_api.h out of this header, matching
// media_db/ClapTextEncoder.

#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "command_model.hpp"

namespace magda {

class CommandModelOnnxError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/** maps.json inverted: dense id-indexed name tables for intents and tags. */
struct CommandModelMaps {
    std::vector<std::string> idToIntent;
    std::vector<std::string> idToTag;
};

/** Parses and validates maps.json text: "intents" and "tags" objects mapping
 *  name -> id, where each map's ids must be integers covering [0, size) with
 *  no duplicates. Throws CommandModelOnnxError on any violation. A free
 *  function, and compiled on every build, so the validation is testable
 *  without the model bundle or ONNX Runtime. */
[[nodiscard]] CommandModelMaps parseCommandModelMaps(const std::string& mapsJson);

class CommandModelOnnx {
  public:
    /** Loads command_model.onnx, tokenizer.json and maps.json from assetDir
     *  (as written by prototypes/command-model-poc/model/export_onnx.py).
     *  Throws CommandModelOnnxError if any is missing or fails to load. */
    explicit CommandModelOnnx(const std::filesystem::path& assetDir);
    ~CommandModelOnnx();

    CommandModelOnnx(const CommandModelOnnx&) = delete;
    CommandModelOnnx& operator=(const CommandModelOnnx&) = delete;
    CommandModelOnnx(CommandModelOnnx&&) noexcept;
    CommandModelOnnx& operator=(CommandModelOnnx&&) noexcept;

    /** Natural language → canonical DSL. "" on empty/unrecognised input. */
    [[nodiscard]] std::string generate(const std::string& text) const;

    /** Perception only — intent + per-word BIO tags. Same shape as the conv
     *  net's, so both backends are directly comparable in tests. */
    [[nodiscard]] CommandModel::Prediction predict(const std::string& text) const;

    /** True when assetDir holds every file the constructor needs. */
    [[nodiscard]] static bool isInstalled(const std::filesystem::path& assetDir);

    /** Where the app looks for the bundle. MAGDA_COMMAND_MODEL_DIR takes
     *  precedence for development, followed by the user-selected directory,
     *  then <dataDir>/CommandModel/models. */
    [[nodiscard]] static std::filesystem::path defaultAssetDir();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda
