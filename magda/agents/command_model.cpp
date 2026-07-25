#include "command_model.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "command_model_data.hpp"

// Hand-written float32 inference + pre/post-processing for the tiny command
// model. This is a faithful port of the Python POC
// (prototypes/command-model-poc/: model/net.py, dataset/tagging.py,
// magda_dsl/{dsl,vocab}.py). It must stay byte-identical to that reference —
// tests/test_command_model.cpp locks it to the committed 102-case fixture.

namespace magda {
namespace {

namespace d = cmdmodel::data;

// ---------------------------------------------------------------------------
// Small string helpers (ASCII, matching Python str semantics on the ASCII
// inputs the model sees).
// ---------------------------------------------------------------------------
bool isCore(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '_' ||
           c == '\'' || c == '-';
}

bool isWordChar(char c) {  // Python regex \w (ASCII)
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '_';
}

char lowerCh(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u >= 'A' && u <= 'Z')
        return static_cast<char>(u - 'A' + 'a');
    return c;
}

char upperCh(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u >= 'a' && u <= 'z')
        return static_cast<char>(u - 'a' + 'A');
    return c;
}

std::string toLower(const std::string& s) {
    std::string out(s);
    for (auto& c : out)
        c = lowerCh(c);
    return out;
}

std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

// Python: w.islower() — at least one cased char, no uppercase cased char.
bool isLowerWord(const std::string& w) {
    bool anyLower = false;
    for (char c : w) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z')
            return false;
        if (u >= 'a' && u <= 'z')
            anyLower = true;
    }
    return anyLower;
}

// Python str.capitalize(): first char upper, remainder lower.
std::string capitalizeWord(const std::string& w) {
    if (w.empty())
        return w;
    std::string out;
    out.reserve(w.size());
    out += upperCh(w[0]);
    for (size_t i = 1; i < w.size(); ++i)
        out += lowerCh(w[i]);
    return out;
}

// tagging._canon_name: capitalise all-lowercase words, leave others untouched.
std::string canonName(const std::string& s) {
    auto words = splitWs(s);
    for (auto& w : words)
        if (isLowerWord(w))
            w = capitalizeWord(w);
    return join(words, " ");
}

// Whole-word (\b..\b) search, ASCII word chars. Returns true if `needle`
// occurs in `hay` bounded by non-word chars on both sides.
bool wordSearch(const std::string& hay, const std::string& needle) {
    if (needle.empty())
        return false;
    size_t from = 0;
    while (true) {
        size_t pos = hay.find(needle, from);
        if (pos == std::string::npos)
            return false;
        bool leftOk = (pos == 0) || !isWordChar(hay[pos - 1]);
        size_t end = pos + needle.size();
        bool rightOk = (end == hay.size()) || !isWordChar(hay[end]);
        // Only enforce boundaries where the needle edge is itself a word char.
        if ((!isWordChar(needle.front()) || leftOk) && (!isWordChar(needle.back()) || rightOk))
            return true;
        from = pos + 1;
    }
}

