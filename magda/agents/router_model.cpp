#include "router_model.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "router_model_data.hpp"

// Hand-written float32 inference + tokenization for the on-device console
// router. Faithful port of the Python POC (prototypes/router-model-poc/:
// router/text.py, router/net.py, router/reference.py). It must stay
// byte-identical to that reference — tests/test_router_model.cpp locks it to
// the committed parity fixture.

namespace magda {
namespace {

namespace d = routermodel::data;

// ---------------------------------------------------------------------------
// UTF-8 <-> codepoints. The router is multilingual, so every rule below is
// expressed over codepoints (as in Python) rather than bytes.
// ---------------------------------------------------------------------------
struct Decoded {
    std::vector<uint32_t> cps;
    std::vector<size_t> offsets;  // byte offset of each codepoint
    size_t bytes = 0;             // total byte length
};

Decoded decodeUtf8(const std::string& s) {
    Decoded out;
    out.bytes = s.size();
    size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        uint32_t cp = b0;
        size_t len = 1;
        if (b0 >= 0xF0)
            len = 4;
        else if (b0 >= 0xE0)
            len = 3;
        else if (b0 >= 0xC0)
            len = 2;
        if (len > 1 && i + len <= s.size()) {
            cp = b0 & (0xFFu >> (len + 1));
            bool ok = true;
            for (size_t k = 1; k < len; ++k) {
                const auto bk = static_cast<unsigned char>(s[i + k]);
                if ((bk & 0xC0) != 0x80) {
                    ok = false;
                    break;
                }
                cp = (cp << 6) | (bk & 0x3F);
            }
            if (!ok) {  // malformed — fall back to a single opaque byte
                cp = b0;
                len = 1;
            }
        } else if (len > 1) {
            cp = b0;
            len = 1;
        }
        out.cps.push_back(cp);
        out.offsets.push_back(i);
        i += len;
    }
    return out;
}

void appendUtf8(std::string& out, uint32_t cp) {
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
}

// ---------------------------------------------------------------------------
// Codepoint classes — mirror router/text.py exactly.
// ---------------------------------------------------------------------------
struct Range {
    uint32_t lo, hi;
};

// One token per codepoint: kana, CJK ideographs (+ extensions), halfwidth kana.
constexpr Range kCjk[] = {
    {0x3040, 0x30FF}, {0x31F0, 0x31FF}, {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},
    {0xF900, 0xFAFF}, {0xFF66, 0xFF9F}, {0x20000, 0x2FA1F},
};

// Non-ASCII blocks that are punctuation/symbols rather than letters.
constexpr Range kSymbols[] = {
    {0x2000, 0x206F}, {0x2190, 0x2BFF}, {0x3000, 0x303F},   {0xFE00, 0xFE6F},
    {0xFF01, 0xFF65}, {0xFFA0, 0xFFFF}, {0x1F000, 0x1FFFF},
};

template <size_t N> bool inRanges(uint32_t cp, const Range (&ranges)[N]) {
    for (const auto& r : ranges)
        if (cp >= r.lo && cp <= r.hi)
            return true;
    return false;
}

bool isCjk(uint32_t cp) {
    return inRanges(cp, kCjk);
}

bool isWord(uint32_t cp) {
    if (cp < 0x80)
        return (cp >= 0x41 && cp <= 0x5A) || (cp >= 0x61 && cp <= 0x7A) ||
               (cp >= 0x30 && cp <= 0x39) || cp == 0x5F || cp == 0x27 || cp == 0x2D;
    return !isCjk(cp) && !inRanges(cp, kSymbols);
}

bool isDigit(uint32_t cp) {
    return cp >= 0x30 && cp <= 0x39;
}

// text.fold: lowercase ASCII + Latin-1 supplement + Cyrillic, pass the rest
// through. That covers every cased script in MAGDA's locale set; a full Unicode
// case table would have to be mirrored on both sides for no practical gain.
std::string fold(const std::string& s) {
    const auto dec = decodeUtf8(s);
    std::string out;
    out.reserve(s.size());
    for (uint32_t cp : dec.cps) {
        if (cp >= 0x41 && cp <= 0x5A)
            cp += 0x20;
        else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)
            cp += 0x20;
        else if (cp >= 0x410 && cp <= 0x42F)
            cp += 0x20;
        else if (cp >= 0x400 && cp <= 0x40F)
            cp += 0x50;
        appendUtf8(out, cp);
    }
    return out;
}

// The app expands "@mention" plugin refs into the DSL alias token "<mention>"
// before an agent sees the message. Restore the "@" surface the model was
// trained on. No-op on plain text, so the parity fixture stays byte-stable.
std::string expandAliasBrackets(const std::string& text) {
    const auto dec = decodeUtf8(text);
    const size_t n = dec.cps.size();
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < n) {
        if (dec.cps[i] == '<') {
            size_t j = i + 1;
            while (j < n && dec.cps[j] < 0x80 && isWord(dec.cps[j]))
                ++j;
            if (j > i + 1 && j < n && dec.cps[j] == '>') {
                out += '@';
                out += text.substr(dec.offsets[i + 1], dec.offsets[j] - dec.offsets[i + 1]);
                i = j + 1;
                continue;
            }
        }
        const size_t end = (i + 1 < n) ? dec.offsets[i + 1] : dec.bytes;
        out += text.substr(dec.offsets[i], end - dec.offsets[i]);
        ++i;
    }
    return out;
}

