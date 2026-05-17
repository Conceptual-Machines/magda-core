#include "RobertaTokenizer.hpp"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <limits>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace magda::media {

namespace {

// ---- UTF-8 helpers --------------------------------------------------------

std::string encodeUtf8(int cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

int utf8CharLen(unsigned char c) {
    if ((c & 0x80U) == 0) {
        return 1;
    }
    if ((c & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((c & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((c & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;  // malformed; treat as single byte
}

// ---- GPT-2 byte → unicode table ------------------------------------------
//
// Maps every byte 0-255 to a printable UTF-8 codepoint. "Already-printable"
// bytes (33-126, 161-172, 174-255) map to themselves; the remaining 68
// bytes (control chars + space + a few oddballs) get sequential codepoints
// starting at U+0100. So byte 0x20 (space) → 'Ġ' (U+0120), the iconic
// space-prefix marker in GPT-2/RoBERTa BPE.

std::array<std::string, 256> buildByteEncoder() {
    auto alreadyPrintable = [](int b) {
        return (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
    };
    std::array<std::string, 256> table;
    int n = 0;
    for (int b = 0; b < 256; ++b) {
        if (alreadyPrintable(b)) {
            table[static_cast<size_t>(b)] = encodeUtf8(b);
        } else {
            table[static_cast<size_t>(b)] = encodeUtf8(256 + n);
            ++n;
        }
    }
    return table;
}

const std::array<std::string, 256>& byteEncoder() {
    static const auto table = buildByteEncoder();
    return table;
}

// ---- GPT-2 pre-tokenisation regex (ASCII-only) ---------------------------

const std::regex& preTokenRe() {
    static const std::regex kRe(
        R"('s|'t|'re|'ve|'m|'ll|'d| ?[A-Za-z]+| ?[0-9]+| ?[^\sA-Za-z0-9]+|\s+(?!\S)|\s+)");
    return kRe;
}

}  // namespace

struct RobertaTokenizer::Impl {
    struct PairHash {
        std::size_t operator()(const std::pair<std::string, std::string>& p) const noexcept {
            const std::size_t h1 = std::hash<std::string>{}(p.first);
            const std::size_t h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 << 1U);
        }
    };

    std::unordered_map<std::string, int64_t> vocab;
    // BPE merges with rank (lower = higher priority, applied first).
    std::unordered_map<std::pair<std::string, std::string>, int, PairHash> merges;
    int64_t bosId = -1;
    int64_t eosId = -1;
    int64_t padId = -1;
    int64_t unkId = -1;

    // BPE result cache per (byte-encoded) pretoken. Word reuse across queries
    // is the common case.
    mutable std::unordered_map<std::string, std::vector<std::string>> bpeCache;

    std::vector<std::string> bpe(const std::string& token) const {
        if (auto cached = bpeCache.find(token); cached != bpeCache.end()) {
            return cached->second;
        }

        std::vector<std::string> symbols;
        for (std::size_t i = 0; i < token.size();) {
            const int len = utf8CharLen(static_cast<unsigned char>(token[i]));
            symbols.push_back(token.substr(i, len));
            i += len;
        }

        while (symbols.size() > 1) {
            int bestRank = std::numeric_limits<int>::max();
            std::pair<std::string, std::string> bestPair;
            for (std::size_t i = 0; i + 1 < symbols.size(); ++i) {
                auto it = merges.find({symbols[i], symbols[i + 1]});
                if (it != merges.end() && it->second < bestRank) {
                    bestRank = it->second;
                    bestPair = it->first;
                }
            }
            if (bestRank == std::numeric_limits<int>::max()) {
                break;
            }

            // Apply the chosen merge wherever it appears; greedy left-to-right.
            std::vector<std::string> merged;
            merged.reserve(symbols.size());
            std::size_t i = 0;
            while (i < symbols.size()) {
                if (i + 1 < symbols.size() && symbols[i] == bestPair.first &&
                    symbols[i + 1] == bestPair.second) {
                    merged.push_back(symbols[i] + symbols[i + 1]);
                    i += 2;
                } else {
                    merged.push_back(symbols[i]);
                    ++i;
                }
            }
            symbols = std::move(merged);
        }

        bpeCache.emplace(token, symbols);
        return symbols;
    }
};

RobertaTokenizer::RobertaTokenizer(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw RobertaTokenizerError("tokenizer.json not found: " + path.string());
    }

    auto json = juce::JSON::parse(juce::File(juce::String(path.string())));
    if (json.isVoid()) {
        throw RobertaTokenizerError("failed to parse " + path.string());
    }

    impl_ = std::make_unique<Impl>();

    auto* modelObj = json.getProperty("model", juce::var()).getDynamicObject();
    if (modelObj == nullptr) {
        throw RobertaTokenizerError("tokenizer.json missing 'model' object");
    }

    if (auto* vocabObj = modelObj->getProperty("vocab").getDynamicObject()) {
        impl_->vocab.reserve(static_cast<size_t>(vocabObj->getProperties().size()));
        for (const auto& kv : vocabObj->getProperties()) {
            // Cast via juce::int64 first: juce::var has overloads to both
            // int and int64, and Linux's int64_t (= long int) doesn't
            // uniquely match either, so the direct static_cast<int64_t>(var)
            // is ambiguous on Linux. macOS / Windows pick juce::int64 (=
            // long long == int64_t) without complaining.
            impl_->vocab.emplace(kv.name.toString().toStdString(),
                                 static_cast<int64_t>(static_cast<juce::int64>(kv.value)));
        }
    }

    if (auto* mergesArr = modelObj->getProperty("merges").getArray()) {
        impl_->merges.reserve(static_cast<size_t>(mergesArr->size()));
        for (int i = 0; i < mergesArr->size(); ++i) {
            const auto& entry = (*mergesArr)[i];
            std::string a;
            std::string b;
            if (auto* pair = entry.getArray()) {
                // New "merges as [[a, b], ...]" format
                if (pair->size() < 2) {
                    continue;
                }
                a = (*pair)[0].toString().toStdString();
                b = (*pair)[1].toString().toStdString();
            } else {
                // Old "merges as ['a b', ...]" format
                const std::string m = entry.toString().toStdString();
                const auto sp = m.find(' ');
                if (sp == std::string::npos) {
                    continue;
                }
                a = m.substr(0, sp);
                b = m.substr(sp + 1);
            }
            impl_->merges.emplace(std::pair{a, b}, i);
        }
    }

    auto find = [&](const std::string& key) -> int64_t {
        if (auto it = impl_->vocab.find(key); it != impl_->vocab.end()) {
            return it->second;
        }
        return -1;
    };
    impl_->bosId = find("<s>");
    impl_->eosId = find("</s>");
    impl_->padId = find("<pad>");
    impl_->unkId = find("<unk>");
}

RobertaTokenizer::~RobertaTokenizer() = default;
RobertaTokenizer::RobertaTokenizer(RobertaTokenizer&&) noexcept = default;
RobertaTokenizer& RobertaTokenizer::operator=(RobertaTokenizer&&) noexcept = default;

int64_t RobertaTokenizer::bosId() const noexcept {
    return impl_ ? impl_->bosId : -1;
}
int64_t RobertaTokenizer::eosId() const noexcept {
    return impl_ ? impl_->eosId : -1;
}
int64_t RobertaTokenizer::padId() const noexcept {
    return impl_ ? impl_->padId : -1;
}
int64_t RobertaTokenizer::unkId() const noexcept {
    return impl_ ? impl_->unkId : -1;
}
std::size_t RobertaTokenizer::vocabSize() const noexcept {
    return impl_ ? impl_->vocab.size() : 0;
}

RobertaTokenizer::Encoded RobertaTokenizer::encode(const std::string& text, int maxLength) const {
    if (!impl_ || maxLength < 2) {
        return {};
    }
    const auto& byteEnc = byteEncoder();

    std::vector<int64_t> contentIds;
    contentIds.reserve(64);

    auto begin = std::sregex_iterator(text.begin(), text.end(), preTokenRe());
    const auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::string pretoken = it->str();

        // Byte-encode: each input byte gets remapped to a UTF-8 codepoint
        // that's safe to look up in the vocab. Spaces become "Ġ" etc.
        std::string encoded;
        encoded.reserve(pretoken.size() * 2);
        for (unsigned char c : pretoken) {
            encoded += byteEnc[c];
        }

        for (const auto& sym : impl_->bpe(encoded)) {
            if (auto vit = impl_->vocab.find(sym); vit != impl_->vocab.end()) {
                contentIds.push_back(vit->second);
            } else {
                contentIds.push_back(impl_->unkId);
            }
        }
    }

    // Truncate to leave room for BOS + EOS.
    const int capacity = maxLength - 2;
    if (static_cast<int>(contentIds.size()) > capacity) {
        contentIds.resize(static_cast<size_t>(capacity));
    }

    Encoded out;
    out.inputIds.reserve(static_cast<size_t>(maxLength));
    out.attentionMask.reserve(static_cast<size_t>(maxLength));

    out.inputIds.push_back(impl_->bosId);
    out.attentionMask.push_back(1);
    for (auto id : contentIds) {
        out.inputIds.push_back(id);
        out.attentionMask.push_back(1);
    }
    out.inputIds.push_back(impl_->eosId);
    out.attentionMask.push_back(1);

    while (static_cast<int>(out.inputIds.size()) < maxLength) {
        out.inputIds.push_back(impl_->padId);
        out.attentionMask.push_back(0);
    }
    return out;
}

}  // namespace magda::media
