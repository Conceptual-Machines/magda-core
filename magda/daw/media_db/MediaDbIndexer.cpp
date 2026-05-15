#include "MediaDbIndexer.hpp"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "AudioFeatures.hpp"
#include "ClapAudioEncoder.hpp"
#include "MediaDatabase.hpp"
#include "PathRules.hpp"
#include "Scan.hpp"

namespace magda::media {

namespace {

// ---- Hash ----------------------------------------------------------------

// FNV-1a 64-bit over the file's first 1 MiB, returned as 8 raw little-endian
// bytes. Cheap deterministic content fingerprint; combined with (mtime, size)
// for skip-if-unchanged detection. Collisions on 64 bits are tolerable —
// false negatives mean an unnecessary re-index, not data loss. (juce::MD5
// lives in juce_cryptography which the daw lib doesn't link.)
std::vector<std::uint8_t> hashFilePrefix(const std::filesystem::path& path) {
    constexpr int kPrefixBytes = 1 << 20;
    juce::File jf(juce::String(path.string()));
    auto stream = jf.createInputStream();
    if (!stream) {
        return {};
    }
    juce::MemoryBlock block;
    block.setSize(kPrefixBytes);
    const int read = static_cast<int>(stream->read(block.getData(), kPrefixBytes));
    if (read <= 0) {
        return {};
    }
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t h = kOffset;
    const auto* bytes = static_cast<const std::uint8_t*>(block.getData());
    for (int i = 0; i < read; ++i) {
        h ^= bytes[i];
        h *= kPrime;
    }
    std::vector<std::uint8_t> out(8);
    for (int i = 0; i < 8; ++i) {
        out[static_cast<size_t>(i)] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFFU);
    }
    return out;
}

// ---- Skip-if-unchanged --------------------------------------------------

struct ExistingRow {
    std::int64_t mtimeNs = 0;
    std::int64_t sizeBytes = 0;
    std::vector<std::uint8_t> hash;
};

std::optional<ExistingRow> lookupExisting(sqlite3* db, const std::string& path) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            db, "SELECT mtime_ns, size_bytes, content_hash FROM media_file WHERE path = ?", -1,
            &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<ExistingRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ExistingRow row;
        row.mtimeNs = sqlite3_column_int64(stmt, 0);
        row.sizeBytes = sqlite3_column_int64(stmt, 1);
        if (const auto* b = sqlite3_column_blob(stmt, 2)) {
            const int n = sqlite3_column_bytes(stmt, 2);
            row.hash.assign(static_cast<const std::uint8_t*>(b),
                            static_cast<const std::uint8_t*>(b) + n);
        }
        result = row;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool unchanged(const ExistingRow& row, const ScannedFile& f,
               const std::vector<std::uint8_t>& hash) {
    return row.mtimeNs == f.mtimeNs && row.sizeBytes == f.sizeBytes && row.hash == hash;
}

// ---- Audio decode for the encoder ---------------------------------------

// Decode mono float at 48 kHz (CLAP's required SR). Re-reads the file the
// AudioFeatures already touched — we accept the double decode for now
// (a single-pass refactor is a follow-up if profiling demands it).
std::optional<std::vector<float>> loadMono48k(const std::filesystem::path& path) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::File jf(juce::String(path.string()));
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(jf));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels < 1) {
        return std::nullopt;
    }

    const int srcSr = static_cast<int>(reader->sampleRate);
    const int srcChannels = static_cast<int>(reader->numChannels);
    const int srcLen = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float> multi(srcChannels, srcLen);
    multi.clear();
    reader->read(&multi, 0, srcLen, 0, true, true);

    std::vector<float> mono(static_cast<size_t>(srcLen), 0.0F);
    const float gain = 1.0F / static_cast<float>(srcChannels);
    for (int ch = 0; ch < srcChannels; ++ch) {
        const float* src = multi.getReadPointer(ch);
        for (int i = 0; i < srcLen; ++i) {
            mono[static_cast<size_t>(i)] += src[i] * gain;
        }
    }
    if (srcSr == 48000) {
        return mono;
    }

    const double ratio = static_cast<double>(srcSr) / 48000.0;
    const int dstLen = static_cast<int>(static_cast<double>(srcLen) / ratio);
    std::vector<float> dst(static_cast<size_t>(dstLen), 0.0F);
    juce::LagrangeInterpolator interp;
    interp.process(ratio, mono.data(), dst.data(), dstLen);
    return dst;
}

