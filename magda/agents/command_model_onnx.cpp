#include "command_model_onnx.hpp"

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

    #include <juce_core/juce_core.h>
    #include <onnxruntime_cxx_api.h>

    #include <algorithm>
    #include <cctype>
    #include <cstdlib>
    #include <string>
    #include <unordered_map>
    #include <vector>

    #include "../daw/core/AppPaths.hpp"
    #include "unigram_tokenizer.hpp"

// Port of prototypes/command-model-poc/model/encoder_parser.py:predict_dsl.
// The steps, and where each lives:
//
//   1. word-split          CommandModel::predict's tokenizer (shared, below)
//   2. canon @mentions     canonForEncoder, here
//   3. subword encode      UnigramTokenizer::encodeWords
//   4. forward pass        ONNX Runtime
//   5. tag per word        read slot logits at each word's FIRST subword
//   6. tags -> actions     CommandModel::renderPrediction
//   7. actions -> DSL      ditto
//
// Step 5 is the one that fails silently. A word may span several subwords, so
// reading tags positionally instead of at firstSubword shifts every tag after
// the first multi-piece word and quietly costs ~30 points of accuracy while
// still producing plausible DSL.

namespace magda {

namespace {

constexpr int kMaxLen = 64;  // subwords, matching model/data_encoder.py MAX_LEN

// Mirrors dataset/tagging.py:_TOK plus _split_glued_units. Kept byte-identical
// to command_model.cpp's tokenizer — both consume the same Python reference,
// and tests/test_command_model.cpp locks that one to it.
bool isCore(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '_' ||
           c == '\'' || c == '-';
}

std::string toLowerAscii(const std::string& s) {
    std::string out(s);
    for (auto& c : out) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z')
            c = static_cast<char>(u - 'A' + 'a');
    }
    return out;
}

bool splitGluedUnit(const std::string& tok, std::string& num, std::string& unit) {
    static const char* kUnits[] = {"db",    "bar",      "bars",      "beat",
                                   "beats", "semitone", "semitones", "st"};
    size_t i = 0;
    if (i < tok.size() && (tok[i] == '+' || tok[i] == '-'))
        ++i;
    const size_t digitsStart = i;
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9')
        ++i;
    if (i == digitsStart)
        return false;
    if (i < tok.size() && tok[i] == '.') {
        size_t frac = i + 1;
        while (frac < tok.size() && tok[frac] >= '0' && tok[frac] <= '9')
            ++frac;
        if (frac > i + 1)
            i = frac;
    }
    if (i == tok.size())
        return false;
    const std::string tail = toLowerAscii(tok.substr(i));
    for (const char* u : kUnits) {
        if (tail == u) {
            num = tok.substr(0, i);
            unit = tok.substr(i);
            return true;
        }
    }
    return false;
}

std::vector<std::string> tokenizeWords(const std::string& text) {
    std::vector<std::string> toks;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const size_t start = i;
        size_t j = i;
        if (text[j] == '@' || text[j] == '#')
            ++j;
        const size_t coreStart = j;
        while (j < n && isCore(text[j]))
            ++j;
        if (j == coreStart) {
            ++i;
            continue;
        }
        if (j < n && text[j] == '.' && j + 1 < n && text[j + 1] >= '0' && text[j + 1] <= '9') {
            ++j;
            while (j < n && text[j] >= '0' && text[j] <= '9')
                ++j;
        }
        const std::string tok = text.substr(start, j - start);
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

// model/data_encoder.py:canon — collapse plugin references to opaque
// placeholders so the model never learns plugin identities. Unlike the conv
// net's canon, case is preserved: these encoders are cased and capitalisation
// is real evidence about what is a name.
std::string canonForEncoder(const std::string& token) {
    if (!token.empty() && token[0] == '@')
        return token.find('.') != std::string::npos ? "<alias.param>" : "<alias>";
    return token;
}

// The app expands "@mention" into the DSL alias token "<mention>" before the
// model sees it (for the LLM path). Restore the "@" the model was trained on.
std::string expandAliasBrackets(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        if (text[i] == '<') {
            const size_t close = text.find('>', i + 1);
            if (close != std::string::npos && close > i + 1) {
                const std::string inner = text.substr(i + 1, close - i - 1);
                const bool simple =
                    std::all_of(inner.begin(), inner.end(), [](char c) { return isCore(c); });
                if (simple) {
                    out += "@" + inner;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i];
        ++i;
    }
    return out;
}

}  // namespace

struct CommandModelOnnx::Impl {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memoryInfo;
    cmdmodel::UnigramTokenizer tokenizer;
    std::vector<std::string> idToIntent;
    std::vector<std::string> idToTag;

    explicit Impl(const std::filesystem::path& dir)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-command-model"),
          memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          tokenizer(dir / "tokenizer.json") {
        // One predictable worker, matching ClapTextEncoder — this runs on a
        // background job already, not on the message thread.
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetInterOpNumThreads(1);
        session = Ort::Session(env, (dir / "command_model.onnx").c_str(), sessionOptions);

        const auto mapsPath = dir / "maps.json";
        auto json = juce::JSON::parse(juce::File(juce::String(mapsPath.string())));
        if (json.isVoid())
            throw CommandModelOnnxError("failed to parse " + mapsPath.string());

        // maps.json stores name -> id; invert into dense id-indexed tables.
        auto invert = [](const juce::var& obj, std::vector<std::string>& out) {
            auto* dyn = obj.getDynamicObject();
            if (dyn == nullptr)
                return;
            for (const auto& prop : dyn->getProperties()) {
                const int id = static_cast<int>(prop.value);
                if (id >= static_cast<int>(out.size()))
                    out.resize(static_cast<size_t>(id) + 1);
                out[static_cast<size_t>(id)] = prop.name.toString().toStdString();
            }
        };
        invert(json.getProperty("intents", juce::var()), idToIntent);
        invert(json.getProperty("tags", juce::var()), idToTag);
        if (idToIntent.empty() || idToTag.empty())
            throw CommandModelOnnxError("maps.json missing intents/tags");
    }
};

CommandModelOnnx::CommandModelOnnx(const std::filesystem::path& assetDir) {
    if (!isInstalled(assetDir))
        throw CommandModelOnnxError("command model assets not found in " + assetDir.string());
    try {
        impl_ = std::make_unique<Impl>(assetDir);
    } catch (const Ort::Exception& e) {
        throw CommandModelOnnxError(std::string("Ort failed to load model: ") + e.what());
    }
}

CommandModelOnnx::~CommandModelOnnx() = default;
CommandModelOnnx::CommandModelOnnx(CommandModelOnnx&&) noexcept = default;
CommandModelOnnx& CommandModelOnnx::operator=(CommandModelOnnx&&) noexcept = default;

bool CommandModelOnnx::isInstalled(const std::filesystem::path& dir) {
    return std::filesystem::exists(dir / "command_model.onnx") &&
           std::filesystem::exists(dir / "tokenizer.json") &&
           std::filesystem::exists(dir / "maps.json");
}

CommandModel::Prediction CommandModelOnnx::predict(const std::string& text) const {
    CommandModel::Prediction out;
    const auto words = tokenizeWords(expandAliasBrackets(text));
    out.tokens = words;
    if (words.empty())
        return out;

    std::vector<std::string> canoned;
    canoned.reserve(words.size());
    for (const auto& w : words)
        canoned.push_back(canonForEncoder(w));

    const auto enc = impl_->tokenizer.encodeWords(canoned, kMaxLen);
    const int64_t seq = static_cast<int64_t>(enc.inputIds.size());
    const std::array<int64_t, 2> shape{1, seq};

    auto& mem = impl_->memoryInfo;
    std::vector<int64_t> ids = enc.inputIds;
    std::vector<int64_t> mask = enc.attentionMask;
    std::array<Ort::Value, 2> inputs{
        Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), shape.data(), shape.size()),
        Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(), shape.data(),
                                          shape.size())};

    static const char* kInputNames[] = {"input_ids", "attention_mask"};
    static const char* kOutputNames[] = {"intent_logits", "slot_logits"};
    auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, kInputNames, inputs.data(),
                                      inputs.size(), kOutputNames, 2);

    // intent: argmax over [1, n_intents]
    const float* intentLogits = outputs[0].GetTensorData<float>();
    const auto intentCount = outputs[0].GetTensorTypeAndShapeInfo().GetShape().back();
    int bestIntent = 0;
    for (int64_t i = 1; i < intentCount; ++i)
        if (intentLogits[i] > intentLogits[bestIntent])
            bestIntent = static_cast<int>(i);
    if (bestIntent < static_cast<int>(impl_->idToIntent.size()))
        out.intent = impl_->idToIntent[static_cast<size_t>(bestIntent)];

    // slots: argmax per position over [1, seq, n_tags], read at each word's
    // FIRST subword so there is exactly one tag per word (see the header note).
    const float* slotLogits = outputs[1].GetTensorData<float>();
    const auto slotShape = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
    const int64_t nTags = slotShape.back();

    out.tags.assign(words.size(), "O");
    for (size_t w = 0; w < words.size(); ++w) {
        const int pos = enc.firstSubword[w];
        if (pos < 0 || pos >= seq)
            continue;  // truncated away — "O" is the safe fallback
        const float* row = slotLogits + static_cast<int64_t>(pos) * nTags;
        int64_t best = 0;
        for (int64_t t = 1; t < nTags; ++t)
            if (row[t] > row[best])
                best = t;
        if (best < static_cast<int64_t>(impl_->idToTag.size()))
            out.tags[w] = impl_->idToTag[static_cast<size_t>(best)];
    }
    return out;
}

