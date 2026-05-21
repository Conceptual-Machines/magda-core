#include "MediaDbQuery.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ClapTextEncoder.hpp"
#include "MediaDatabase.hpp"
#include "RobertaTokenizer.hpp"

namespace magda::media {

namespace {

// ---- WHERE-clause builder -----------------------------------------------

struct BuiltWhere {
    std::string clause;
    // Bindings in order. Indices in `clause` are placeholders (?).
    std::vector<std::pair<std::string, std::string>> textBinds;  // (?, value)
    std::vector<std::pair<std::string, double>> doubleBinds;
    std::vector<std::pair<std::string, int>> intBinds;
};

BuiltWhere buildWhere(const QueryFilters& f) {
    BuiltWhere w;
    std::vector<std::string> clauses;
    clauses.emplace_back("1=1");

    auto addText = [&](const char* col, const std::optional<std::string>& v) {
        if (v) {
            clauses.emplace_back(std::string(col) + " = ?");
            w.textBinds.emplace_back(col, *v);
        }
    };
    auto addDouble = [&](const char* col, const char* op, std::optional<double> v) {
        if (v) {
            clauses.emplace_back(std::string(col) + " " + op + " ?");
            w.doubleBinds.emplace_back(col, *v);
        }
    };

    addText("kind", f.kind);
    addText("family", f.family);
    addText("shape", f.shape);
    addText("key_root", f.keyRoot);
    addText("key_scale", f.keyScale);
    addText("format", f.format);
    addDouble("bpm", ">=", f.bpmMin);
    addDouble("bpm", "<=", f.bpmMax);
    if (f.tonal) {
        clauses.emplace_back("tonal = ?");
        w.intBinds.emplace_back("tonal", *f.tonal ? 1 : 0);
    }
    // Tag filter — FTS5 column-scoped MATCH on tag_text. Multi-token
    // input is implicit AND in FTS5 (e.g. "drum 808" matches rows whose
    // tag_text contains both "drum" and "808"). Bound as text just like
    // any column equality, so bindAll order stays valid.
    if (f.tags && !f.tags->empty()) {
        clauses.emplace_back("id IN (SELECT rowid FROM media_fts WHERE tag_text MATCH ?)");
        w.textBinds.emplace_back("tags", *f.tags);
    }

    w.clause = clauses.front();
    for (size_t i = 1; i < clauses.size(); ++i) {
        w.clause += " AND " + clauses[i];
    }
    return w;
}

// Binds (text, double, int) in their original argument order, starting at
// `nextIdx`. Returns the next free index after the last bind.
int bindAll(sqlite3_stmt* stmt, int startIdx, const BuiltWhere& w) {
    int idx = startIdx;
    // textBinds/doubleBinds/intBinds were appended in document order, so the
    // overall sequence is text -> double -> int. The clause builder put them
    // in the same SQL order, so this just works.
    for (const auto& [_, v] : w.textBinds) {
        sqlite3_bind_text(stmt, idx++, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    for (const auto& [_, v] : w.doubleBinds) {
        sqlite3_bind_double(stmt, idx++, v);
    }
    for (const auto& [_, v] : w.intBinds) {
        sqlite3_bind_int(stmt, idx++, v);
    }
    return idx;
}

// ---- FTS MATCH-expression builder ---------------------------------------

// Turn a free-text query into an FTS5 MATCH expression: tokens joined by OR
// with prefix matching, e.g. "kick drum" -> "\"kick\"* OR \"drum\"*".
// Single-char tokens are kept (FTS5 supports any-length prefix terms) so
// incremental typing doesn't blank the result list on the first keystroke.
// Empty tokens (e.g. lone punctuation) are dropped.
std::string buildFtsQuery(const std::string& text) {
    static const std::regex kStrip(R"([^\w\-])");
    std::stringstream ss(text);
    std::string token;
    std::string out;
    while (ss >> token) {
        std::string cleaned = std::regex_replace(token, kStrip, "");
        if (cleaned.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " OR ";
        }
        out += '"' + cleaned + "\"*";
    }
    return out;
}

// ---- Score gathering -----------------------------------------------------

// Cap on rows pulled from FTS, also reused as the cosine candidate cap so
// the two pools line up. A common term like "kick" might match tens of
// thousands of rows in a large library; without this cap ftsScores would
// load all of them into the map before the cosine stage trims down.
constexpr int kCandidateCap = 5000;

std::unordered_map<std::int64_t, float> ftsScores(sqlite3* db, const std::string& text,
                                                  const BuiltWhere& w) {
    const std::string fts = buildFtsQuery(text);
    if (fts.empty()) {
        return {};
    }
    // ORDER BY bm25 ASC because bm25() returns negative values whose lower
    // magnitude == stronger match; we alias to -bm25 in the SELECT so the
    // map stores larger-is-better scores like everywhere else.
    const std::string sql = "SELECT f.id, -bm25(media_fts) AS s "
                            "FROM media_fts "
                            "JOIN media_file AS f ON f.id = media_fts.rowid "
                            "WHERE media_fts MATCH ? AND " +
                            w.clause + " ORDER BY bm25(media_fts) LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_text(stmt, 1, fts.c_str(), -1, SQLITE_TRANSIENT);
    const int after = bindAll(stmt, 2, w);
    sqlite3_bind_int(stmt, after, kCandidateCap);

    std::unordered_map<std::int64_t, float> scores;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        scores[sqlite3_column_int64(stmt, 0)] = static_cast<float>(sqlite3_column_double(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return scores;
}

// Cosine over an explicit candidate set. Two-stage retrieval keeps this
// bounded — without it, a 100k-row library would scan every embedding row
// for every keystroke. Caller picks candidates from FTS hits + a recency
// fallback (see search() below).
std::unordered_map<std::int64_t, float> audioScoresOnCandidates(
    sqlite3* db, const std::vector<float>& queryVec,
    const std::vector<std::int64_t>& candidateIds) {
    if (candidateIds.empty()) {
        return {};
    }
    std::string sql = "SELECT file_id, vector_dim, vector_blob "
                      "FROM media_embedding WHERE file_id IN (";
    for (size_t i = 0; i < candidateIds.size(); ++i) {
        sql += (i == 0 ? "?" : ",?");
    }
    sql += ")";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    for (size_t i = 0; i < candidateIds.size(); ++i) {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), candidateIds[i]);
    }

    std::unordered_map<std::int64_t, float> scores;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int dim = sqlite3_column_int(stmt, 1);
        if (dim != static_cast<int>(queryVec.size())) {
            continue;  // dim mismatch -> incompatible model_id; skip silently
        }
        const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 2));
        const int bytes = sqlite3_column_bytes(stmt, 2);
        if (bytes != dim * static_cast<int>(sizeof(float))) {
            continue;
        }
        std::vector<float> v(static_cast<size_t>(dim));
        std::memcpy(v.data(), blob, static_cast<size_t>(bytes));
        float dot = 0.0F;
        for (int i = 0; i < dim; ++i) {
            dot += queryVec[static_cast<size_t>(i)] * v[static_cast<size_t>(i)];
        }
        scores[sqlite3_column_int64(stmt, 0)] = dot;
    }
    sqlite3_finalize(stmt);
    return scores;
}

// Recency fallback: pick the N most-recently-indexed rows matching the
// active filters. Used when the FTS side returns nothing (e.g. a purely
// semantic query like "warm pad" with no filename overlap) — otherwise
// cosine would have no candidates and the user would see an empty list
// even though there are embedded files that might match semantically.
std::vector<std::int64_t> candidateIdsByRecency(sqlite3* db, const BuiltWhere& w, int limit) {
    const std::string sql =
        "SELECT id FROM media_file WHERE " + w.clause + " ORDER BY indexed_at DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    const int after = bindAll(stmt, 1, w);
    sqlite3_bind_int(stmt, after, limit);

    std::vector<std::int64_t> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return ids;
}

// ---- Hydrate a list of (score, file_id) into QueryResult rows -----------

std::optional<std::string> optString(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)));
}

