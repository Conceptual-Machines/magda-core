#include "MediaDbIndexer.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "AudioFeatures.hpp"
#include "ClapAudioEncoder.hpp"
#include "ClapTextEncoder.hpp"
#include "MediaDatabase.hpp"
#include "MediaDbZeroShotTags.hpp"
#include "PathRules.hpp"
#include "RobertaTokenizer.hpp"
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

[[nodiscard]] std::string sqliteLastError(sqlite3* db) {
    const char* msg = db ? sqlite3_errmsg(db) : "(no connection)";
    return msg ? msg : "(unknown sqlite error)";
}

void execSqlOrThrow(sqlite3* db, const char* sql, const char* context) {
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string err = errMsg ? errMsg : sqliteLastError(db);
        sqlite3_free(errMsg);
        throw MediaDatabaseError(std::string(context) + ": " + err);
    }
}

void rollbackQuietly(sqlite3* db) {
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
}

void reportFailure(const MediaDbIndexer::FailureFn& failure, const std::filesystem::path& path,
                   const std::string& reason) {
    if (failure) {
        failure(path, reason);
    }
}

bool shouldCancel(const MediaDbIndexer::ShouldCancelFn& shouldCancelFn) {
    return shouldCancelFn && shouldCancelFn();
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

std::string deriveMidiShape(double durationS) {
    if (durationS <= 0.0) {
        return "unknown";
    }
    return durationS < 2.0 ? "one-shot" : "loop";
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

std::vector<std::string> tagTokensFromText(const std::string& raw) {
    std::vector<std::string> out;
    std::string token;
    auto flush = [&]() {
        if (!token.empty() && std::find(out.begin(), out.end(), token) == out.end()) {
            out.push_back(token);
        }
        token.clear();
    };

    for (unsigned char c : raw) {
        if (std::isalnum(c)) {
            token += static_cast<char>(std::tolower(c));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

void addTag(std::vector<std::pair<std::string, float>>& tags, const std::string& tag) {
    if (tag.empty()) {
        return;
    }
    const auto exists =
        std::any_of(tags.begin(), tags.end(), [&](const auto& t) { return t.first == tag; });
    if (!exists) {
        tags.emplace_back(tag, 1.0F);
    }
}

std::vector<std::pair<std::string, float>> scanTagsFor(
    const std::filesystem::path& path, const MediaDbIndexer::ScanTagOptions& options) {
    std::vector<std::pair<std::string, float>> tags;
    for (const auto& tag : options.customTags) {
        for (const auto& token : tagTokensFromText(tag)) {
            addTag(tags, token);
        }
    }

    if (options.includeRootFolderName && !options.root.empty()) {
        for (const auto& token : tagTokensFromText(options.root.filename().string())) {
            addTag(tags, token);
        }
    }

    if (options.includePathNodes) {
        std::filesystem::path relativeParent = path.parent_path();
        if (!options.root.empty()) {
            std::error_code ec;
            auto rel = std::filesystem::relative(path.parent_path(), options.root, ec);
            if (!ec && !rel.empty() && rel.string() != ".") {
                relativeParent = rel;
            } else {
                relativeParent.clear();
            }
        }

        for (const auto& part : relativeParent) {
            const auto partText = part.string();
            if (partText.empty() || partText == "." || partText == "..") {
                continue;
            }
            for (const auto& token : tagTokensFromText(partText)) {
                addTag(tags, token);
            }
        }
    }
    return tags;
}

struct MidiClipFeatures {
    double durationS = 0.0;
    std::optional<double> bpm;
    bool hasNotes = false;
};

std::optional<MidiClipFeatures> extractMidiClipFeatures(const std::filesystem::path& path) {
    juce::File file(juce::String(path.string()));
    auto stream = file.createInputStream();
    if (!stream) {
        return std::nullopt;
    }

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(*stream)) {
        return std::nullopt;
    }

    MidiClipFeatures result;

    juce::MidiMessageSequence tempoEvents;
    midiFile.findAllTempoEvents(tempoEvents);
    if (tempoEvents.getNumEvents() > 0) {
        const auto& msg = tempoEvents.getEventPointer(0)->message;
        const double secondsPerQuarter = msg.getTempoSecondsPerQuarterNote();
        if (secondsPerQuarter > 0.0) {
            result.bpm = 60.0 / secondsPerQuarter;
        }
    }
    if (!result.bpm) {
        result.bpm = parseBpmFromPath(path).value_or(120.0);
    }

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex) {
        const auto* track = midiFile.getTrack(trackIndex);
        if (track == nullptr) {
            continue;
        }
        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex) {
            const auto& msg = track->getEventPointer(eventIndex)->message;
            if (msg.isNoteOn()) {
                result.hasNotes = true;
                break;
            }
        }
        if (result.hasNotes) {
            break;
        }
    }

    midiFile.convertTimestampTicksToSeconds();
    result.durationS = midiFile.getLastTimestamp();
    return result;
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

void deleteEmbeddings(sqlite3* db, std::int64_t fileId) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM media_embedding WHERE file_id = ?", -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, fileId);
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

// ---- Zero-shot helpers (issue #1319) ------------------------------------

// Read every tag row for a file across all source_models. Used when the
// zero-shot pass needs to refresh the FTS row with the union of path +
// scan + clap-zeroshot tags after writing new clap-zeroshot rows.
std::vector<std::pair<std::string, float>> readAllTags(sqlite3* db, std::int64_t fileId) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT tag, confidence FROM media_tag WHERE file_id = ?", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_int64(stmt, 1, fileId);
    std::vector<std::pair<std::string, float>> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text == nullptr) {
            continue;
        }
        out.emplace_back(text, static_cast<float>(sqlite3_column_double(stmt, 1)));
    }
    sqlite3_finalize(stmt);
    return out;
}