std::string CommandModelOnnx::generate(const std::string& text) const {
    const auto p = predict(text);

    juce::String tagged;
    for (size_t i = 0; i < p.tokens.size(); ++i)
        tagged += (i ? " " : "") + juce::String(p.tokens[i]) + "/" + juce::String(p.tags[i]);
    DBG("MAGDA CommandModelOnnx: in=\"" + juce::String(text) +
        "\" intent=" + juce::String(p.intent) + " [" + tagged + "]");

    return CommandModel::renderPrediction(p);
}

}  // namespace magda

#endif  // MAGDA_HAVE_CLAP

// Path resolution needs no ONNX Runtime, so it lives outside the CLAP guard:
// the downloader and the settings UI still need to know where the bundle goes
// on builds where the backend itself is compiled out.
#include "../daw/core/AppPaths.hpp"

namespace magda {

std::filesystem::path CommandModelOnnx::defaultAssetDir() {
    if (const char* env = std::getenv("MAGDA_COMMAND_MODEL_DIR"))
        return {env};
    // <dataDir>/CommandModel/models — same shape as MediaDB/models and
    // StemSeparation/models, so every downloaded bundle sits in the same place
    // relative to its feature.
    return std::filesystem::path(magda::paths::dataDir()
                                     .getChildFile("CommandModel")
                                     .getChildFile("models")
                                     .getFullPathName()
                                     .toStdString());
}

}  // namespace magda
