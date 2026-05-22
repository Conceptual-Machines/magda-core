#include "MediaDbZeroShotTags.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "ClapTextEncoder.hpp"
#include "RobertaTokenizer.hpp"

namespace magda::media {

namespace {

// Mirrors tags.py's PROMPT_TEMPLATE. The label list stays terse for the DB
// and the UI; this string is only used internally when we hand text to the
// CLAP text encoder. Keep the wording in sync with the prototype so the
// validation runs there stay representative of the C++ embedding values.
constexpr const char* kPromptTemplate = "the sound of ";

// Single source of truth for the label list. Mirrors DEFAULT_TAGS in
// prototypes/media_db/src/media_db/tags.py — keep the two lists in sync
// when adjusting taxonomy so the prototype's validation runs stay
// representative.
const std::vector<std::string>& staticLabels() {
    static const std::vector<std::string> kLabels = {
        // drums
        "a kick drum",
        "a snare drum",
        "a clap",
        "a hi-hat",
        "a cymbal",
        "a tom drum",
        "a percussion loop",
        "a drum loop",
        "a 808 bass drum",
        // bass and lead
        "a sub bass",
        "a synth bass",
        "an acid bass",
        "a synth lead",
        "a synth pad",
        "a synth pluck",
        "an arpeggio",
        // acoustic
        "a piano",
        "an electric piano",
        "an organ",
        "an acoustic guitar",
        "an electric guitar",
        "strings",
        "brass",
        "woodwinds",
        "a vocal",
        "a vocal chop",
        // fx
        "a sound effect",
        "an impact",
        "a riser",
        "a downlifter",
        "a noise sweep",
        "an ambience",
        "a foley sound",
        // texture / mood descriptors
        "a dark sound",
        "a bright sound",
        "a warm sound",
        "a metallic sound",
        "a distorted sound",
        "a clean sound",
        "a lo-fi sound",
        "a glitchy sound",
    };
    return kLabels;
}

// Label -> coarse family. "texture" is a sentinel; texture labels can still
// emit a tag but never set the family (see familyFromTopLabels).
const std::unordered_map<std::string, std::string>& staticFamilyMap() {
    static const std::unordered_map<std::string, std::string> kFamily = {
        {"a kick drum", "drum"},
        {"a snare drum", "drum"},
        {"a clap", "drum"},
        {"a hi-hat", "drum"},
        {"a cymbal", "drum"},
        {"a tom drum", "drum"},
        {"a percussion loop", "drum"},
        {"a drum loop", "drum"},
        {"a 808 bass drum", "drum"},

        {"a sub bass", "bass"},
        {"a synth bass", "bass"},
        {"an acid bass", "bass"},

        {"a synth lead", "lead"},
        {"a synth pluck", "lead"},
        {"an arpeggio", "lead"},

        {"a synth pad", "pad"},

        {"a piano", "keys"},
        {"an electric piano", "keys"},
        {"an organ", "keys"},

        {"an acoustic guitar", "guitar"},
        {"an electric guitar", "guitar"},

        {"strings", "orchestral"},
        {"brass", "orchestral"},
        {"woodwinds", "orchestral"},

        {"a vocal", "vocal"},
        {"a vocal chop", "vocal"},

        {"a sound effect", "fx"},
        {"an impact", "fx"},
        {"a riser", "fx"},
        {"a downlifter", "fx"},
        {"a noise sweep", "fx"},
        {"an ambience", "fx"},
        {"a foley sound", "fx"},

        {"a dark sound", "texture"},
        {"a bright sound", "texture"},
        {"a warm sound", "texture"},
        {"a metallic sound", "texture"},
        {"a distorted sound", "texture"},
        {"a clean sound", "texture"},
        {"a lo-fi sound", "texture"},
        {"a glitchy sound", "texture"},
    };
    return kFamily;
}

void l2NormalizeInPlace(float* v, std::size_t n) {
    double sumSq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sumSq += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    }
    const float norm = static_cast<float>(std::sqrt(sumSq));
    if (norm <= 0.0F) {
        return;
    }
    const float inv = 1.0F / norm;
    for (std::size_t i = 0; i < n; ++i) {
        v[i] *= inv;
    }
}

}  // namespace

