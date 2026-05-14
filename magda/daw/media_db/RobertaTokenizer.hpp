// RoBERTa BPE tokenizer for CLAP's text encoder (issue #768).
//
// CLAP's text path uses laion/clap-htsat-unfused's RoBERTa-base tokenizer.
// This module loads the standard Hugging Face tokenizer.json format and
// produces the (input_ids, attention_mask) pair that ClapTextEncoder feeds
// to ONNX Runtime.
//
// Scope: byte-level BPE, ASCII pre-tokenization regex (English producer
// queries — "warm analog pad", "punchy kick drum"). Non-ASCII input is
// still encoded correctly (bytes → unicode), but the regex split is ASCII-
// classed; multilingual queries would benefit from ICU later.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace magda::media {

class RobertaTokenizerError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class RobertaTokenizer {
  public:
    // Loads vocab + merges + special tokens from a Hugging Face
    // tokenizer.json. Throws on parse / load failure.
    explicit RobertaTokenizer(const std::filesystem::path& tokenizerJsonPath);
    ~RobertaTokenizer();

    RobertaTokenizer(const RobertaTokenizer&) = delete;
    RobertaTokenizer& operator=(const RobertaTokenizer&) = delete;
    RobertaTokenizer(RobertaTokenizer&&) noexcept;
    RobertaTokenizer& operator=(RobertaTokenizer&&) noexcept;

    struct Encoded {
        std::vector<int64_t> inputIds;
        std::vector<int64_t> attentionMask;  // 1 = real token, 0 = padding
    };

    // Encode a single string. BOS + content + EOS, padded with PAD to
    // maxLength (default 77 — CLAP's convention). Truncates content if too
    // long; in that case BOS + first (maxLength - 2) content tokens + EOS.
    Encoded encode(const std::string& text, int maxLength = 77) const;

    // Special-token IDs (cached at load time so callers can build inputs
    // without re-parsing). Returns -1 if the token isn't present in the
    // tokenizer.json — which would indicate the file is corrupt for our
    // use case.
    [[nodiscard]] int64_t bosId() const noexcept;
    [[nodiscard]] int64_t eosId() const noexcept;
    [[nodiscard]] int64_t padId() const noexcept;
    [[nodiscard]] int64_t unkId() const noexcept;
    [[nodiscard]] std::size_t vocabSize() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::media