// ---------------------------------------------------------------------------
// Split a number glued to its unit ("-6db" -> "-6", "db") so the value can be
// tagged. Mirrors dataset/tagging.py:_split_glued_units — same whitelist, and
// for the same reason: a general digits-then-letters split would wreck "16ths"
// (a grid phrase), "C3" (a pitch) and "@pro_q_3" (an alias).
// ---------------------------------------------------------------------------
bool splitGluedUnit(const std::string& tok, std::string& num, std::string& unit) {
    static const char* kUnits[] = {"db",    "bar",      "bars",      "beat",
                                   "beats", "semitone", "semitones", "st"};
    size_t i = 0;
    if (i < tok.size() && (tok[i] == '+' || tok[i] == '-'))
        ++i;
    size_t digitsStart = i;
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9')
        ++i;
    if (i == digitsStart)
        return false;  // no leading number
    if (i < tok.size() && tok[i] == '.') {
        size_t frac = i + 1;
        while (frac < tok.size() && tok[frac] >= '0' && tok[frac] <= '9')
            ++frac;
        if (frac > i + 1)
            i = frac;
    }
    if (i == tok.size())
        return false;  // pure number, nothing glued

    std::string tail = toLower(tok.substr(i));
    for (const char* u : kUnits) {
        if (tail == u) {
            num = tok.substr(0, i);
            unit = tok.substr(i);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Tokenizer — mirrors _TOK = r"[@#]?[A-Za-z0-9_'\-]+(?:\.[0-9]+)?" findall,
// then dataset/tagging.py:_split_glued_units.
// ---------------------------------------------------------------------------
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> toks;
    size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        size_t start = i;
        size_t j = i;
        if (text[j] == '@' || text[j] == '#')
            ++j;  // optional sigil
        size_t coreStart = j;
        while (j < n && isCore(text[j]))
            ++j;
        if (j == coreStart) {  // no core run → match fails here
            ++i;
            continue;
        }
        // optional decimal suffix: '.' followed by one or more digits
        if (j < n && text[j] == '.' && j + 1 < n &&
            static_cast<unsigned char>(text[j + 1]) >= '0' &&
            static_cast<unsigned char>(text[j + 1]) <= '9') {
            ++j;
            while (j < n && static_cast<unsigned char>(text[j]) >= '0' &&
                   static_cast<unsigned char>(text[j]) <= '9')
                ++j;
        }
        std::string tok = text.substr(start, j - start);
        std::string num, unit;
        if (splitGluedUnit(tok, num, unit)) {
            toks.push_back(num);
            toks.push_back(unit);
        } else {
            toks.push_back(tok);
        }
        i = j;
    }
    return toks;
}

// The app expands "@mention" plugin refs into the DSL alias token "<mention>"
// before the model sees them (for the LLM path). Restore the "@" sigil form the
// model was trained on, so "<fm_0>" is tagged as a plugin, not a track name.
// No-op on plain text (e.g. the parity fixture), so byte-parity is preserved.
std::string expandAliasBrackets(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0, n = text.size();
    while (i < n) {
        if (text[i] == '<') {
            size_t j = i + 1;
            while (j < n && isCore(text[j]))
                ++j;
            if (j > i + 1 && j < n && text[j] == '>') {
                out += '@';
                out += text.substr(i + 1, j - (i + 1));
                i = j + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// data.canon: collapse @-references to the opaque alias token; else lowercase.
std::string canonToken(const std::string& token) {
    if (!token.empty() && token[0] == '@')
        return token.find('.') != std::string::npos ? "<alias.param>" : "<alias>";
    return toLower(token);
}

std::string aliasToken(const std::string& surface) {
    // Python: "<" + surface.lstrip("@").strip("<>") + ">"
    size_t b = 0;
    while (b < surface.size() && surface[b] == '@')
        ++b;
    std::string s = surface.substr(b);
    size_t lo = 0, hi = s.size();
    while (lo < hi && (s[lo] == '<' || s[lo] == '>'))
        ++lo;
    while (hi > lo && (s[hi - 1] == '<' || s[hi - 1] == '>'))
        --hi;
    return "<" + s.substr(lo, hi - lo) + ">";
}

// ---------------------------------------------------------------------------
// Numeric parsing helpers.
// ---------------------------------------------------------------------------
// _parse_number: first [+-]?\d+(?:\.\d+)? match, else 0.
double parseNumber(const std::string& text) {
    size_t i = 0, n = text.size();
    while (i < n) {
        size_t j = i;
        if (text[j] == '+' || text[j] == '-')
            ++j;
        size_t digStart = j;
        while (j < n && std::isdigit(static_cast<unsigned char>(text[j])))
            ++j;
        if (j == digStart) {  // need at least one leading digit
            ++i;
            continue;
        }
        if (j < n && text[j] == '.' && j + 1 < n &&
            std::isdigit(static_cast<unsigned char>(text[j + 1]))) {
            ++j;
            while (j < n && std::isdigit(static_cast<unsigned char>(text[j])))
                ++j;
        }
        return std::stod(text.substr(i, j - i));
    }
    return 0.0;
}

// Format a double the way Python's "{:g}" does (identical to C "%g").
std::string fmtG(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return std::string(buf);
}

struct Phrase {
    double value;
    std::vector<const char*> spellings;
};

const std::vector<Phrase>& panPhrases() {
    static const std::vector<Phrase> p = {
        {-1.0, {"hard left"}},      {-0.5, {"left"}},
        {-0.25, {"slightly left"}}, {0.0, {"center", "centre", "middle"}},
        {0.25, {"slightly right"}}, {0.5, {"right"}},
        {1.0, {"hard right"}},
    };
    return p;
}

double parsePan(const std::string& text) {
    std::string t = toLower(text);
    // trim
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
    for (const auto& p : panPhrases())
        for (const char* s : p.spellings)
            if (t == s)
                return p.value;
    return parseNumber(t);
}

// GRID_PHRASES: grid value → surface spellings.
double parseGrid(const std::string& text) {
    static const std::vector<Phrase> grids = {
        {0.125, {"32nd", "32nds"}},
        {0.25, {"16th", "16ths"}},
        {0.5, {"8th", "8ths", "eighth", "eighths"}},
        {1.0, {"quarter", "quarters"}},
    };
    std::string t = toLower(text);
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
    for (const auto& g : grids)
        for (const char* s : g.spellings)
            if (t == s)
                return g.value;
    double n = parseNumber(t);
    return n != 0.0 ? n : 0.25;
}

std::string canonPitch(const std::string& text) {
    // trim
    size_t a = text.find_first_not_of(" \t\r\n");
    size_t b = text.find_last_not_of(" \t\r\n");
    std::string t = (a == std::string::npos) ? "" : text.substr(a, b - a + 1);
    // all digits → MIDI number, unchanged
    if (!t.empty() && std::all_of(t.begin(), t.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c));
        }))
        return t;
    // [a-gA-G][b]?[0-9]
    if (t.size() == 2 || t.size() == 3) {
        char note = t[0];
        bool noteOk = (note >= 'a' && note <= 'g') || (note >= 'A' && note <= 'G');
        if (noteOk) {
            std::string flat, digit;
            size_t idx = 1;
            if (t.size() == 3) {
                if (t[1] == 'b')
                    flat = "b";
                else
                    noteOk = false;
                idx = 2;
            }
            if (noteOk && idx < t.size() && std::isdigit(static_cast<unsigned char>(t[idx]))) {
                digit = std::string(1, t[idx]);
                std::string out;
                out += upperCh(note);
                out += flat;  // already lowercase
                out += digit;
                return out;
            }
        }
    }
    // fallback: upper()
    std::string up;
    for (char c : t)
        up += upperCh(c);
    return up;
}

const char* resolveColor(const std::string& word) {
    static const std::array<std::pair<const char*, const char*>, 10> colors = {{
        {"red", "#ff5a36"},
        {"orange", "#ff8c42"},
        {"yellow", "#ffd23f"},
        {"green", "#3ddc84"},
        {"teal", "#1abc9c"},
        {"blue", "#44c7ff"},
        {"purple", "#9b59b6"},
        {"pink", "#ff6ad5"},
        {"grey", "#8a8a8a"},
        {"gray", "#8a8a8a"},
    }};
    std::string t = toLower(word);
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
    for (const auto& c : colors)
        if (t == c.first)
            return c.second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// BIO span collection (tagging._spans).
// ---------------------------------------------------------------------------
struct Spans {
    std::unordered_map<std::string, std::vector<std::string>> m;
    bool has(const std::string& k) const {
        return m.count(k) && !m.at(k).empty();
    }
    std::string first(const std::string& k, const std::string& dflt = "") const {
        auto it = m.find(k);
        return (it != m.end() && !it->second.empty()) ? it->second.front() : dflt;
    }
    const std::vector<std::string>& list(const std::string& k) const {
        static const std::vector<std::string> empty;
        auto it = m.find(k);
        return it != m.end() ? it->second : empty;
    }
};

Spans collectSpans(const std::vector<std::string>& tokens, const std::vector<std::string>& tags) {
    Spans out;
    std::string curSlot;
    std::vector<std::string> cur;
    auto flush = [&]() {
        if (!curSlot.empty())
            out.m[curSlot].push_back(join(cur, " "));
        curSlot.clear();
        cur.clear();
    };
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& tg = tags[i];
        if (tg.rfind("B-", 0) == 0) {
            flush();
            curSlot = tg.substr(2);
            cur = {tokens[i]};
        } else if (tg.rfind("I-", 0) == 0 && curSlot == tg.substr(2)) {
            cur.push_back(tokens[i]);
        } else {
            flush();
        }
    }
    flush();
    return out;
}

// ---------------------------------------------------------------------------
// Token-level fallbacks (tagging.py).
// ---------------------------------------------------------------------------
const std::vector<std::string>& trackNameHints() {
    static const std::vector<std::string> hints = {
        "reese bass", "sub bass", "top loop", "bass",  "drums", "lead",    "pads",  "vocals",
        "kick",       "snare",    "hats",     "pluck", "arp",   "strings", "brass", "guitar",
        "perc",       "keys",     "synth",    "fx",    "sub",   "vox",     "choir",
    };
    return hints;
}

std::string trackNameFromTokens(const std::vector<std::string>& tokens) {
    std::string text = toLower(join(tokens, " "));
    std::vector<std::string> byLen = trackNameHints();
    std::stable_sort(byLen.begin(), byLen.end(), [](const std::string& a, const std::string& b) {
        return a.size() > b.size();
    });
    for (const auto& name : byLen)
        if (wordSearch(text, name))
            return canonName(name);
    return "";
}

int subseq(const std::vector<std::string>& lower, const std::vector<std::string>& words) {
    if (words.empty())
        return -1;
    if (words.size() > lower.size())
        return -1;
    for (size_t i = 0; i + words.size() <= lower.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < words.size(); ++j)
            if (lower[i + j] != words[j]) {
                ok = false;
                break;
            }
        if (ok)
            return static_cast<int>(i);
    }
    return -1;
}

std::string renameTargetFromTokens(const std::vector<std::string>& tokens) {
    std::vector<std::string> lower;
    for (const auto& t : tokens)
        lower.push_back(toLower(t));
    static const std::vector<std::vector<std::string>> phrases = {
        {"rename", "them", "to"}, {"rename", "to"}, {"call", "them"}, {"to"}};
    for (const auto& phrase : phrases) {
        int i = subseq(lower, phrase);
        if (i >= 0) {
            std::vector<std::string> tail(tokens.begin() + i + phrase.size(), tokens.end());
            if (!tail.empty())
                return canonName(join(tail, " "));
        }
    }
    return "";
}

std::vector<std::string> pluginsFromTokens(const std::vector<std::string>& tokens) {
    std::vector<std::string> out;
    for (const auto& t : tokens) {
        if (!t.empty() && t[0] == '@') {
            std::string tok = aliasToken(t);
            if (std::find(out.begin(), out.end(), tok) == out.end())
                out.push_back(tok);
        }
    }
    return out;
}

// tagging._refine_intent
std::string refineIntent(const std::string& intent, const std::vector<std::string>& tokens) {
    if (intent != "delete_track" && intent != "mute_track" && intent != "solo_track")
        return intent;
    std::string text = toLower(join(tokens, " "));
    if (wordSearch(text, "delete") || wordSearch(text, "remove"))
        return "delete_track";
    if (wordSearch(text, "mute") || wordSearch(text, "silence"))
        return "mute_track";
    if (wordSearch(text, "solo") || wordSearch(text, "isolate"))
        return "solo_track";
    return intent;
}

// ---------------------------------------------------------------------------
// DSL rendering (magda_dsl/dsl.py).
// ---------------------------------------------------------------------------
std::string q(const std::string& s) {
    return "\"" + s + "\"";
}

double valueOrFirstNumber(const Spans& s, const std::vector<std::string>& tokens) {
    if (s.has("VALUE"))
        return parseNumber(s.first("VALUE"));
    return parseNumber(join(tokens, " "));
}

}  // namespace