const std::vector<std::string>& defaultZeroShotLabels() {
    return staticLabels();
}

const std::string& familyForLabel(const std::string& label) {
    static const std::string kUnknown = "unknown";
    const auto& map = staticFamilyMap();
    if (auto it = map.find(label); it != map.end()) {
        return it->second;
    }
    return kUnknown;
}

std::string familyFromTopLabels(const std::vector<std::pair<std::string, float>>& topLabels) {
    // topLabels is descending-confidence (scoreEmbedding's contract). Walk
    // it and return the first non-texture family above the floor. Mirrors
    // derive.py:family() — first real-instrument candidate wins; texture
    // descriptors are skipped so "warm sound" can't outrank "synth pad".
    for (const auto& [label, conf] : topLabels) {
        if (conf < kZeroShotFamilyFloor) {
            break;  // sorted; everything after is below threshold
        }
        const auto& family = familyForLabel(label);
        if (family.empty() || family == "texture" || family == "unknown") {
            continue;
        }
        return family;
    }
    return {};
}

ZeroShotTagger::ZeroShotTagger(ClapTextEncoder& textEncoder, RobertaTokenizer& tokenizer)
    : ZeroShotTagger(textEncoder, tokenizer, staticLabels()) {}

ZeroShotTagger::ZeroShotTagger(ClapTextEncoder& textEncoder, RobertaTokenizer& tokenizer,
                               std::vector<std::string> labels)
    : labels_(std::move(labels)) {
    if (labels_.empty()) {
        throw std::runtime_error("ZeroShotTagger: label list is empty");
    }

    const int dim = textEncoder.dim();
    if (dim <= 0) {
        throw std::runtime_error("ZeroShotTagger: text encoder reports non-positive dim");
    }
    dim_ = static_cast<std::size_t>(dim);
    promptMatrix_.resize(labels_.size() * dim_, 0.0F);

    for (std::size_t i = 0; i < labels_.size(); ++i) {
        // Wrap the terse label with "the sound of " before tokenizing — the
        // text encoder was trained on full sentences, and the prototype
        // applies the same template (PROMPT_TEMPLATE in tags.py).
        const std::string prompt = std::string(kPromptTemplate) + labels_[i];
        const auto enc = tokenizer.encode(prompt);
        auto vec = textEncoder.embedTokens(enc.inputIds, enc.attentionMask);
        if (vec.size() != dim_) {
            throw std::runtime_error("ZeroShotTagger: text embedding dim mismatch on label " +
                                     labels_[i]);
        }
        // ClapTextEncoder already L2-normalizes, but re-normalize defensively
        // — a future refactor could drop normalization at the encoder layer
        // and we'd silently corrupt cosine math here.
        l2NormalizeInPlace(vec.data(), vec.size());
        const auto offset = static_cast<std::ptrdiff_t>(i * dim_);
        std::copy(vec.begin(), vec.end(), promptMatrix_.begin() + offset);
    }
}

std::vector<std::pair<std::string, float>> ZeroShotTagger::scoreEmbedding(
    const float* audioEmbedding, std::size_t dim, float threshold) const {
    if (dim != dim_) {
        throw std::runtime_error("ZeroShotTagger::scoreEmbedding: dim mismatch");
    }

    std::vector<std::pair<std::string, float>> hits;
    hits.reserve(labels_.size());
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const std::size_t rowStart = i * dim_;
        double dot = 0.0;
        for (std::size_t j = 0; j < dim_; ++j) {
            dot += static_cast<double>(promptMatrix_[rowStart + j]) *
                   static_cast<double>(audioEmbedding[j]);
        }
        const float score = static_cast<float>(dot);
        if (score >= threshold) {
            hits.emplace_back(labels_[i], score);
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return hits;
}

std::size_t ZeroShotTagger::numLabels() const noexcept {
    return labels_.size();
}

std::size_t ZeroShotTagger::embeddingDim() const noexcept {
    return dim_;
}

const std::vector<std::string>& ZeroShotTagger::labels() const noexcept {
    return labels_;
}

}  // namespace magda::media
