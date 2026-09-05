#include "unigram_tokenizer.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

// Mirrors the Hugging Face pipeline for microsoft/deberta-v3-base as exported
// by model/export_onnx.py, in the order tokenizers applies it:
//
//   added tokens   matched literally first, so "<alias>" stays one unit
//   normalizer     Replace(\s{2,}|[\n\r\t] -> " ") -> NFC -> Strip(right)
//   pre_tokenizer  Metaspace(replacement="▁", prepend_scheme="always")
//   model          Unigram, Viterbi over log-probs, unk_id 3
//   post_processor TemplateProcessing: [CLS] $A [SEP]
//
// NFC is omitted: inputs are ASCII (see the header). The normalizer's other
// two steps still matter — a word carrying a tab would otherwise segment
// differently here than in Python.

namespace magda::cmdmodel {

namespace {

constexpr const char* kMetaspace = "\xE2\x96\x81";  // U+2581 LOWER ONE EIGHTH BLOCK

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Normalizer: collapse runs of whitespace (and any tab/newline/CR) to a single
// space, then strip trailing whitespace. Matches the Replace + Strip pair.
std::string normalize(const std::string& in) {
    std::string collapsed;
    collapsed.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (isAsciiSpace(in[i])) {
            size_t run = i;
            while (run < in.size() && isAsciiSpace(in[run]))
                ++run;
            // Replace matches \s{2,} OR a single [\n\r\t]; a lone space is left
            // alone, which is the same result either way — one space.
            collapsed += ' ';
            i = run;
        } else {
            collapsed += in[i];
            ++i;
        }
    }
    while (!collapsed.empty() && isAsciiSpace(collapsed.back()))
        collapsed.pop_back();
    return collapsed;
}

// Metaspace with prepend_scheme="always": every word gets a leading ▁, and any
// interior space becomes ▁ too.
std::string metaspace(const std::string& word) {
    std::string out(kMetaspace);
    for (char c : word) {
        if (c == ' ')
            out += kMetaspace;
        else
            out += c;
    }
    return out;
}

// Advance one UTF-8 code point; used so an unknown byte sequence is consumed
// whole rather than split into invalid fragments.
size_t utf8Advance(const std::string& s, size_t i) {
    auto c = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0)
        len = 2;
    else if ((c & 0xF0) == 0xE0)
        len = 3;
    else if ((c & 0xF8) == 0xF0)
        len = 4;
    return std::min(i + len, s.size());
}

}  // namespace

struct UnigramTokenizer::Impl {
    std::unordered_map<std::string, int> tokenToId;
    std::vector<float> scores;  // by id
    std::unordered_map<std::string, int> addedTokens;
    size_t maxPieceBytes = 1;
    int unkId = 0;
    int clsId = -1;
    int sepId = -1;

    // Viterbi over log-probs: best[i] is the best score for the first i bytes.
    // Unigram maximizes the sum of piece log-probs, so this is a plain longest-
    // path DP with one relaxation per (position, candidate piece length).
    void viterbi(const std::string& text, std::vector<std::string>& out) const {
        const size_t n = text.size();
        constexpr float kNeg = -std::numeric_limits<float>::infinity();
        // Unknown pieces are penalized rather than forbidden, so a word
        // containing an out-of-vocab byte still segments instead of failing.
        const float unkPenalty = -20.0f;

        std::vector<float> best(n + 1, kNeg);
        std::vector<size_t> prev(n + 1, 0);
        std::vector<int> prevId(n + 1, -1);
        best[0] = 0.0f;

        for (size_t i = 0; i < n; ++i) {
            if (best[i] == kNeg)
                continue;
            bool matched = false;
            const size_t limit = std::min(n - i, maxPieceBytes);
            for (size_t len = 1; len <= limit; ++len) {
                auto it = tokenToId.find(text.substr(i, len));
                if (it == tokenToId.end())
                    continue;
                matched = true;
                const float score = best[i] + scores[static_cast<size_t>(it->second)];
                if (score > best[i + len]) {
                    best[i + len] = score;
                    prev[i + len] = i;
                    prevId[i + len] = it->second;
                }
            }
            if (!matched) {
                // No vocab entry starts here: emit <unk> for one code point.
                const size_t j = utf8Advance(text, i);
                const float score = best[i] + unkPenalty;
                if (score > best[j]) {
                    best[j] = score;
                    prev[j] = i;
                    prevId[j] = unkId;
                }
            }
        }

        std::vector<std::string> rev;
        size_t pos = n;
        while (pos > 0) {
            const size_t p = prev[pos];
            rev.push_back(text.substr(p, pos - p));
            pos = p;
        }
        out.insert(out.end(), rev.rbegin(), rev.rend());
    }
};