std::optional<double> optDouble(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_double(stmt, col);
}

std::string tagsTextForSort(const QueryResult& r) {
    std::string out;
    for (const auto& tag : r.tags) {
        if (!out.empty()) {
            out += " ";
        }
        out += tag;
    }
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string lowerForSort(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string keyForSort(const QueryResult& r) {
    std::string key = r.keyRoot.value_or("");
    if (r.keyScale && !r.keyScale->empty()) {
        key += " " + *r.keyScale;
    }
    return lowerForSort(std::move(key));
}

bool hasSortValue(const QueryResult& r, QuerySortField field) {
    switch (field) {
        case QuerySortField::Bpm:
            return r.bpm.has_value();
        case QuerySortField::Key:
            return r.keyRoot.has_value();
        case QuerySortField::Duration:
            return r.durationS.has_value();
        case QuerySortField::Tags:
            return !r.tags.empty();
        default:
            return true;
    }
}

void sortResults(std::vector<QueryResult>& rows, QuerySort sort) {
    if (sort.field == QuerySortField::Default) {
        return;
    }

    std::stable_sort(rows.begin(), rows.end(), [sort](const QueryResult& a, const QueryResult& b) {
        const bool aHas = hasSortValue(a, sort.field);
        const bool bHas = hasSortValue(b, sort.field);
        if (aHas != bHas) {
            return aHas;  // keep missing values at the bottom in both directions
        }
        if (!aHas) {
            return a.fileId < b.fileId;
        }

        int cmp = 0;
        switch (sort.field) {
            case QuerySortField::Name:
                cmp =
                    lowerForSort(a.displayName.value_or(a.path.filename().string()))
                        .compare(lowerForSort(b.displayName.value_or(b.path.filename().string())));
                break;
            case QuerySortField::Family:
                cmp = lowerForSort(a.family).compare(lowerForSort(b.family));
                break;
            case QuerySortField::Shape:
                cmp = lowerForSort(a.shape).compare(lowerForSort(b.shape));
                break;
            case QuerySortField::Bpm:
                cmp = (*a.bpm < *b.bpm) ? -1 : ((*a.bpm > *b.bpm) ? 1 : 0);
                break;
            case QuerySortField::Key:
                cmp = keyForSort(a).compare(keyForSort(b));
                break;
            case QuerySortField::Duration:
                cmp = (*a.durationS < *b.durationS) ? -1 : ((*a.durationS > *b.durationS) ? 1 : 0);
                break;
            case QuerySortField::Tags:
                cmp = tagsTextForSort(a).compare(tagsTextForSort(b));
                break;
            case QuerySortField::Default:
                break;
        }
        if (cmp == 0) {
            return a.fileId < b.fileId;
        }
        return sort.ascending ? cmp < 0 : cmp > 0;
    });
}

std::string orderByFor(QuerySort sort) {
    const char* dir = sort.ascending ? "ASC" : "DESC";
    auto nullable = [&](const std::string& expr) {
        return expr + " IS NULL ASC, " + expr + " " + dir;
    };
    auto nullableLower = [&](const std::string& expr) {
        return expr + " IS NULL ASC, lower(" + expr + ") " + dir;
    };

    switch (sort.field) {
        case QuerySortField::Name:
            return "lower(COALESCE(display_name, path)) " + std::string(dir);
        case QuerySortField::Family:
            return nullableLower("family");
        case QuerySortField::Shape:
            return nullableLower("shape");
        case QuerySortField::Bpm:
            return nullable("COALESCE(bpm_user, bpm)");
        case QuerySortField::Key:
            return "COALESCE(key_root_user, key_root) IS NULL ASC, "
                   "lower(COALESCE(key_root_user, key_root, '') || ' ' || "
                   "COALESCE(key_scale_user, key_scale, '')) " +
                   std::string(dir);
        case QuerySortField::Duration:
            return nullable("duration_s");
        case QuerySortField::Tags:
            return "tags IS NULL ASC, lower(tags) " + std::string(dir);
        case QuerySortField::Default:
            return "indexed_at DESC";
    }
    return "indexed_at DESC";
}

std::vector<QueryResult> hydrate(sqlite3* db,
                                 const std::vector<std::pair<float, std::int64_t>>& scored) {
    std::vector<QueryResult> out;
    if (scored.empty()) {
        return out;
    }
    // Bulk-fetch with one IN-clause query, then re-order by the input vector.
    std::string sql =
        "SELECT id, path, display_name, kind, family, shape, COALESCE(bpm_user, bpm) AS bpm, "
        "COALESCE(key_root_user, key_root) AS key_root, "
        "COALESCE(key_scale_user, key_scale) AS key_scale, duration_s, "
        "(bpm_user IS NOT NULL OR key_root_user IS NOT NULL OR "
        " key_scale_user IS NOT NULL OR total_beats_user IS NOT NULL OR "
        " beat_mode_user IS NOT NULL OR warp_markers_json IS NOT NULL OR "
        " display_name IS NOT NULL OR EXISTS (SELECT 1 FROM media_tag "
        " WHERE file_id = media_file.id AND source_model = 'user')) AS user_edited, "
        "       (SELECT GROUP_CONCAT(tag, ', ') FROM media_tag "
        "        WHERE file_id = media_file.id) AS tags, "
        "       EXISTS (SELECT 1 FROM media_embedding "
        "               WHERE file_id = media_file.id) AS tagged, "
        "       size_bytes, mtime_ns "
        "FROM media_file WHERE id IN (";
    for (size_t i = 0; i < scored.size(); ++i) {
        sql += (i == 0 ? "?" : ",?");
    }
    sql += ")";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    for (size_t i = 0; i < scored.size(); ++i) {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), scored[i].second);
    }

    std::unordered_map<std::int64_t, QueryResult> byId;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueryResult r;
        r.fileId = sqlite3_column_int64(stmt, 0);
        r.path = std::filesystem::path(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        r.displayName = optString(stmt, 2);
        r.kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (auto t = optString(stmt, 4)) {
            r.family = *t;
        }
        if (auto t = optString(stmt, 5)) {
            r.shape = *t;
        }
        r.bpm = optDouble(stmt, 6);
        r.keyRoot = optString(stmt, 7);
        r.keyScale = optString(stmt, 8);
        r.durationS = optDouble(stmt, 9);
        r.userEdited = sqlite3_column_int(stmt, 10) != 0;
        if (auto tagsCsv = optString(stmt, 11)) {
            std::stringstream ts(*tagsCsv);
            std::string tag;
            while (std::getline(ts, tag, ',')) {
                // GROUP_CONCAT joins with ", " — trim leading space.
                while (!tag.empty() && tag.front() == ' ') {
                    tag.erase(tag.begin());
                }
                if (!tag.empty()) {
                    r.tags.push_back(tag);
                }
            }
        }
        r.tagged = sqlite3_column_int(stmt, 12) != 0;
        r.sizeBytes = sqlite3_column_int64(stmt, 13);
        r.mtimeNs = sqlite3_column_int64(stmt, 14);
        byId.emplace(r.fileId, std::move(r));
    }
    sqlite3_finalize(stmt);

    out.reserve(scored.size());
    for (const auto& [score, fid] : scored) {
        auto it = byId.find(fid);
        if (it == byId.end()) {
            continue;
        }
        it->second.score = score;
        out.push_back(it->second);
    }
    return out;
}