// ---- Derived categoricals ------------------------------------------------
//
// Mirrors derive.py in the Python prototype. The TAG_FAMILY map there
// converts CLAP zero-shot tag scores into a family; in C++ we currently
// don't run zero-shot tagging during indexing, so family comes entirely
// from pathFamilyHint. We can layer the zero-shot side in later without
// changing the column semantics.

constexpr float kFlatnessThreshold = 0.08F;

std::string deriveShape(const AudioFeatures& f) {
    if (f.durationS <= 0.0) {
        return "unknown";
    }
    if (f.durationS < 2.0) {
        return "one-shot";
    }
    if (f.transientDensity < 0.5F) {
        return "sustained";
    }
    return "loop";
}

std::string deriveFamily(const std::filesystem::path& path) {
    if (auto hint = pathFamilyHint(path)) {
        return *hint;
    }
    return "unknown";
}

int deriveTonal(const AudioFeatures& f) {
    return f.spectralFlatness < kFlatnessThreshold ? 1 : 0;
}

// Apply the policy rules: one-shots have no BPM; drum/fx have no key.
// Filename-derived keys (already in AudioFeatures from PathRules) are
// kept; we only suppress when the family inherently shouldn't carry one.
void applyPolicies(AudioFeatures& f, const std::string& shape, const std::string& family) {
    if (shape == "one-shot") {
        f.bpm.reset();
    }
    if (family == "drum" || family == "fx") {
        f.keyRoot.reset();
        f.keyScale.reset();
        f.keyConfidence.reset();
    }
}

// ---- Insert helpers ------------------------------------------------------

void bindOptDouble(sqlite3_stmt* stmt, int idx, const std::optional<double>& v) {
    if (v) {
        sqlite3_bind_double(stmt, idx, *v);
    } else {
        sqlite3_bind_null(stmt, idx);
    }
}

void bindOptText(sqlite3_stmt* stmt, int idx, const std::optional<std::string>& v) {
    if (v) {
        sqlite3_bind_text(stmt, idx, v->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, idx);
    }
}

void bindOptFloat(sqlite3_stmt* stmt, int idx, const std::optional<float>& v) {
    if (v) {
        sqlite3_bind_double(stmt, idx, static_cast<double>(*v));
    } else {
        sqlite3_bind_null(stmt, idx);
    }
}

