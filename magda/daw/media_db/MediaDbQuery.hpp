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

    // Free-text tag filter. Whitespace-separated tokens are AND-combined
    // (typing "drum 808" → tagged with both drum AND 808). Implemented via
    // FTS5 column-scoped MATCH on media_fts.tag_text so it scales with
    // library size.
    std::optional<std::string> tags;
};

struct QueryResult {
    std::int64_t fileId = -1;
    std::filesystem::path path;
    std::optional<std::string> displayName;
    std::string kind;
    std::string family;
    std::string shape;
    std::optional<double> bpm;
    std::optional<std::string> keyRoot;
    std::optional<std::string> keyScale;
    std::optional<double> durationS;
    bool userEdited = false;
    bool tagged = false;
    // size_bytes and mtime_ns as recorded at index time. The browser
    // compares these against an on-the-fly stat() to flag rows whose
    // backing file went missing or was modified since indexing.
    std::int64_t sizeBytes = 0;
    std::int64_t mtimeNs = 0;
    // All tags on this file regardless of source_model (indexer-derived
    // path tags AND user-added tags appear in the same list). Loaded via
    // GROUP_CONCAT in the main SELECT.
    std::vector<std::string> tags;
    float score = 0.0F;  // NaN when no text query (filter-only browse)
};

struct QueryWeights {
    // Defaults match the Python prototype: filename evidence (FTS / BM25)
    // tends to be more reliable than CLAP cosine on short percussive samples,
    // so text gets the slight edge.
    float audio = 0.45F;
    float text = 0.55F;
};

enum class QuerySortField {
    Default,
    Name,
    Family,
    Shape,
    Bpm,
    Key,
    Duration,
    Tags,
};

struct QuerySort {
    QuerySortField field = QuerySortField::Default;
    bool ascending = true;
};

class MediaDbQuery {
  public:
    // db: required. encoder + tokenizer: nullable. When either is null, the
    // semantic side is skipped — search degrades to "FTS BM25 + filters",
    // which is still useful (the no-AI-pack story).
    MediaDbQuery(MediaDatabase& db, ClapTextEncoder* textEncoder, RobertaTokenizer* tokenizer);

    // Run a search. If `text` is empty / nullopt and no explicit sort is
    // provided, returns filter-only browse ordered by indexed_at DESC.
    // `offset` skips the first N rows of the result list — used by the UI's
    // pagination footer to jump between pages without changing the underlying
    // query.
    std::vector<QueryResult> search(const std::optional<std::string>& text,
                                    const QueryFilters& filters, int limit = 20, int offset = 0,
                                    QueryWeights weights = {}, QuerySort sort = {}) const;

    // Audio-to-audio similarity. Loads the seed file's CLAP audio embedding
    // and ranks the filter-matched candidate set by cosine. No text encoder
    // or tokenizer needed — uses the embeddings already in the DB. The seed
    // itself is dropped from the result list. Returns empty if the seed has
    // no embedding (e.g. wasn't indexed with the Sample Analyzer installed).
    std::vector<QueryResult> similarTo(std::int64_t seedFileId, const QueryFilters& filters,
                                       int limit = 20, int offset = 0, QuerySort sort = {}) const;

    // Cheap precheck: does this file_id have a row in media_embedding?
    // Used by the UI to tell "seed has no embedding, re-index needed"
    // apart from "embedding exists but no neighbours matched filters".
    [[nodiscard]] bool hasEmbedding(std::int64_t fileId) const;

    // Total number of rows in media_embedding. Used by the similar-sounds
    // empty state to distinguish "library has no embeddings at all" from
    // "library has plenty but the filters exclude them".
    [[nodiscard]] int totalEmbeddings() const;

    // Total number of indexed files. Used by the browser's empty-state copy
    // to distinguish "library is genuinely empty" from "filters excluded
    // everything" — without it the UI would lie any time a filter is
    // active and matches nothing.
    [[nodiscard]] int totalFiles() const;

  private:
    MediaDatabase& db_;
    ClapTextEncoder* textEncoder_;
    RobertaTokenizer* tokenizer_;
};

}  // namespace magda::media