// Read the path + currently-stored family for a file. Family is what we
// wrote during the scan pass — usually a path hint or "unknown".
struct ExistingFileRow {
    std::string path;
    std::string family;
};
std::optional<ExistingFileRow> readFileRow(sqlite3* db, std::int64_t fileId) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT path, family FROM media_file WHERE id = ?", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, fileId);
    std::optional<ExistingFileRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ExistingFileRow row;
        if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) {
            row.path = p;
        }
        if (const auto* fam = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) {
            row.family = fam;
        }
        result = std::move(row);
    }
    sqlite3_finalize(stmt);
    return result;
}

void updateFamily(sqlite3* db, std::int64_t fileId, const std::string& family) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE media_file SET family = ? WHERE id = ?", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, family.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, fileId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// After audio embedding: score against the prompt matrix, write the top
// hits as media_tag rows under the 'clap-zeroshot' source, override family
// from the top non-texture tag when the scan pass left it "unknown", and
// refresh the FTS row so tag-text search picks the new tags up. All inside
// the same transaction the caller opens around upsertEmbedding so a power
// loss between writes can't leave the DB in a state where the embedding
// exists but tags/family/FTS are stale.
void applyZeroShotTagging(sqlite3* db, std::int64_t fileId,
                          const std::vector<float>& audioEmbedding, const ZeroShotTagger& tagger) {
    const auto hits = tagger.scoreEmbedding(audioEmbedding.data(), audioEmbedding.size());
    replaceTags(db, fileId, hits, "clap-zeroshot");

    const auto fileRow = readFileRow(db, fileId);
    if (fileRow) {
        // Only overwrite when the path-derived family was non-informative.
        // The path hint is treated as more reliable than CLAP on short or
        // ambiguous samples (a 0.5s vocal hit sounds drum-like to CLAP, but
        // a producer who put it in /Vocals/ knew what they made).
        if (fileRow->family.empty() || fileRow->family == "unknown") {
            const auto clapFamily = familyFromTopLabels(hits);
            if (!clapFamily.empty()) {
                updateFamily(db, fileId, clapFamily);
            }
        }
    }

    // Refresh FTS so tag_text reflects path + scan + clap-zeroshot tags
    // together. readAllTags is cheap (one indexed SELECT) compared to the
    // ORT inference we just ran.
    const auto unioned = readAllTags(db, fileId);
    const auto pathText = fileRow ? buildPathText(fileRow->path) : std::string{};
    upsertFts(db, fileId, pathText, buildTagText(unioned));
}

// ---- Per-file pipeline --------------------------------------------------
//
// Same logic in both serial and parallel modes: takes a raw sqlite3* (so
// workers can pass their own connection) plus an encoder pointer (shared —
// mutates the supplied Stats. Semantic embeddings are intentionally handled
// by embedMissingAudio() after the scan, so slow CLAP work cannot make file
// discovery/indexing appear stuck.