std::int64_t upsertFile(sqlite3* db, const ScannedFile& f, const std::vector<std::uint8_t>& hash,
                        const std::optional<AudioFeatures>& feats, const std::string& shape,
                        const std::string& family, int tonal) {
    static constexpr const char* kSql = R"SQL(
        INSERT INTO media_file (
            path, kind, format, size_bytes, mtime_ns, content_hash, indexed_at,
            duration_s, sample_rate, channels, bpm, key_root, key_scale,
            key_confidence, rms, spectral_centroid, spectral_flatness,
            transient_density, shape, family, tonal
        ) VALUES (
            :path, :kind, :format, :size, :mtime, :hash, :indexed,
            :duration, :sr, :channels, :bpm, :key_root, :key_scale,
            :key_conf, :rms, :centroid, :flatness, :transient,
            :shape, :family, :tonal
        )
        ON CONFLICT(path) DO UPDATE SET
            mtime_ns = excluded.mtime_ns,
            size_bytes = excluded.size_bytes,
            content_hash = excluded.content_hash,
            indexed_at = excluded.indexed_at,
            duration_s = excluded.duration_s,
            sample_rate = excluded.sample_rate,
            channels = excluded.channels,
            bpm = excluded.bpm,
            key_root = excluded.key_root,
            key_scale = excluded.key_scale,
            key_confidence = excluded.key_confidence,
            rms = excluded.rms,
            spectral_centroid = excluded.spectral_centroid,
            spectral_flatness = excluded.spectral_flatness,
            transient_density = excluded.transient_density,
            shape = excluded.shape,
            family = excluded.family,
            tonal = excluded.tonal
        RETURNING id
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    const std::string pathStr = f.path.string();
    const auto now = static_cast<std::int64_t>(std::time(nullptr));

    auto p = [&](const char* name) { return sqlite3_bind_parameter_index(stmt, name); };

    sqlite3_bind_text(stmt, p(":path"), pathStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, p(":kind"), f.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, p(":format"), f.format.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, p(":size"), f.sizeBytes);
    sqlite3_bind_int64(stmt, p(":mtime"), f.mtimeNs);
    if (!hash.empty()) {
        sqlite3_bind_blob(stmt, p(":hash"), hash.data(), static_cast<int>(hash.size()),
                          SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, p(":hash"));
    }
    sqlite3_bind_int64(stmt, p(":indexed"), now);

    if (feats) {
        sqlite3_bind_double(stmt, p(":duration"), feats->durationS);
        sqlite3_bind_int(stmt, p(":sr"), feats->sampleRate);
        sqlite3_bind_int(stmt, p(":channels"), feats->channels);
        bindOptDouble(stmt, p(":bpm"), feats->bpm);
        bindOptText(stmt, p(":key_root"), feats->keyRoot);
        bindOptText(stmt, p(":key_scale"), feats->keyScale);
        bindOptFloat(stmt, p(":key_conf"), feats->keyConfidence);
        sqlite3_bind_double(stmt, p(":rms"), static_cast<double>(feats->rms));
        sqlite3_bind_double(stmt, p(":centroid"), static_cast<double>(feats->spectralCentroid));
        sqlite3_bind_double(stmt, p(":flatness"), static_cast<double>(feats->spectralFlatness));
        sqlite3_bind_double(stmt, p(":transient"), static_cast<double>(feats->transientDensity));
    } else {
        for (const char* k : {":duration", ":sr", ":channels", ":bpm", ":key_root", ":key_scale",
                              ":key_conf", ":rms", ":centroid", ":flatness", ":transient"}) {
            sqlite3_bind_null(stmt, p(k));
        }
    }

    sqlite3_bind_text(stmt, p(":shape"), shape.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, p(":family"), family.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, p(":tonal"), tonal);

    std::int64_t id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

void replaceTags(sqlite3* db, std::int64_t fileId,
                 const std::vector<std::pair<std::string, float>>& tags,
                 const std::string& source) {
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM media_tag WHERE file_id = ? AND source_model = ?", -1, &del,
                       nullptr);
    sqlite3_bind_int64(del, 1, fileId);
    sqlite3_bind_text(del, 2, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(del);
    sqlite3_finalize(del);
    if (tags.empty()) {
        return;
    }

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db,
                       "INSERT INTO media_tag (file_id, tag, confidence, source_model) "
                       "VALUES (?, ?, ?, ?)",
                       -1, &ins, nullptr);
    for (const auto& [tag, conf] : tags) {
        sqlite3_bind_int64(ins, 1, fileId);
        sqlite3_bind_text(ins, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(ins, 3, static_cast<double>(conf));
        sqlite3_bind_text(ins, 4, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
}

void upsertEmbedding(sqlite3* db, std::int64_t fileId, const std::string& modelId,
                     const std::vector<float>& vec) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
                       "INSERT OR REPLACE INTO media_embedding "
                       "(file_id, model_id, model_version, vector_dim, vector_blob) "
                       "VALUES (?, ?, ?, ?, ?)",
                       -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_text(stmt, 2, modelId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, "1", -1, SQLITE_STATIC);  // model_version placeholder
    sqlite3_bind_int(stmt, 4, static_cast<int>(vec.size()));
    sqlite3_bind_blob(stmt, 5, vec.data(), static_cast<int>(vec.size() * sizeof(float)),
                      SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void upsertFts(sqlite3* db, std::int64_t fileId, const std::string& pathText,
               const std::string& tagText) {
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM media_fts WHERE rowid = ?", -1, &del, nullptr);
    sqlite3_bind_int64(del, 1, fileId);
    sqlite3_step(del);
    sqlite3_finalize(del);

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO media_fts (rowid, path_text, tag_text) VALUES (?, ?, ?)",
                       -1, &ins, nullptr);
    sqlite3_bind_int64(ins, 1, fileId);
    sqlite3_bind_text(ins, 2, pathText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, tagText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(ins);
    sqlite3_finalize(ins);
}

std::string buildPathText(const std::filesystem::path& path) {
    // Tokenise the path on common separators, lowercase, dedup. The FTS5
    // tokenizer further chops on punctuation, but we feed it a clean string
    // so reviewing tag_text in the DB isn't unreadable.
    std::string raw = path.string();
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        const bool isSep = c == '_' || c == '/' || c == '\\' || c == '-' || c == '.' || c == ',' ||
                           c == '(' || c == ')';
        out += isSep ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string buildTagText(const std::vector<std::pair<std::string, float>>& tags) {
    std::string out;
    for (const auto& [t, _] : tags) {
        if (!out.empty()) {
            out += ' ';
        }
        out += t;
    }
    return out;
}

}  // namespace

// ---- Public --------------------------------------------------------------

MediaDbIndexer::MediaDbIndexer(MediaDatabase& db, ClapAudioEncoder* encoder)
    : db_(db), encoder_(encoder) {}

void MediaDbIndexer::setProgress(ProgressFn fn) {
    progress_ = std::move(fn);
}

MediaDbIndexer::Stats MediaDbIndexer::indexDirectory(const std::filesystem::path& root) {
    Stats stats;
    sqlite3* sqlDb = db_.handle();

    // Pre-scan for progress total. (One extra walk; cheap vs the indexing
    // pass that follows.)
    int total = 0;
    walk(root, [&](const ScannedFile&) { ++total; });
    int done = 0;

    MediaDatabase::Transaction txn(db_);

    walk(root, [&](const ScannedFile& f) {
        try {
            const auto hash = hashFilePrefix(f.path);
            const auto existing = lookupExisting(sqlDb, f.path.string());
            if (existing && unchanged(*existing, f, hash)) {
                ++stats.skipped;
            } else {
                std::optional<AudioFeatures> feats;
                if (f.kind == "audio") {
                    feats = extractFeatures(f.path);
                }

                const std::string family = deriveFamily(f.path);
                const std::string shape = feats ? deriveShape(*feats) : std::string{"unknown"};
                const int tonal = feats ? deriveTonal(*feats) : 0;
                if (feats) {
                    applyPolicies(*feats, shape, family);
                }

                const std::int64_t fileId = upsertFile(sqlDb, f, hash, feats, shape, family, tonal);
                if (fileId < 0) {
                    ++stats.failed;
                    return;
                }

                const auto tags = pathTags(f.path);
                replaceTags(sqlDb, fileId, tags, "path");

                if (encoder_ && f.kind == "audio") {
                    if (auto mono = loadMono48k(f.path)) {
                        try {
                            auto emb =
                                encoder_->embed(mono->data(), static_cast<int>(mono->size()));
                            if (!emb.empty()) {
                                upsertEmbedding(sqlDb, fileId, encoder_->modelId(), emb);
                            }
                        } catch (const ClapEncoderError&) {
                            // Embedding failure is non-fatal: keep the row,
                            // search will fall back to FTS-only for this file.
                        }
                    }
                }

                upsertFts(sqlDb, fileId, buildPathText(f.path), buildTagText(tags));

                if (existing) {
                    ++stats.updated;
                } else {
                    ++stats.inserted;
                }
            }
        } catch (...) {
            ++stats.failed;
        }
        ++done;
        if (progress_) {
            progress_(done, total, f.path);
        }
    });

    txn.commit();
    return stats;
}

}  // namespace magda::media
