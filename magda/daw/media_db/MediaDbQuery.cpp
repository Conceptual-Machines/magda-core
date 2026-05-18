#include "MediaDbQuery.hpp"

#include <sqlite3.h>

#include <algorithm>
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

std::unordered_map<std::int64_t, float> ftsScores(sqlite3* db, const std::string& text,
                                                  const BuiltWhere& w) {
    const std::string fts = buildFtsQuery(text);
    if (fts.empty()) {
        return {};
    }
    const std::string sql = "SELECT f.id, -bm25(media_fts) AS s "
                            "FROM media_fts "
                            "JOIN media_file AS f ON f.id = media_fts.rowid "
                            "WHERE media_fts MATCH ? AND " +
                            w.clause;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_text(stmt, 1, fts.c_str(), -1, SQLITE_TRANSIENT);
    bindAll(stmt, 2, w);

    std::unordered_map<std::int64_t, float> scores;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        scores[sqlite3_column_int64(stmt, 0)] = static_cast<float>(sqlite3_column_double(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return scores;
}

std::unordered_map<std::int64_t, float> audioScores(sqlite3* db, const std::vector<float>& queryVec,
                                                    const BuiltWhere& w) {
    const std::string sql = "SELECT f.id, e.vector_dim, e.vector_blob "
                            "FROM media_file AS f "
                            "JOIN media_embedding AS e ON e.file_id = f.id "
                            "WHERE " +
                            w.clause;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    bindAll(stmt, 1, w);

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

std::vector<QueryResult> hydrate(sqlite3* db,
                                 const std::vector<std::pair<float, std::int64_t>>& scored) {
    std::vector<QueryResult> out;
    if (scored.empty()) {
        return out;
    }
    // Bulk-fetch with one IN-clause query, then re-order by the input vector.
    std::string sql = "SELECT id, path, kind, family, shape, bpm, key_root, key_scale, duration_s "
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
        r.kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (auto t = optString(stmt, 3)) {
            r.family = *t;
        }
        if (auto t = optString(stmt, 4)) {
            r.shape = *t;
        }
        r.bpm = optDouble(stmt, 5);
        r.keyRoot = optString(stmt, 6);
        r.keyScale = optString(stmt, 7);
        r.durationS = optDouble(stmt, 8);
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

std::vector<QueryResult> filterOnly(sqlite3* db, const BuiltWhere& w, int limit) {
    const std::string sql =
        "SELECT id, path, kind, family, shape, bpm, key_root, key_scale, duration_s "
        "FROM media_file WHERE " +
        w.clause + " ORDER BY indexed_at DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    const int after = bindAll(stmt, 1, w);
    sqlite3_bind_int(stmt, after, limit);

    std::vector<QueryResult> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueryResult r;
        r.fileId = sqlite3_column_int64(stmt, 0);
        r.path = std::filesystem::path(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        r.kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (auto t = optString(stmt, 3)) {
            r.family = *t;
        }
        if (auto t = optString(stmt, 4)) {
            r.shape = *t;
        }
        r.bpm = optDouble(stmt, 5);
        r.keyRoot = optString(stmt, 6);
        r.keyScale = optString(stmt, 7);
        r.durationS = optDouble(stmt, 8);
        r.score = std::numeric_limits<float>::quiet_NaN();
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

}  // namespace

// ---- Public --------------------------------------------------------------

MediaDbQuery::MediaDbQuery(MediaDatabase& db, ClapTextEncoder* textEncoder,
                           RobertaTokenizer* tokenizer)
    : db_(db), textEncoder_(textEncoder), tokenizer_(tokenizer) {}

std::vector<QueryResult> MediaDbQuery::search(const std::optional<std::string>& text,
                                              const QueryFilters& filters, int limit,
                                              QueryWeights weights) const {
    sqlite3* sql = db_.handle();
    const BuiltWhere where = buildWhere(filters);

    if (!text || text->empty()) {
        return filterOnly(sql, where, limit);
    }

    // Audio cosine for everything embedded that matches filters. Skipped if
    // either the encoder or tokenizer isn't loaded.
    std::unordered_map<std::int64_t, float> audio;
    if (textEncoder_ && tokenizer_) {
        const auto enc = tokenizer_->encode(*text);
        if (!enc.inputIds.empty()) {
            try {
                auto qvec = textEncoder_->embedTokens(enc.inputIds, enc.attentionMask);
                if (!qvec.empty()) {
                    audio = audioScores(sql, qvec, where);
                }
            } catch (const ClapTextEncoderError&) {
                // Treat semantic side as unavailable for this query.
            }
        }
    }

    const auto bm25 = ftsScores(sql, *text, where);

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
    if (static_cast<int>(combined.size()) > limit) {
        combined.resize(static_cast<size_t>(limit));
    }
    return hydrate(sql, combined);
}

}  // namespace magda::media