void processOneFile(sqlite3* sqlDb, const ScannedFile& f, MediaDbIndexer::Stats& stats,
                    MediaDbIndexer::Mode mode, bool wrapWritesInTransaction,
                    const MediaDbIndexer::FailureFn& failure,
                    const MediaDbIndexer::ShouldCancelFn& shouldCancelFn,
                    const MediaDbIndexer::ScanTagOptions& scanTagOptions) {
    bool transactionOpen = false;
    try {
        if (shouldCancel(shouldCancelFn)) {
            return;
        }
        // OnlyNew skips before the prefix hash so existing files become
        // a single cheap SELECT — useful when the user just wants to
        // pick up additions on a large library without re-hashing.
        if (mode == MediaDbIndexer::Mode::OnlyNew) {
            if (lookupExisting(sqlDb, f.path.string())) {
                ++stats.skipped;
                return;
            }
        }
        const auto hash = hashFilePrefix(f.path);
        const auto existing = lookupExisting(sqlDb, f.path.string());
        // Incremental skip: caller wants to re-derive when mtime/size/hash
        // changed but leave unchanged rows alone. ForceAll bypasses this
        // entirely.
        if (mode == MediaDbIndexer::Mode::Incremental && existing &&
            unchanged(*existing, f, hash)) {
            ++stats.skipped;
            return;
        }

        std::optional<AudioFeatures> feats;
        std::optional<MidiClipFeatures> midiFeats;
        if (f.kind == "audio") {
            feats = extractFeatures(f.path);
        } else if (f.kind == "clip") {
            midiFeats = extractMidiClipFeatures(f.path);
            if (midiFeats) {
                AudioFeatures indexed;
                indexed.durationS = midiFeats->durationS;
                indexed.bpm = midiFeats->bpm;
                feats = indexed;
            }
        }

        const std::string family = deriveFamily(f.path);
        const std::string shape = midiFeats
                                      ? deriveMidiShape(midiFeats->durationS)
                                      : (feats ? deriveShape(*feats) : std::string{"unknown"});
        const int tonal =
            midiFeats ? (midiFeats->hasNotes ? 1 : 0) : (feats ? deriveTonal(*feats) : 0);
        if (feats && f.kind == "audio") {
            applyPolicies(*feats, shape, family);
        }
        if (shouldCancel(shouldCancelFn)) {
            return;
        }

        auto tags = pathTags(f.path);
        for (const auto& tag : scanTagsFor(f.path, scanTagOptions)) {
            addTag(tags, tag.first);
        }

        // Expensive reads/analysis happen before the write transaction. In
        // parallel scans this keeps SQLite writer locks short; holding a
        // transaction across decode work can make peer workers hit busy
        // timeouts and silently lose entire batches.
        if (wrapWritesInTransaction) {
            execSqlOrThrow(sqlDb, "BEGIN IMMEDIATE", "BEGIN media index file");
            transactionOpen = true;
        }

        const std::int64_t fileId = upsertFile(sqlDb, f, hash, feats, shape, family, tonal);
        if (fileId < 0) {
            throw MediaDatabaseError("upsert media_file failed: " + sqliteLastError(sqlDb));
        }

        replaceTags(sqlDb, fileId, tags, "path");
        if (existing && f.kind == "audio") {
            deleteEmbeddings(sqlDb, fileId);
            // Stale CLAP zero-shot tags described the previous audio
            // content; clear them in the same transaction so the file can't
            // be left with semantic tags that no longer describe the bytes
            // on disk. The next embedding pass re-derives them when a
            // tagger is available; when it isn't, the file simply has no
            // CLAP tags rather than misleading ones (issue #1319).
            replaceTags(sqlDb, fileId, {}, "clap-zeroshot");
        }

        upsertFts(sqlDb, fileId, buildPathText(f.path), buildTagText(tags));

        if (wrapWritesInTransaction) {
            execSqlOrThrow(sqlDb, "COMMIT", "COMMIT media index file");
            transactionOpen = false;
        }

        if (existing) {
            ++stats.updated;
        } else {
            ++stats.inserted;
        }
    } catch (const std::exception& e) {
        if (transactionOpen) {
            rollbackQuietly(sqlDb);
        }
        reportFailure(failure, f.path, e.what());
        ++stats.failed;
    } catch (...) {
        if (transactionOpen) {
            rollbackQuietly(sqlDb);
        }
        reportFailure(failure, f.path, "unknown error");
        ++stats.failed;
    }
}

