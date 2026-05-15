// Hybrid search over the media DB (issue #768).
//
// Pulls together everything Phases A-E1 produced:
//   - scalar filters (kind, family, shape, tonal, bpm range, key, format)
//     against media_file
//   - FTS5 BM25 over media_fts (path + tag tokens)
//   - text-encoder cosine against media_embedding rows (only when a text
//     encoder + tokenizer are wired in)
//
// Mirrors prototypes/media_db/src/media_db/query.py: per-side max-normalise,
// weighted sum, top-N. Same DEFAULTS so the C++ runtime ranks like the Python
// prototype on the same DB.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace magda::media {

class MediaDatabase;
class ClapTextEncoder;
class RobertaTokenizer;

struct QueryFilters {
    std::optional<std::string> kind;    // "audio" | "preset" | "clip"
    std::optional<std::string> family;  // "drum", "vocal", ...
    std::optional<std::string> shape;   // "one-shot" | "loop" | "sustained"
    std::optional<bool> tonal;
    std::optional<double> bpmMin;
    std::optional<double> bpmMax;
    std::optional<std::string> keyRoot;
    std::optional<std::string> keyScale;
    std::optional<std::string> format;  // file extension lowercase
};

struct QueryResult {
    std::int64_t fileId = -1;
    std::filesystem::path path;
    std::string kind;
    std::string family;
    std::string shape;
    std::optional<double> bpm;
    std::optional<std::string> keyRoot;
    std::optional<std::string> keyScale;
    std::optional<double> durationS;
    float score = 0.0F;  // NaN when no text query (filter-only browse)
};

struct QueryWeights {
    // Defaults match the Python prototype: filename evidence (FTS / BM25)
    // tends to be more reliable than CLAP cosine on short percussive samples,
    // so text gets the slight edge.
    float audio = 0.45F;
    float text = 0.55F;
};

class MediaDbQuery {
  public:
    // db: required. encoder + tokenizer: nullable. When either is null, the
    // semantic side is skipped — search degrades to "FTS BM25 + filters",
    // which is still useful (the no-AI-pack story).
    MediaDbQuery(MediaDatabase& db, ClapTextEncoder* textEncoder, RobertaTokenizer* tokenizer);

    // Run a search. If `text` is empty / nullopt, returns filter-only browse
    // ordered by indexed_at DESC.
    std::vector<QueryResult> search(const std::optional<std::string>& text,
                                    const QueryFilters& filters, int limit = 20,
                                    QueryWeights weights = {}) const;

  private:
    MediaDatabase& db_;
    ClapTextEncoder* textEncoder_;
    RobertaTokenizer* tokenizer_;
};

}  // namespace magda::media