// text.canon: plugin references collapse to one opaque token, so the router
// never learns plugin identities and the vocab never grows with the registry.
std::string canonToken(const std::string& token) {
    if (!token.empty() && token[0] == '@')
        return token.find('.') != std::string::npos ? "<alias.param>" : "<alias>";
    return fold(token);
}

}  // namespace

// ---------------------------------------------------------------------------
std::vector<std::string> RouterModel::tokenize(const std::string& text) const {
    const auto dec = decodeUtf8(text);
    const size_t n = dec.cps.size();
    auto byteAt = [&](size_t idx) { return idx < n ? dec.offsets[idx] : dec.bytes; };

    std::vector<std::string> toks;
    size_t i = 0;
    while (i < n) {
        if (isCjk(dec.cps[i])) {  // no spaces in these scripts — one token each
            toks.push_back(text.substr(byteAt(i), byteAt(i + 1) - byteAt(i)));
            ++i;
            continue;
        }
        const size_t start = i;
        size_t j = i;
        if (dec.cps[j] == '@' || dec.cps[j] == '#')
            ++j;  // optional sigil
        const size_t core = j;
        while (j < n && isWord(dec.cps[j]))
            ++j;
        if (j == core) {  // nothing but a sigil / separator here
            ++i;
            continue;
        }
        // optional decimal suffix: '.' followed by one or more digits
        if (j + 1 < n && dec.cps[j] == '.' && isDigit(dec.cps[j + 1])) {
            ++j;
            while (j < n && isDigit(dec.cps[j]))
                ++j;
        }
        toks.push_back(text.substr(byteAt(start), byteAt(j) - byteAt(start)));
        i = j;
    }
    return toks;
}

RouterModel::RouterModel() {
    for (int i = 0; i < d::kVocabSize; ++i)
        vocab_.emplace(d::kVocab[i], i);
    auto it = vocab_.find("<UNK>");
    unkId_ = it != vocab_.end() ? it->second : 1;
}

std::string RouterModel::classify(const std::string& text) const {
    auto toks = tokenize(expandAliasBrackets(text));
    if (static_cast<int>(toks.size()) > d::kMaxLen)
        toks.resize(d::kMaxLen);
    const int L = static_cast<int>(toks.size());
    if (L == 0)
        return {};

    // Encode to ids, padded to kMaxLen (pad id 0).
    std::array<int, 64> ids{};  // kMaxLen == 32 <= 64
    for (int t = 0; t < L; ++t) {
        auto it = vocab_.find(canonToken(toks[t]));
        ids[t] = it != vocab_.end() ? it->second : unkId_;
    }

    const int E = d::kEmbed, H = d::kHidden, ML = d::kMaxLen;

    // Embedding → [E][ML] (channel-major, matching the .transpose(1,2)).
    std::vector<float> e(static_cast<size_t>(E) * ML, 0.0f);
    for (int t = 0; t < ML; ++t) {
        const float* row = &d::kEmbedWeight[ids[t] * E];
        for (int c = 0; c < E; ++c)
            e[c * ML + t] = row[c];
    }

    auto conv = [&](const std::vector<float>& in, int ci, int co, const float* W, const float* B,
                    int dilation) {
        std::vector<float> out(static_cast<size_t>(co) * ML, 0.0f);
        for (int oc = 0; oc < co; ++oc) {
            const float* wOc = &W[oc * ci * 3];
            for (int t = 0; t < ML; ++t) {
                float acc = B[oc];
                for (int k = 0; k < 3; ++k) {
                    const int tin = t + (k - 1) * dilation;
                    if (tin < 0 || tin >= ML)
                        continue;
                    for (int ic = 0; ic < ci; ++ic)
                        acc += wOc[ic * 3 + k] * in[ic * ML + tin];
                }
                out[oc * ML + t] = acc > 0.0f ? acc : 0.0f;  // ReLU
            }
        }
        return out;
    };

    const auto h1 = conv(e, E, H, d::kB1Weight, d::kB1Bias, 1);
    const auto h2 = conv(h1, H, H, d::kB2Weight, d::kB2Bias, 2);
    const auto h = conv(h2, H, H, d::kB3Weight, d::kB3Bias, 4);

    // Masked mean-pool over [0, L) → classification head.
    std::vector<float> pooled(static_cast<size_t>(H), 0.0f);
    for (int c = 0; c < H; ++c) {
        float sum = 0.0f;
        for (int t = 0; t < L; ++t)
            sum += h[c * ML + t];
        pooled[c] = sum / static_cast<float>(L);
    }

    int best = 0;
    float bestVal = 0.0f;
    for (int i = 0; i < d::kNumLabels; ++i) {
        float acc = d::kHeadBias[i];
        const float* w = &d::kHeadWeight[i * H];
        for (int c = 0; c < H; ++c)
            acc += w[c] * pooled[c];
        if (i == 0 || acc > bestVal) {
            bestVal = acc;
            best = i;
        }
    }
    return d::kLabels[best];
}

}  // namespace magda