// ---- Thread-count heuristics --------------------------------------------

constexpr int kParallelThreshold = 64;  // below this, serial wins on setup cost
constexpr size_t kBatchSize = 50;       // files per transaction per worker

int decideThreadCount(int requested, size_t fileCount, const std::filesystem::path& dbPath) {
    // In-memory DBs can't be opened from a second thread against the same
    // store, so workers can't see each other's writes. Force serial.
    if (dbPath == ":memory:" || dbPath.string() == ":memory:") {
        return 1;
    }
    if (fileCount < kParallelThreshold) {
        return 1;
    }
    int n = requested;
    if (n <= 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        n = hw == 0 ? 2 : static_cast<int>(hw) - 1;
        if (n < 1) {
            n = 1;
        }
    }
    // Never spawn more workers than there is work for, accounting for the
    // batch size (one worker per batch slot, capped).
    const int maxUseful = static_cast<int>((fileCount + kBatchSize - 1) / kBatchSize);
    if (n > maxUseful) {
        n = maxUseful;
    }
    return std::max(n, 1);
}

struct PendingEmbedding {
    std::int64_t fileId = -1;
    std::filesystem::path path;
};

std::string placeholders(size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        out += (i == 0 ? "?" : ",?");
    }
    return out;
}

std::vector<PendingEmbedding> pendingEmbeddings(sqlite3* db, const std::string& modelId,
                                                const std::filesystem::path& root) {
    std::string sql = "SELECT f.id, f.path "
                      "FROM media_file AS f "
                      "WHERE f.kind = 'audio' "
                      "AND NOT EXISTS ("
                      "  SELECT 1 FROM media_embedding AS e "
                      "  WHERE e.file_id = f.id AND e.model_id = ? AND e.model_version = '1'"
                      ")";
    const bool filterRoot = !root.empty();
    if (filterRoot) {
        sql += " AND f.path LIKE ?";
    }
    sql += " ORDER BY f.indexed_at DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw MediaDatabaseError("prepare pending embeddings: " + sqliteLastError(db));
    }
    sqlite3_bind_text(stmt, 1, modelId.c_str(), -1, SQLITE_TRANSIENT);
    std::string rootLike;
    if (filterRoot) {
        rootLike = root.string();
        // Stored paths use the platform's native separator (backslash on
        // Windows, forward slash elsewhere), and SQL LIKE compares
        // characters literally. Appending '/' unconditionally produced a
        // pattern that never matched on Windows, leaving freshly indexed
        // files stuck without embeddings.
        if (!rootLike.empty() && rootLike.back() != '/' && rootLike.back() != '\\') {
            rootLike += static_cast<char>(std::filesystem::path::preferred_separator);
        }
        rootLike += '%';
        sqlite3_bind_text(stmt, 2, rootLike.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<PendingEmbedding> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!text) {
            continue;
        }
        out.push_back(PendingEmbedding{sqlite3_column_int64(stmt, 0), std::filesystem::path(text)});
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<PendingEmbedding> audioRowsForIds(sqlite3* db,
                                              const std::vector<std::int64_t>& fileIds) {
    if (fileIds.empty()) {
        return {};
    }
    const std::string sql = "SELECT id, path FROM media_file WHERE kind = 'audio' AND id IN (" +
                            placeholders(fileIds.size()) + ") ORDER BY indexed_at DESC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw MediaDatabaseError("prepare selected audio rows: " + sqliteLastError(db));
    }
    for (size_t i = 0; i < fileIds.size(); ++i) {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), fileIds[i]);
    }

    std::vector<PendingEmbedding> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (text) {
            out.push_back(
                PendingEmbedding{sqlite3_column_int64(stmt, 0), std::filesystem::path(text)});
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<std::filesystem::path> pathsForIds(sqlite3* db,
                                               const std::vector<std::int64_t>& fileIds) {
    if (fileIds.empty()) {
        return {};
    }
    const std::string sql =
        "SELECT path FROM media_file WHERE id IN (" + placeholders(fileIds.size()) + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw MediaDatabaseError("prepare selected file rows: " + sqliteLastError(db));
    }
    for (size_t i = 0; i < fileIds.size(); ++i) {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), fileIds[i]);
    }

    std::vector<std::filesystem::path> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) {
            out.emplace_back(text);
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

}  // namespace