// ---------------------------------------------------------------------------
CommandModel::CommandModel() {
    for (int i = 0; i < d::kVocabSize; ++i)
        vocab_.emplace(d::kVocab[i], i);
    auto it = vocab_.find("<UNK>");
    unkId_ = it != vocab_.end() ? it->second : 1;
}

CommandModel::Prediction CommandModel::predict(const std::string& text) const {
    Prediction out;
    auto toks = tokenize(expandAliasBrackets(text));
    if (static_cast<int>(toks.size()) > d::kMaxLen)
        toks.resize(d::kMaxLen);
    const int L = static_cast<int>(toks.size());
    out.tokens = toks;
    if (L == 0)
        return out;

    // Encode to ids, padded to kMaxLen (pad id 0).
    std::array<int, 64> ids{};  // kMaxLen == 24 <= 64
    for (int t = 0; t < L; ++t) {
        auto it = vocab_.find(canonToken(toks[t]));
        ids[t] = it != vocab_.end() ? it->second : unkId_;
    }

    const int E = d::kEmbed, H = d::kHidden, ML = d::kMaxLen;

    // Embedding → [E][ML] (channel-major, matching the .transpose(1,2)).
    std::vector<float> e(E * ML, 0.0f);
    for (int t = 0; t < ML; ++t) {
        const float* row = &d::kEmbedWeight[ids[t] * E];
        for (int c = 0; c < E; ++c)
            e[c * ML + t] = row[c];
    }

    auto conv = [&](const std::vector<float>& in, int ci, int co, const float* W, const float* B,
                    int dilation) {
        std::vector<float> out(co * ML, 0.0f);
        for (int oc = 0; oc < co; ++oc) {
            const float* wOc = &W[oc * ci * 3];
            for (int t = 0; t < ML; ++t) {
                float acc = B[oc];
                for (int k = 0; k < 3; ++k) {
                    int tin = t + (k - 1) * dilation;
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

    auto h1 = conv(e, E, H, d::kB1Weight, d::kB1Bias, 1);
    auto h2 = conv(h1, H, H, d::kB2Weight, d::kB2Bias, 2);
    auto h = conv(h2, H, H, d::kB3Weight, d::kB3Bias, 4);

    // Per-token slot head over [0, L).
    out.tags.resize(L);
    for (int t = 0; t < L; ++t) {
        int best = 0;
        float bestVal = 0.0f;
        for (int tag = 0; tag < d::kNumTags; ++tag) {
            float acc = d::kSlotBias[tag];
            const float* w = &d::kSlotWeight[tag * H];
            for (int c = 0; c < H; ++c)
                acc += w[c] * h[c * ML + t];
            if (tag == 0 || acc > bestVal) {
                bestVal = acc;
                best = tag;
            }
        }
        out.tags[t] = d::kTags[best];
    }

    // Masked mean-pool over [0, L) → intent head.
    std::vector<float> pooled(H, 0.0f);
    for (int c = 0; c < H; ++c) {
        float sum = 0.0f;
        for (int t = 0; t < L; ++t)
            sum += h[c * ML + t];
        pooled[c] = sum / static_cast<float>(L);
    }
    int bestIntent = 0;
    float bestIntentVal = 0.0f;
    for (int i = 0; i < d::kNumIntents; ++i) {
        float acc = d::kIntentBias[i];
        const float* w = &d::kIntentWeight[i * H];
        for (int c = 0; c < H; ++c)
            acc += w[c] * pooled[c];
        if (i == 0 || acc > bestIntentVal) {
            bestIntentVal = acc;
            bestIntent = i;
        }
    }
    out.intent = d::kIntents[bestIntent];
    return out;
}

std::string CommandModel::generate(const std::string& text) const {
    Prediction p = predict(text);

    // Perception log: shows exactly how each word was tagged, so a surprising
    // DSL (e.g. a plugin read as a track name) is diagnosable at a glance.
    juce::String tagged;
    for (size_t i = 0; i < p.tokens.size(); ++i)
        tagged += (i ? " " : "") + juce::String(p.tokens[i]) + "/" + juce::String(p.tags[i]);
    DBG("MAGDA CommandModel: in=\"" + juce::String(text) + "\" intent=" + juce::String(p.intent) +
        " [" + tagged + "]");

    return renderPrediction(p);
}

std::string CommandModel::renderPrediction(const Prediction& p) {
    if (p.tokens.empty())
        return "";

    const auto& tokens = p.tokens;
    std::string intent = refineIntent(p.intent, tokens);
    Spans s = collectSpans(tokens, p.tags);

    std::string name = s.has("TRACK_NAME") ? canonName(s.first("TRACK_NAME")) : "";
    if (name.empty())
        name = trackNameFromTokens(tokens);

    auto trackRef = [&]() { return "track(name=" + q(name) + ")"; };
    std::vector<std::string> lines;

    if (intent == "create_track") {
        std::string head = "track(name=" + q(name) + ", new=true)";
        std::vector<std::string> plugs;
        for (const auto& pl : s.list("PLUGIN"))
            plugs.push_back(aliasToken(pl));
        if (plugs.empty())
            plugs = pluginsFromTokens(tokens);
        if (plugs.empty()) {
            lines.push_back(head);
        } else {
            lines.push_back(head + ".fx.add(name=" + q(plugs[0]) + ")");
            for (size_t i = 1; i < plugs.size(); ++i)
                lines.push_back("track(name=" + q(name) + ").fx.add(name=" + q(plugs[i]) + ")");
        }
    } else if (intent == "create_rack") {
        // rack.new on the track (empty name -> selection); devices chain onto
        // rack.new so the interpreter routes them inside the rack chain.
        std::string line = trackRef() + ".rack.new()";
        std::vector<std::string> plugs;
        for (const auto& pl : s.list("PLUGIN"))
            plugs.push_back(aliasToken(pl));
        if (plugs.empty())
            plugs = pluginsFromTokens(tokens);
        for (const auto& p : plugs)
            line += ".fx.add(name=" + q(p) + ")";
        lines.push_back(line);
    } else if (intent == "add_plugin") {
        std::vector<std::string> plugs;
        for (const auto& pl : s.list("PLUGIN"))
            plugs.push_back(aliasToken(pl));
        if (plugs.empty())
            plugs = pluginsFromTokens(tokens);
        if (plugs.empty()) {
            // Match Python q(None) -> "None" when nothing resolves.
            lines.push_back(trackRef() + ".fx.add(name=" + q("None") + ")");
        } else {
            for (const auto& p : plugs)  // fan out, one fx.add per plugin
                lines.push_back(trackRef() + ".fx.add(name=" + q(p) + ")");
        }
    } else if (intent == "rename_track") {
        lines.push_back(trackRef() + ".track.set(name=" + q(canonName(s.first("NEW_NAME"))) + ")");
    } else if (intent == "delete_track") {
        lines.push_back(trackRef() + ".delete()");
    } else if (intent == "mute_track") {
        lines.push_back(trackRef() + ".track.set(mute=true)");
    } else if (intent == "solo_track") {
        lines.push_back(trackRef() + ".track.set(solo=true)");
    } else if (intent == "select_all_clips") {
        lines.push_back(trackRef() + ".clips.select()");
    } else if (intent == "select_all_clips_rename") {
        std::string newName = s.has("NEW_NAME") ? canonName(s.first("NEW_NAME")) : "";
        if (newName.empty())
            newName = renameTargetFromTokens(tokens);
        lines.push_back(trackRef() + ".clips.select().clip.rename(name=" + q(newName) + ")");
    } else if (intent == "select_clips_named") {
        lines.push_back(trackRef() +
                        ".clips.select(clip.name == " + q(canonName(s.first("CLIP_NAME"))) + ")");
    } else if (intent == "select_clips_type") {
        lines.push_back(trackRef() +
                        ".clips.select(clip.type == " + q(toLower(s.first("CLIP_TYPE"))) + ")");
    } else if (intent == "select_clips_longer_than") {
        lines.push_back(trackRef() + ".clips.select(clip.length_bars > " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "select_clips_shorter_than") {
        lines.push_back(trackRef() + ".clips.select(clip.length_bars < " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "select_clips_length_at_least") {
        lines.push_back(trackRef() + ".clips.select(clip.length_bars >= " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "select_clips_length_at_most") {
        lines.push_back(trackRef() + ".clips.select(clip.length_bars <= " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "select_clips_length_exactly") {
        lines.push_back(trackRef() + ".clips.select(clip.length_bars == " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "select_clips_not_named") {
        lines.push_back(trackRef() +
                        ".clips.select(clip.name != " + q(canonName(s.first("CLIP_NAME"))) + ")");
    } else if (intent == "select_clips_starting_after") {
        lines.push_back(trackRef() + ".clips.select(clip.start_bar >= " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "select_clips_starting_before") {
        lines.push_back(trackRef() + ".clips.select(clip.start_bar <= " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "set_track_color") {
        const char* colour = nullptr;
        for (const auto& w : s.list("COLOR")) {
            const char* c = resolveColor(w);
            if (c)
                colour = c;
        }
        // Match Python q(None) -> "None" when no colour word resolves.
        lines.push_back(trackRef() + ".track.set(colour=" + q(colour ? colour : "None") + ")");
    } else if (intent == "set_track_volume") {
        double v = s.has("VALUE") ? parseNumber(s.first("VALUE")) : 0.0;
        lines.push_back(trackRef() + ".track.set(volume_db=" + fmtG(v) + ")");
    } else if (intent == "set_track_pan") {
        double v = s.has("VALUE") ? parsePan(s.first("VALUE")) : 0.0;
        lines.push_back(trackRef() + ".track.set(pan=" + fmtG(v) + ")");
    } else if (intent == "group_tracks") {
        std::vector<int> ids;
        for (const auto& x : s.list("TRACK_ID"))
            ids.push_back(static_cast<int>(std::lround(parseNumber(x))));
        std::string groupName = canonName(s.first("GROUP_NAME"));
        int anchor = ids.empty() ? 0 : ids[0];
        std::vector<std::string> idStrs;
        for (int x : ids)
            idStrs.push_back(std::to_string(x));
        lines.push_back("track(id=" + std::to_string(anchor) + ").track.group(name=" +
                        q(groupName) + ", tracks=" + q(join(idStrs, ",")) + ")");
    } else if (intent == "clip_new") {
        lines.push_back(trackRef() +
                        ".clip.new(length_bars=" + fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "clip_rename") {
        lines.push_back(trackRef() + ".clip.rename(name=" + q(canonName(s.first("CLIP_NAME"))) +
                        ")");
    } else if (intent == "clip_delete") {
        lines.push_back(trackRef() + ".clip.delete(index=" + fmtG(valueOrFirstNumber(s, tokens)) +
                        ")");
    } else if (intent == "track_move") {
        lines.push_back(trackRef() + ".track.move(index=" + fmtG(valueOrFirstNumber(s, tokens)) +
                        ")");
    } else if (intent == "notes_delete") {
        lines.push_back(trackRef() + ".notes.delete()");
    } else if (intent == "notes_transpose") {
        double n = std::abs(valueOrFirstNumber(s, tokens));
        bool down = false;  // Python checks exact token membership, not substring
        for (const auto& tk : tokens) {
            std::string lt = toLower(tk);
            // Mirrors dataset/tagging.py:reconstruct. "drop"/"below" joined the
            // set for #1847 — the templates only ever said "down"/"lower", so
            // "drop the bass notes 12 semitones" transposed UP.
            if (lt == "down" || lt == "lower" || lt == "drop" || lt == "dropped" || lt == "below")
                down = true;
        }
        if (down)
            n = -n;
        lines.push_back(trackRef() + ".notes.transpose(semitones=" + fmtG(n) + ")");
    } else if (intent == "notes_set_velocity") {
        lines.push_back(trackRef() +
                        ".notes.set_velocity(value=" + fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "notes_select_velocity_above") {
        lines.push_back(trackRef() + ".notes.select(note.velocity > " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "notes_select_velocity_below") {
        lines.push_back(trackRef() + ".notes.select(note.velocity < " +
                        fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "notes_resize") {
        lines.push_back(trackRef() + ".notes.resize(length=" + fmtG(valueOrFirstNumber(s, tokens)) +
                        ")");
    } else if (intent == "notes_quantize") {
        double grid = s.has("VALUE") ? parseGrid(s.first("VALUE")) : 0.25;
        lines.push_back(trackRef() + ".notes.quantize(grid=" + fmtG(grid) + ")");
    } else if (intent == "notes_set_pitch") {
        std::string pitch = s.has("PITCH") ? canonPitch(s.first("PITCH")) : canonPitch("C4");
        lines.push_back(trackRef() + ".notes.set_pitch(pitch=" + pitch + ")");
    } else if (intent == "notes_select_pitch") {
        std::string pitch = s.has("PITCH") ? canonPitch(s.first("PITCH")) : canonPitch("C4");
        lines.push_back(trackRef() + ".notes.select(note.pitch == " + pitch + ")");
    } else if (intent == "groove_set") {
        std::string tmpl = canonName(s.first("GROOVE_NAME"));
        lines.push_back("groove.set(template=" + q(tmpl) +
                        ", strength=" + fmtG(valueOrFirstNumber(s, tokens)) + ")");
    } else if (intent == "groove_list") {
        lines.push_back("groove.list()");
    } else {
        return "";  // unknown intent
    }

    return join(lines, "\n");
}

}  // namespace magda