UnigramTokenizer::UnigramTokenizer(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>()) {
    if (!std::filesystem::exists(path))
        throw UnigramTokenizerError("tokenizer.json not found: " + path.string());

    auto json = juce::JSON::parse(juce::File(juce::String(path.string())));
    if (json.isVoid())
        throw UnigramTokenizerError("failed to parse " + path.string());

    auto* model = json.getProperty("model", juce::var()).getDynamicObject();
    if (model == nullptr)
        throw UnigramTokenizerError("tokenizer.json missing 'model'");

    const auto type = model->getProperty("type").toString();
    if (type != "Unigram")
        throw UnigramTokenizerError("expected a Unigram model, got '" + type.toStdString() + "'");

    impl_->unkId = static_cast<int>(model->getProperty("unk_id"));

    const auto* vocab = model->getProperty("vocab").getArray();
    if (vocab == nullptr)
        throw UnigramTokenizerError("tokenizer.json missing 'model.vocab'");

    impl_->scores.resize(static_cast<size_t>(vocab->size()));
    for (int id = 0; id < vocab->size(); ++id) {
        const auto* entry = (*vocab)[id].getArray();  // [token, log_prob]
        if (entry == nullptr || entry->size() < 2)
            throw UnigramTokenizerError("malformed vocab entry at " + std::to_string(id));
        auto token = (*entry)[0].toString().toStdString();
        impl_->scores[static_cast<size_t>(id)] = static_cast<float>(double((*entry)[1]));
        impl_->maxPieceBytes = std::max(impl_->maxPieceBytes, token.size());
        impl_->tokenToId.emplace(std::move(token), id);
    }

    // Added tokens (specials plus our <alias> placeholders) are matched before
    // the Unigram model runs, and may sit outside the vocab array's range.
    if (const auto* added = json.getProperty("added_tokens", juce::var()).getArray()) {
        for (const auto& a : *added) {
            auto* obj = a.getDynamicObject();
            if (obj == nullptr)
                continue;
            const auto content = obj->getProperty("content").toString().toStdString();
            const int id = static_cast<int>(obj->getProperty("id"));
            impl_->addedTokens.emplace(content, id);
            if (content == "[CLS]")
                impl_->clsId = id;
            else if (content == "[SEP]")
                impl_->sepId = id;
        }
    }
    if (impl_->clsId < 0 || impl_->sepId < 0)
        throw UnigramTokenizerError("tokenizer.json missing [CLS]/[SEP] in added_tokens");
}

UnigramTokenizer::~UnigramTokenizer() = default;
UnigramTokenizer::UnigramTokenizer(UnigramTokenizer&&) noexcept = default;
UnigramTokenizer& UnigramTokenizer::operator=(UnigramTokenizer&&) noexcept = default;

int UnigramTokenizer::vocabSize() const noexcept {
    return static_cast<int>(impl_->scores.size()) + static_cast<int>(impl_->addedTokens.size());
}

std::vector<std::string> UnigramTokenizer::pieces(const std::string& word) const {
    std::vector<std::string> out;
    const auto normalized = normalize(word);
    if (normalized.empty())
        return out;
    // An added token matches literally, with no metaspace prefix — this is why
    // "<alias>" survives as one unit instead of splitting into "<","alias",">".
    if (impl_->addedTokens.count(normalized)) {
        out.push_back(normalized);
        return out;
    }
    impl_->viterbi(metaspace(normalized), out);
    return out;
}

UnigramTokenizer::Encoding UnigramTokenizer::encodeWords(const std::vector<std::string>& words,
                                                         int maxLen) const {
    Encoding enc;
    enc.firstSubword.assign(words.size(), -1);
    enc.inputIds.push_back(impl_->clsId);

    // One slot must stay free for [SEP], so a word only fits if its pieces land
    // at index <= maxLen - 2.
    const size_t budget = maxLen > 1 ? static_cast<size_t>(maxLen) - 1 : 1;

    for (size_t w = 0; w < words.size(); ++w) {
        const auto ps = pieces(words[w]);
        if (ps.empty())
            continue;
        if (enc.inputIds.size() + ps.size() > budget)
            break;  // later words stay at -1 and fall back to "O"
        enc.firstSubword[w] = static_cast<int>(enc.inputIds.size());
        for (const auto& p : ps) {
            auto added = impl_->addedTokens.find(p);
            if (added != impl_->addedTokens.end()) {
                enc.inputIds.push_back(added->second);
                continue;
            }
            auto it = impl_->tokenToId.find(p);
            enc.inputIds.push_back(it != impl_->tokenToId.end() ? it->second : impl_->unkId);
        }
    }

    enc.inputIds.push_back(impl_->sepId);
    enc.attentionMask.assign(enc.inputIds.size(), 1);
    return enc;
}

}  // namespace magda::cmdmodel