// ---- Public --------------------------------------------------------------

MediaDbIndexer::MediaDbIndexer(MediaDatabase& db, ClapAudioEncoder* encoder,
                               TextEncoderProvider textEncoderProvider,
                               TokenizerProvider tokenizerProvider)
    : db_(db),
      encoder_(encoder),
      textEncoderProvider_(std::move(textEncoderProvider)),
      tokenizerProvider_(std::move(tokenizerProvider)) {}

MediaDbIndexer::~MediaDbIndexer() = default;

namespace {
// Lazy-init helper for the indexer's ZeroShotTagger. Invokes the provider
// lambdas (which may trigger the ~480 MB text-model load) only on the
// first call, and only when the embedding pass actually has work — see
// the embedMissing*/embedAudio* call sites which skip this when files is
// empty. If anything throws we leave the tagger null and the embedding
// loop degrades to "audio embedding only", reporting the failure once via
// the failure callback so the per-file path stays quiet.
ZeroShotTagger* ensureZeroShotTagger(std::unique_ptr<ZeroShotTagger>& slot,
                                     const MediaDbIndexer::TextEncoderProvider& textProvider,
                                     const MediaDbIndexer::TokenizerProvider& tokenizerProvider,
                                     const MediaDbIndexer::FailureFn& failure) {
    if (slot) {
        return slot.get();
    }
    if (!textProvider || !tokenizerProvider) {
        return nullptr;
    }
    auto* textEncoder = textProvider();
    auto* tokenizer = tokenizerProvider();
    if (textEncoder == nullptr || tokenizer == nullptr) {
        return nullptr;
    }
    try {
        slot = std::make_unique<ZeroShotTagger>(*textEncoder, *tokenizer);
    } catch (const std::exception& e) {
        reportFailure(failure, {}, std::string{"zero-shot tag init failed: "} + e.what());
        slot.reset();
    } catch (...) {
        reportFailure(failure, {}, "zero-shot tag init failed: unknown error");
        slot.reset();
    }
    return slot.get();
}
}  // namespace

void MediaDbIndexer::setProgress(ProgressFn fn) {
    progress_ = std::move(fn);
}

void MediaDbIndexer::setFailureCallback(FailureFn fn) {
    failure_ = std::move(fn);
}

void MediaDbIndexer::setShouldCancel(ShouldCancelFn fn) {
    shouldCancel_ = std::move(fn);
}

void MediaDbIndexer::setScanTagOptions(ScanTagOptions options) {
    scanTagOptions_ = std::move(options);
}

