// SentencePiece Unigram tokenizer for the encoder command model (#1847).
//
// DeBERTa-v3 tokenizes with Unigram, not the byte-level BPE that
// media_db/RobertaTokenizer.cpp implements for CLAP — different algorithm, so
// it needs its own implementation. Loads the same Hugging Face tokenizer.json
// format, exported by prototypes/command-model-poc/model/export_onnx.py.
//
// Scope is deliberately narrow, matching what the command model actually
// receives. The Python word splitter (dataset/tagging.py:_TOK) is ASCII-only,
// so every word reaching this class is ASCII: NFC normalization is therefore a
// no-op and is not implemented. Non-ASCII input cannot reach here today; see
// the multilingual section of the POC's findings.md.
//
// The API is words-in, because the model is trained with
// is_split_into_words=True: BIO slot tags are per word, and the caller needs to
// know where each word's first subword landed so it can read that word's tag
// back out. Getting that index wrong silently costs ~30 points of accuracy.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace magda::cmdmodel {

class UnigramTokenizerError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class UnigramTokenizer {
  public:
    struct Encoding {
        std::vector<int64_t> inputIds;       // includes [CLS] ... [SEP]
        std::vector<int64_t> attentionMask;  // all ones; padding is the caller's job
        // firstSubword[i] = index into inputIds of word i's first subword, or
        // -1 if the word was truncated away. This is the read-out point for
        // word i's BIO tag.
        std::vector<int> firstSubword;
    };

    // Loads vocab (token + log-prob), unk id, and the special-token ids from a
    // Hugging Face tokenizer.json. Throws on parse failure or a missing field.
    explicit UnigramTokenizer(const std::filesystem::path& tokenizerJsonPath);
    ~UnigramTokenizer();

    UnigramTokenizer(const UnigramTokenizer&) = delete;
    UnigramTokenizer& operator=(const UnigramTokenizer&) = delete;
    UnigramTokenizer(UnigramTokenizer&&) noexcept;
    UnigramTokenizer& operator=(UnigramTokenizer&&) noexcept;

    // Encode pre-split words, mirroring HF `tokenizer(words,
    // is_split_into_words=True)`. maxLen caps the total subword count
    // including [CLS]/[SEP]; words that do not fit get firstSubword == -1.
    [[nodiscard]] Encoding encodeWords(const std::vector<std::string>& words,
                                       int maxLen = 64) const;

    // Single-word segmentation, exposed for tests and fuzzing against Python.
    [[nodiscard]] std::vector<std::string> pieces(const std::string& word) const;

    [[nodiscard]] int vocabSize() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::cmdmodel
