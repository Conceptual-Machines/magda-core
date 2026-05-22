// CLAP zero-shot tagging for the media DB indexer (issue #1319).
//
// Mirrors prototypes/media_db/src/media_db/tags.py — a curated list of
// short instrument labels ("a kick drum", "a synth pad", "a riser") plus a
// coarse instrument-family map. The C++ port wraps each label with the
// "the sound of {label}" prompt template before running it through the
// loaded ClapTextEncoder + RobertaTokenizer, exactly like the prototype's
// PROMPT_TEMPLATE; the resulting [K, 512] row-major matrix of L2-normalized
// embeddings is cached. Scoring an audio embedding is then a dense matrix-
// vector dot product (~K * 512 multiply-adds — negligible against the
// encoder pass that produced the audio embedding).
//
// Labels (NOT prompts) are what the tagger returns and what the indexer
// writes into media_tag. The prompt template is an internal detail of the
// embedding step so the stored tags stay terse and FTS isn't polluted with
// "the sound of" boilerplate on every match.
//
// Family precedence matches derive.py:232 — the indexer's path hint wins;
// this module only contributes "what does CLAP think it is" when the path
// gave no hint. Texture descriptors ("warm sound", "dark sound") are
// intentionally excluded from the family fallback so a "warm sound" tag can
// never silently outrank an actual instrument tag.

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace magda::media {

class ClapTextEncoder;
class RobertaTokenizer;

// Cosine threshold for emitting a tag into media_tag. Lower than the human
// "obviously the right tag" bar — the FTS+filter UI lets recall pay off, and
// per-tag calibration is a follow-up once we can see real recall gaps.
constexpr float kZeroShotTagThreshold = 0.28F;

// Floor for promoting a CLAP top-tag to a family. Matches derive.py's
// FAMILY_TAG_FLOOR: be conservative — better to leave family as "unknown"
// than to mis-classify a borderline file.
constexpr float kZeroShotFamilyFloor = 0.20F;

// The taxonomy as short labels suitable for storing in media_tag.tag. The
// embedding step wraps each label with kPromptTemplate internally before
// running it through the text encoder; callers never see the "the sound of"
// form. Order is irrelevant — the index into the list isn't persisted.
const std::vector<std::string>& defaultZeroShotLabels();

// Coarse family for each label above. "texture" is a sentinel that excludes
// the label from family inference (see header preamble). Returns "unknown"
// for labels not in the map.
const std::string& familyForLabel(const std::string& label);

// Apply CLAP's cosine score and produce the family override. Returns the
// empty string when no non-texture label clears kZeroShotFamilyFloor; the
// caller keeps the existing family ("unknown" or the path hint) in that
// case. `topLabels` is expected to be sorted by descending confidence.
std::string familyFromTopLabels(const std::vector<std::pair<std::string, float>>& topLabels);

// Loads the labels, wraps each in the prompt template, tokenizes, runs
// through the text encoder, and caches the result as a row-major [K, 512]
// matrix. K is fixed at construction; the matrix is roughly ~80 KB so the
// one-time cost is trivial relative to the ORT session itself.
//
// Throws std::runtime_error if any label fails to embed — the indexer
// catches and degrades to path-only tagging in that case.
class ZeroShotTagger {
  public:
    ZeroShotTagger(ClapTextEncoder& textEncoder, RobertaTokenizer& tokenizer);
    ZeroShotTagger(ClapTextEncoder& textEncoder, RobertaTokenizer& tokenizer,
                   std::vector<std::string> labels);

    ZeroShotTagger(const ZeroShotTagger&) = delete;
    ZeroShotTagger& operator=(const ZeroShotTagger&) = delete;
    ZeroShotTagger(ZeroShotTagger&&) noexcept = default;
    ZeroShotTagger& operator=(ZeroShotTagger&&) noexcept = default;
    ~ZeroShotTagger() = default;

    // Score a single audio embedding (must be L2-normalized; the audio
    // encoder already guarantees that) against every cached prompt. Returns
    // (label, score) pairs whose cosine score >= threshold, sorted by
    // descending score. The labels returned are the terse form (suitable
    // for media_tag.tag), not the wrapped prompt.
    //
    // The audio embedding's dim must equal the cached embedding dim
    // (defaults to 512 for CLAP). Mismatched inputs throw.
    std::vector<std::pair<std::string, float>> scoreEmbedding(
        const float* audioEmbedding, std::size_t dim,
        float threshold = kZeroShotTagThreshold) const;

    [[nodiscard]] std::size_t numLabels() const noexcept;
    [[nodiscard]] std::size_t embeddingDim() const noexcept;
    [[nodiscard]] const std::vector<std::string>& labels() const noexcept;

  private:
    std::vector<std::string> labels_;  // K — short, suitable for media_tag.tag
    std::vector<float> promptMatrix_;  // row-major [K, dim_], L2-normalized
    std::size_t dim_ = 0;
};

}  // namespace magda::media