MediaDbIndexer::Stats MediaDbIndexer::indexDirectory(const std::filesystem::path& root,
                                                     int numThreads, Mode mode) {
    // Pre-scan into a vector so we have a stable list to slice for workers
    // and a total count for progress. Cheap relative to the indexing pass.
    std::vector<ScannedFile> files;
    walk(root, [&](const ScannedFile& f) { files.push_back(f); });
    const int total = static_cast<int>(files.size());

    const int workers = decideThreadCount(numThreads, files.size(), db_.path());

    // ---- Serial path ----------------------------------------------------
    if (workers <= 1) {
        Stats stats;
        sqlite3* sqlDb = db_.handle();
        int done = 0;
        for (const auto& f : files) {
            if (shouldCancel(shouldCancel_)) {
                break;
            }
            processOneFile(sqlDb, f, stats, mode, true, failure_, shouldCancel_, scanTagOptions_);
            ++done;
            if (progress_) {
                progress_(done, total, f.path);
            }
        }
        return stats;
    }

    // ---- Parallel path --------------------------------------------------
    //
    // Each worker owns its own MediaDatabase connection against the same
    // file. SQLite WAL serialises writers behind a single lock but lets
    // readers proceed; per-file write transactions keep that lock away from
    // expensive decode + embedding work. The atomic index is a fetch_add
    // work-queue — load-balanced even when files have very different costs.

    std::atomic<size_t> nextIdx{0};
    std::atomic<int> doneCounter{0};
    std::mutex progressMutex;
    std::mutex statsMutex;
    std::mutex failureMutex;

    Stats aggregate;
    const std::filesystem::path dbPath = db_.path();
    ProgressFn& progressRef = progress_;
    FailureFn failureCallback = failure_;
    ShouldCancelFn cancelCallback = shouldCancel_;
    const auto scanTagOptions = scanTagOptions_;
    FailureFn failureRef = [failureCallback, &failureMutex](const std::filesystem::path& current,
                                                            const std::string& reason) {
        std::lock_guard<std::mutex> lock(failureMutex);
        reportFailure(failureCallback, current, reason);
    };

    auto runWorker = [&]() {
        try {
            MediaDatabase localDb(dbPath);
            sqlite3* sqlDb = localDb.handle();
            // Per-connection PRAGMAs for write throughput under contention.
            // journal_mode is per-database (not per-connection) and persists,
            // but setting it here is harmless if already WAL.
            sqlite3_exec(sqlDb, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
            sqlite3_exec(sqlDb, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
            sqlite3_exec(sqlDb, "PRAGMA busy_timeout=10000", nullptr, nullptr, nullptr);

            Stats local;
            while (true) {
                if (shouldCancel(cancelCallback)) {
                    break;
                }
                const size_t batchStart = nextIdx.fetch_add(kBatchSize);
                if (batchStart >= files.size()) {
                    break;
                }
                const size_t batchEnd = std::min(batchStart + kBatchSize, files.size());

                for (size_t i = batchStart; i < batchEnd; ++i) {
                    if (shouldCancel(cancelCallback)) {
                        break;
                    }
                    processOneFile(sqlDb, files[i], local, mode, true, failureRef, cancelCallback,
                                   scanTagOptions);
                    const int nowDone = ++doneCounter;
                    if (progressRef) {
                        std::lock_guard<std::mutex> lock(progressMutex);
                        progressRef(nowDone, total, files[i].path);
                    }
                }
            }

            std::lock_guard<std::mutex> lock(statsMutex);
            aggregate.inserted += local.inserted;
            aggregate.updated += local.updated;
            aggregate.skipped += local.skipped;
            aggregate.failed += local.failed;
        } catch (const MediaDatabaseError& e) {
            // This worker couldn't even open a connection. Other workers
            // will still drain the queue; report the lost worker for logs.
            reportFailure(failureRef, {}, e.what());
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        threads.emplace_back(runWorker);
    }
    for (auto& t : threads) {
        t.join();
    }

    return aggregate;
}

MediaDbIndexer::Stats MediaDbIndexer::indexFile(const std::filesystem::path& path, Mode mode) {
    Stats stats;
    if (shouldCancel(shouldCancel_)) {
        return stats;
    }
    if (auto scanned = classify(path)) {
        processOneFile(db_.handle(), *scanned, stats, mode, true, failure_, shouldCancel_,
                       scanTagOptions_);
    } else {
        reportFailure(failure_, path, "file no longer exists or has unsupported format");
        ++stats.failed;
    }
    if (progress_) {
        progress_(1, 1, path);
    }
    return stats;
}

MediaDbIndexer::Stats MediaDbIndexer::indexFileIds(const std::vector<std::int64_t>& fileIds,
                                                   Mode mode) {
    Stats stats;
    sqlite3* sqlDb = db_.handle();
    const auto paths = pathsForIds(sqlDb, fileIds);
    const int total = static_cast<int>(paths.size());

    int done = 0;
    for (const auto& path : paths) {
        if (shouldCancel(shouldCancel_)) {
            break;
        }
        if (auto scanned = classify(path)) {
            processOneFile(sqlDb, *scanned, stats, mode, true, failure_, shouldCancel_,
                           scanTagOptions_);
        } else {
            reportFailure(failure_, path, "file no longer exists or has unsupported format");
            ++stats.failed;
        }
        ++done;
        if (progress_) {
            progress_(done, total, path);
        }
    }
    return stats;
}

MediaDbIndexer::EmbeddingStats MediaDbIndexer::embedMissingAudio(
    const std::filesystem::path& root) {
    EmbeddingStats stats;
    if (!encoder_) {
        return stats;
    }

    sqlite3* sqlDb = db_.handle();
    const std::string modelId = encoder_->modelId();
    const auto files = pendingEmbeddings(sqlDb, modelId, root);
    const int total = static_cast<int>(files.size());

    // Zero-shot tagger needs the text encoder + tokenizer (the latter
    // triggering a ~480 MB ORT session load). Only ask the providers for
    // them when we actually have files to process — otherwise installing
    // the Sample Tagger bundle makes empty / no-op embedding passes pay
    // the load cost for nothing.
    ZeroShotTagger* tagger = nullptr;
    if (!files.empty()) {
        tagger = ensureZeroShotTagger(zeroShotTagger_, textEncoderProvider_, tokenizerProvider_,
                                      failure_);
    }

    int done = 0;
    for (const auto& f : files) {
        if (shouldCancel(shouldCancel_)) {
            break;
        }
        try {
            if (shouldCancel(shouldCancel_)) {
                break;
            }
            auto mono = loadMono48k(f.path);
            if (!mono) {
                throw MediaDatabaseError("audio decode failed for embedding");
            }
            if (shouldCancel(shouldCancel_)) {
                break;
            }

            auto embedding = encoder_->embed(mono->data(), static_cast<int>(mono->size()));
            if (embedding.empty()) {
                throw MediaDatabaseError("encoder returned an empty embedding");
            }
            if (shouldCancel(shouldCancel_)) {
                break;
            }

            execSqlOrThrow(sqlDb, "BEGIN IMMEDIATE", "BEGIN media embedding");
            bool transactionOpen = true;
            try {
                upsertEmbedding(sqlDb, f.fileId, modelId, embedding);
                if (tagger != nullptr) {
                    applyZeroShotTagging(sqlDb, f.fileId, embedding, *tagger);
                }
                execSqlOrThrow(sqlDb, "COMMIT", "COMMIT media embedding");
                transactionOpen = false;
            } catch (...) {
                if (transactionOpen) {
                    rollbackQuietly(sqlDb);
                }
                throw;
            }
            ++stats.embedded;
        } catch (const std::exception& e) {
            reportFailure(failure_, f.path, e.what());
            ++stats.failed;
        } catch (...) {
            reportFailure(failure_, f.path, "unknown embedding error");
            ++stats.failed;
        }

        ++done;
        if (progress_) {
            progress_(done, total, f.path);
        }
    }
    return stats;
}

MediaDbIndexer::EmbeddingStats MediaDbIndexer::embedAudioFileIds(
    const std::vector<std::int64_t>& fileIds) {
    EmbeddingStats stats;
    if (!encoder_) {
        return stats;
    }

    sqlite3* sqlDb = db_.handle();
    const std::string modelId = encoder_->modelId();
    const auto files = audioRowsForIds(sqlDb, fileIds);
    const int total = static_cast<int>(files.size());

    ZeroShotTagger* tagger = nullptr;
    if (!files.empty()) {
        tagger = ensureZeroShotTagger(zeroShotTagger_, textEncoderProvider_, tokenizerProvider_,
                                      failure_);
    }

    int done = 0;
    for (const auto& f : files) {
        if (shouldCancel(shouldCancel_)) {
            break;
        }
        try {
            auto mono = loadMono48k(f.path);
            if (!mono) {
                throw MediaDatabaseError("audio decode failed for embedding");
            }
            if (shouldCancel(shouldCancel_)) {
                break;
            }

            auto embedding = encoder_->embed(mono->data(), static_cast<int>(mono->size()));
            if (embedding.empty()) {
                throw MediaDatabaseError("encoder returned an empty embedding");
            }
            if (shouldCancel(shouldCancel_)) {
                break;
            }

            execSqlOrThrow(sqlDb, "BEGIN IMMEDIATE", "BEGIN selected media embedding");
            bool transactionOpen = true;
            try {
                upsertEmbedding(sqlDb, f.fileId, modelId, embedding);
                if (tagger != nullptr) {
                    applyZeroShotTagging(sqlDb, f.fileId, embedding, *tagger);
                }
                execSqlOrThrow(sqlDb, "COMMIT", "COMMIT selected media embedding");
                transactionOpen = false;
            } catch (...) {
                if (transactionOpen) {
                    rollbackQuietly(sqlDb);
                }
                throw;
            }
            ++stats.embedded;
        } catch (const std::exception& e) {
            reportFailure(failure_, f.path, e.what());
            ++stats.failed;
        } catch (...) {
            reportFailure(failure_, f.path, "unknown embedding error");
            ++stats.failed;
        }

        ++done;
        if (progress_) {
            progress_(done, total, f.path);
        }
    }
    return stats;
}

}  // namespace magda::media