// ---- Filter-only browse --------------------------------------------------

std::vector<QueryResult> filterOnly(sqlite3* db, const BuiltWhere& w, int limit, int offset,
                                    QuerySort sort) {
    std::string sql =
        "SELECT id, path, display_name, kind, family, shape, COALESCE(bpm_user, bpm) AS bpm, "
        "COALESCE(key_root_user, key_root) AS key_root, "
        "COALESCE(key_scale_user, key_scale) AS key_scale, duration_s, "
        "(bpm_user IS NOT NULL OR key_root_user IS NOT NULL OR "
        " key_scale_user IS NOT NULL OR total_beats_user IS NOT NULL OR "
        " beat_mode_user IS NOT NULL OR warp_markers_json IS NOT NULL OR "
        " display_name IS NOT NULL OR EXISTS (SELECT 1 FROM media_tag "
        " WHERE file_id = media_file.id AND source_model = 'user')) AS user_edited, "
        "       (SELECT GROUP_CONCAT(tag, ', ') FROM media_tag "
        "        WHERE file_id = media_file.id) AS tags, "
        "       EXISTS (SELECT 1 FROM media_embedding "
        "               WHERE file_id = media_file.id) AS tagged, "
        "       size_bytes, mtime_ns "
        "FROM media_file WHERE " +
        w.clause + " ORDER BY " +
        (sort.field == QuerySortField::Default ? orderByFor(sort) : "indexed_at DESC");
    if (sort.field == QuerySortField::Default) {
        sql += " LIMIT ? OFFSET ?";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    const int after = bindAll(stmt, 1, w);
    if (sort.field == QuerySortField::Default) {
        sqlite3_bind_int(stmt, after, limit);
        sqlite3_bind_int(stmt, after + 1, offset);
    }

    std::vector<QueryResult> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueryResult r;
        r.fileId = sqlite3_column_int64(stmt, 0);
        r.path = std::filesystem::path(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        r.displayName = optString(stmt, 2);
        r.kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (auto t = optString(stmt, 4)) {
            r.family = *t;
        }
        if (auto t = optString(stmt, 5)) {
            r.shape = *t;
        }
        r.bpm = optDouble(stmt, 6);
        r.keyRoot = optString(stmt, 7);
        r.keyScale = optString(stmt, 8);
        r.durationS = optDouble(stmt, 9);
        r.userEdited = sqlite3_column_int(stmt, 10) != 0;
        if (auto tagsCsv = optString(stmt, 11)) {
            std::stringstream ts(*tagsCsv);
            std::string tag;
            while (std::getline(ts, tag, ',')) {
                while (!tag.empty() && tag.front() == ' ') {
                    tag.erase(tag.begin());
                }
                if (!tag.empty()) {
                    r.tags.push_back(tag);
                }
            }
        }
        r.tagged = sqlite3_column_int(stmt, 12) != 0;
        r.sizeBytes = sqlite3_column_int64(stmt, 13);
        r.mtimeNs = sqlite3_column_int64(stmt, 14);
        r.score = std::numeric_limits<float>::quiet_NaN();
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    if (sort.field != QuerySortField::Default) {
        sortResults(out, sort);
        if (offset >= static_cast<int>(out.size())) {
            return {};
        }
        out.erase(out.begin(), out.begin() + offset);
        if (static_cast<int>(out.size()) > limit) {
            out.resize(static_cast<size_t>(limit));
        }
    }
    return out;
}

}  // namespace

// ---- Public --------------------------------------------------------------

MediaDbQuery::MediaDbQuery(MediaDatabase& db, ClapTextEncoder* textEncoder,
                           RobertaTokenizer* tokenizer)
    : db_(db), textEncoder_(textEncoder), tokenizer_(tokenizer) {}

std::vector<QueryResult> MediaDbQuery::search(const std::optional<std::string>& text,
                                              const QueryFilters& filters, int limit, int offset,
                                              QueryWeights weights, QuerySort sort) const {
    sqlite3* sql = db_.handle();
    const BuiltWhere where = buildWhere(filters);
    if (limit < 0) {
        limit = 0;
    }
    if (offset < 0) {
        offset = 0;
    }

    if (!text || text->empty()) {
        return filterOnly(sql, where, limit, offset, sort);
    }

    // Stage 1: cheap FTS BM25 over path / tag tokens. This is also the
    // primary candidate source for stage 2.
    const auto bm25 = ftsScores(sql, *text, where);

    // Stage 2: cosine over a bounded candidate set. The full media_embedding
    // table can be tens of MB for a real library; scanning it per keystroke
    // is the dominant search cost. Prefer FTS hits as candidates; if there
    // are none (purely semantic query with no filename overlap) fall back to
    // the most-recently-indexed rows matching the filters.
    std::vector<std::int64_t> cosineCandidates;
    cosineCandidates.reserve(bm25.size());
    if (!bm25.empty()) {
        // FTS already capped at kCandidateCap and ordered by score, so we
        // can just take the ids directly — no extra sort needed here.
        cosineCandidates.reserve(bm25.size());
        for (const auto& [id, _] : bm25) {
            cosineCandidates.push_back(id);
        }
    } else if (textEncoder_ && tokenizer_) {
        cosineCandidates = candidateIdsByRecency(sql, where, kCandidateCap);
    }

    std::unordered_map<std::int64_t, float> audio;
    if (textEncoder_ && tokenizer_ && !cosineCandidates.empty()) {
        const auto enc = tokenizer_->encode(*text);
        if (!enc.inputIds.empty()) {
            try {
                auto qvec = textEncoder_->embedTokens(enc.inputIds, enc.attentionMask);
                if (!qvec.empty()) {
                    audio = audioScoresOnCandidates(sql, qvec, cosineCandidates);
                }
            } catch (const ClapTextEncoderError&) {
                // Treat semantic side as unavailable for this query.
            }
        }
    }

    std::unordered_set<std::int64_t> candidates;
    for (const auto& [id, _] : audio) {
        candidates.insert(id);
    }
    for (const auto& [id, _] : bm25) {
        candidates.insert(id);
    }
    if (candidates.empty()) {
        return {};
    }

    float aMax = 0.0F;
    for (const auto& [_, v] : audio) {
        aMax = std::max(aMax, v);
    }
    float tMax = 0.0F;
    for (const auto& [_, v] : bm25) {
        tMax = std::max(tMax, v);
    }
    if (aMax <= 0.0F) {
        aMax = 1.0F;
    }
    if (tMax <= 0.0F) {
        tMax = 1.0F;
    }

    std::vector<std::pair<float, std::int64_t>> combined;
    combined.reserve(candidates.size());
    for (std::int64_t id : candidates) {
        float a = 0.0F;
        if (auto it = audio.find(id); it != audio.end()) {
            a = std::max(0.0F, it->second) / aMax;
        }
        float t = 0.0F;
        if (auto it = bm25.find(id); it != bm25.end()) {
            t = std::max(0.0F, it->second) / tMax;
        }
        combined.emplace_back(weights.audio * a + weights.text * t, id);
    }
    std::sort(combined.begin(), combined.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (sort.field != QuerySortField::Default) {
        auto sorted = hydrate(sql, combined);
        sortResults(sorted, sort);
        if (offset >= static_cast<int>(sorted.size())) {
            return {};
        }
        sorted.erase(sorted.begin(), sorted.begin() + offset);
        if (static_cast<int>(sorted.size()) > limit) {
            sorted.resize(static_cast<size_t>(limit));
        }
        return sorted;
    }
    // Slice [offset, offset + limit] off the ranked list. Cosine over the
    // candidate set produced the full ordering already; paging is a pure
    // window into that, so it costs nothing extra except hydrating fewer
    // rows.
    if (offset >= static_cast<int>(combined.size())) {
        return {};
    }
    combined.erase(combined.begin(), combined.begin() + offset);
    if (static_cast<int>(combined.size()) > limit) {
        combined.resize(static_cast<size_t>(limit));
    }
    return hydrate(sql, combined);
}

std::vector<QueryResult> MediaDbQuery::similarTo(std::int64_t seedFileId,
                                                 const QueryFilters& filters, int limit, int offset,
                                                 QuerySort sort) const {
    sqlite3* sql = db_.handle();
    if (limit < 0) {
        limit = 0;
    }
    if (offset < 0) {
        offset = 0;
    }

    // Pull the seed's embedding. similarTo is a no-op if the seed wasn't
    // indexed with an audio model (no embedding row).
    std::vector<float> seedVec;
    {
        sqlite3_stmt* stmt = nullptr;
        const char* q = "SELECT vector_dim, vector_blob FROM media_embedding WHERE file_id = ?";
        if (sqlite3_prepare_v2(sql, q, -1, &stmt, nullptr) != SQLITE_OK) {
            return {};
        }
        sqlite3_bind_int64(stmt, 1, seedFileId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const int dim = sqlite3_column_int(stmt, 0);
            const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 1));
            const int bytes = sqlite3_column_bytes(stmt, 1);
            if (dim > 0 && bytes == dim * static_cast<int>(sizeof(float))) {
                seedVec.resize(static_cast<size_t>(dim));
                std::memcpy(seedVec.data(), blob, static_cast<size_t>(bytes));
            }
        }
        sqlite3_finalize(stmt);
    }
    if (seedVec.empty()) {
        return {};
    }

    // Candidate set = filter-matched, capped by recency. Same bound as the
    // text search path so a huge library doesn't get scanned end to end.
    const BuiltWhere where = buildWhere(filters);
    auto candidateIds = candidateIdsByRecency(sql, where, kCandidateCap);
    if (candidateIds.empty()) {
        return {};
    }

    const auto scores = audioScoresOnCandidates(sql, seedVec, candidateIds);
    if (scores.empty()) {
        return {};
    }

    std::vector<std::pair<float, std::int64_t>> ranked;
    ranked.reserve(scores.size());
    for (const auto& [id, s] : scores) {
        if (id == seedFileId) {
            continue;  // drop the seed from its own neighbours
        }
        ranked.emplace_back(s, id);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (sort.field != QuerySortField::Default) {
        auto sorted = hydrate(sql, ranked);
        sortResults(sorted, sort);
        if (offset >= static_cast<int>(sorted.size())) {
            return {};
        }
        sorted.erase(sorted.begin(), sorted.begin() + offset);
        if (static_cast<int>(sorted.size()) > limit) {
            sorted.resize(static_cast<size_t>(limit));
        }
        return sorted;
    }

    if (offset >= static_cast<int>(ranked.size())) {
        return {};
    }
    ranked.erase(ranked.begin(), ranked.begin() + offset);
    if (static_cast<int>(ranked.size()) > limit) {
        ranked.resize(static_cast<size_t>(limit));
    }
    return hydrate(sql, ranked);
}

bool MediaDbQuery::hasEmbedding(std::int64_t fileId) const {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), "SELECT 1 FROM media_embedding WHERE file_id = ? LIMIT 1",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, fileId);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

int MediaDbQuery::totalEmbeddings() const {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), "SELECT COUNT(*) FROM media_embedding", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

int MediaDbQuery::totalFiles() const {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), "SELECT COUNT(*) FROM media_file", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return 0;
    }
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

}  // namespace magda::media
