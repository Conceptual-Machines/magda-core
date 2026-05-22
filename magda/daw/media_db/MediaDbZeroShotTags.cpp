#include "MediaDbZeroShotTags.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "ClapTextEncoder.hpp"
#include "RobertaTokenizer.hpp"

namespace magda::media {

namespace {

// Single source of truth for the prompt list. Mirrors DEFAULT_TAGS in
// prototypes/media_db/src/media_db/tags.py — keep the two lists in sync when
// adjusting taxonomy so the prototype's validation runs stay representative.
const std::vector<std::string>& staticPrompts() {
    static const std::vector<std::string> kPrompts = {
        // drums
        "the sound of a kick drum",
        "the sound of a snare drum",
        "the sound of a clap",
        "the sound of a hi-hat",
        "the sound of a cymbal",
        "the sound of a tom drum",
        "the sound of a percussion loop",
        "the sound of a drum loop",
        "the sound of a 808 bass drum",
        // bass and lead
        "the sound of a sub bass",
        "the sound of a synth bass",
        "the sound of an acid bass",
        "the sound of a synth lead",
        "the sound of a synth pad",
        "the sound of a synth pluck",
        "the sound of an arpeggio",
        // acoustic
        "the sound of a piano",
        "the sound of an electric piano",
        "the sound of an organ",
        "the sound of an acoustic guitar",
        "the sound of an electric guitar",
        "the sound of strings",
        "the sound of brass",
        "the sound of woodwinds",
        "the sound of a vocal",
        "the sound of a vocal chop",
        // fx
        "the sound of a sound effect",
        "the sound of an impact",
        "the sound of a riser",
        "the sound of a downlifter",
        "the sound of a noise sweep",
        "the sound of an ambience",
        "the sound of a foley sound",
        // texture / mood descriptors
        "the sound of a dark sound",
        "the sound of a bright sound",
        "the sound of a warm sound",
        "the sound of a metallic sound",
        "the sound of a distorted sound",
        "the sound of a clean sound",
        "the sound of a lo-fi sound",
        "the sound of a glitchy sound",
    };
    return kPrompts;
}

// Prompt -> coarse family. "texture" is a sentinel; texture prompts can
// still emit a tag but never set the family (see familyFromTopTags). The
// keys are the exact prompt strings from staticPrompts() so lookups are
// trivial; if a caller passes a string that isn't in the map we treat it
// as unknown.
const std::unordered_map<std::string, std::string>& staticFamilyMap() {
    static const std::unordered_map<std::string, std::string> kFamily = {
        {"the sound of a kick drum", "drum"},
        {"the sound of a snare drum", "drum"},
        {"the sound of a clap", "drum"},
        {"the sound of a hi-hat", "drum"},
        {"the sound of a cymbal", "drum"},
        {"the sound of a tom drum", "drum"},
        {"the sound of a percussion loop", "drum"},
        {"the sound of a drum loop", "drum"},
        {"the sound of a 808 bass drum", "drum"},

        {"the sound of a sub bass", "bass"},
        {"the sound of a synth bass", "bass"},
        {"the sound of an acid bass", "bass"},

        {"the sound of a synth lead", "lead"},
        {"the sound of a synth pluck", "lead"},
        {"the sound of an arpeggio", "lead"},

        {"the sound of a synth pad", "pad"},

        {"the sound of a piano", "keys"},
        {"the sound of an electric piano", "keys"},
        {"the sound of an organ", "keys"},

        {"the sound of an acoustic guitar", "guitar"},
        {"the sound of an electric guitar", "guitar"},

        {"the sound of strings", "orchestral"},
        {"the sound of brass", "orchestral"},
        {"the sound of woodwinds", "orchestral"},

        {"the sound of a vocal", "vocal"},
        {"the sound of a vocal chop", "vocal"},

        {"the sound of a sound effect", "fx"},
        {"the sound of an impact", "fx"},
        {"the sound of a riser", "fx"},
        {"the sound of a downlifter", "fx"},
        {"the sound of a noise sweep", "fx"},
        {"the sound of an ambience", "fx"},
        {"the sound of a foley sound", "fx"},

        {"the sound of a dark sound", "texture"},
        {"the sound of a bright sound", "texture"},
        {"the sound of a warm sound", "texture"},
        {"the sound of a metallic sound", "texture"},
        {"the sound of a distorted sound", "texture"},
        {"the sound of a clean sound", "texture"},
        {"the sound of a lo-fi sound", "texture"},
        {"the sound of a glitchy sound", "texture"},
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

const std::vector<std::string>& defaultZeroShotPrompts() {
    return staticPrompts();
}

const std::string& familyForPrompt(const std::string& prompt) {
    static const std::string kUnknown = "unknown";
    const auto& map = staticFamilyMap();
    if (auto it = map.find(prompt); it != map.end()) {
        return it->second;
    }
    return kUnknown;
}

std::string familyFromTopTags(const std::vector<std::pair<std::string, float>>& topTags) {
    // topTags is descending-confidence (scoreEmbedding's contract). Walk it
    // and return the first non-texture family above the floor. This mirrors
    // derive.py:family() — first real-instrument candidate wins; texture
    // descriptors are skipped so "warm sound" can't outrank "synth pad".
    for (const auto& [tag, conf] : topTags) {
        if (conf < kZeroShotFamilyFloor) {
            break;  // sorted; everything after is below threshold
        }
        const auto& family = familyForPrompt(tag);
        if (family.empty() || family == "texture" || family == "unknown") {
            continue;
        }
        return family;
    }
    return {};
}

ZeroShotTagger::ZeroShotTagger(ClapTextEncoder& textEncoder, RobertaTokenizer& tokenizer)
    : ZeroShotTagger(textEncoder, tokenizer, staticPrompts()) {}

ZeroShotTagger::ZeroShotTagger(ClapTextEncoder& textEncoder, RobertaTokenizer& tokenizer,
                               std::vector<std::string> prompts)
    : prompts_(std::move(prompts)) {
    if (prompts_.empty()) {
        throw std::runtime_error("ZeroShotTagger: prompt list is empty");
    }

    const int dim = textEncoder.dim();
    if (dim <= 0) {
        throw std::runtime_error("ZeroShotTagger: text encoder reports non-positive dim");
    }
    dim_ = static_cast<std::size_t>(dim);
    promptMatrix_.resize(prompts_.size() * dim_, 0.0F);

    for (std::size_t i = 0; i < prompts_.size(); ++i) {
        const auto enc = tokenizer.encode(prompts_[i]);
        auto vec = textEncoder.embedTokens(enc.inputIds, enc.attentionMask);
        if (vec.size() != dim_) {
            throw std::runtime_error("ZeroShotTagger: text embedding dim mismatch on prompt " +
                                     prompts_[i]);
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
    hits.reserve(prompts_.size());
    for (std::size_t i = 0; i < prompts_.size(); ++i) {
        const std::size_t rowStart = i * dim_;
        double dot = 0.0;
        for (std::size_t j = 0; j < dim_; ++j) {
            dot += static_cast<double>(promptMatrix_[rowStart + j]) *
                   static_cast<double>(audioEmbedding[j]);
        }
        const float score = static_cast<float>(dot);
        if (score >= threshold) {
            hits.emplace_back(prompts_[i], score);
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return hits;
}

std::size_t ZeroShotTagger::numPrompts() const noexcept {
    return prompts_.size();
}

std::size_t ZeroShotTagger::embeddingDim() const noexcept {
    return dim_;
}

const std::vector<std::string>& ZeroShotTagger::prompts() const noexcept {
    return prompts_;
}

}  // namespace magda::media
