// Phase E2 tests — MediaDbQuery (issue #768).
//
// Uses the indexer to populate an in-memory DB from synthetic WAVs, then
// runs queries against it. Encoder/tokenizer are nullable in the query layer
// (no-AI-pack story); the hybrid-with-CLAP path is exercised by encoder /
// tokenizer tests separately.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <sqlite3.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "../magda/daw/media_db/MediaDatabase.hpp"
#include "../magda/daw/media_db/MediaDbIndexer.hpp"
#include "../magda/daw/media_db/MediaDbQuery.hpp"

namespace fs = std::filesystem;
using magda::media::MediaDatabase;
using magda::media::MediaDbIndexer;
using magda::media::MediaDbQuery;
using magda::media::QueryFilters;
using magda::media::QueryResult;
using magda::media::QuerySort;
using magda::media::QuerySortField;

namespace {

class TempDir {
  public:
    TempDir() {
        path_ = fs::temp_directory_path() /
                ("magda_query_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
};

void writeMonoWav(const fs::path& out, double seconds, double freq, int sampleRate = 44100) {
    fs::create_directories(out.parent_path());
    juce::File jf(juce::String(out.string()));
    jf.deleteFile();
    juce::WavAudioFormat wav;
    juce::StringPairArray meta;
    std::unique_ptr<juce::FileOutputStream> stream(jf.createOutputStream());
    REQUIRE(stream != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sampleRate, 1, 16, meta, 0));
    REQUIRE(writer != nullptr);
    stream.release();

    const int n = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> buf(1, n);
    auto* data = buf.getWritePointer(0);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    for (int i = 0; i < n; ++i) {
        data[i] = static_cast<float>(0.3 * std::sin(kTwoPi * freq * i / sampleRate));
    }
    writer->writeFromAudioSampleBuffer(buf, 0, n);
}

// Standard fixture used across the test cases below.
void populateCorpus(const fs::path& root, MediaDatabase& db) {
    writeMonoWav(root / "Drums" / "Kicks" / "kick_punchy_120bpm.wav", 0.4, 80.0);
    writeMonoWav(root / "Drums" / "Snares" / "snare_tight.wav", 0.5, 200.0);
    writeMonoWav(root / "Vocals" / "Adlibs" / "vocal_dry_Cm.wav", 3.0, 440.0);
    writeMonoWav(root / "Pads" / "warm_analog.wav", 4.0, 220.0);
    writeMonoWav(root / "Bass" / "808_140bpm.wav", 3.5, 60.0);

    MediaDbIndexer indexer(db, nullptr);
    auto stats = indexer.indexDirectory(root);
    REQUIRE(stats.inserted == 5);
}

std::int64_t insertAudioRow(MediaDatabase& db, const std::string& path, std::int64_t indexedAt) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "INSERT INTO media_file "
                               "(path, kind, format, size_bytes, mtime_ns, indexed_at) "
                               "VALUES (?, 'audio', 'wav', 0, 0, ?) RETURNING id",
                               -1, &stmt, nullptr) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, indexedAt);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto id = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return id;
}

void insertEmbedding(MediaDatabase& db, std::int64_t fileId, int dim, const void* data,
                     int byteCount) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(),
                               "INSERT INTO media_embedding "
                               "(file_id, model_id, model_version, vector_dim, vector_blob) "
                               "VALUES (?, 'test-model', '1', ?, ?)",
                               -1, &stmt, nullptr) == SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, fileId);
    sqlite3_bind_int(stmt, 2, dim);
    sqlite3_bind_blob(stmt, 3, data, byteCount, SQLITE_TRANSIENT);
    REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

void insertEmbedding(MediaDatabase& db, std::int64_t fileId, const std::vector<float>& vec) {
    insertEmbedding(db, fileId, static_cast<int>(vec.size()), vec.data(),
                    static_cast<int>(vec.size() * sizeof(float)));
}

}  // namespace

TEST_CASE("query: filter-only browse with no text", "[media_db][query]") {
    TempDir dir;
    MediaDatabase db(":memory:");
    populateCorpus(dir.path(), db);

    MediaDbQuery q(db, nullptr, nullptr);

    SECTION("no filters returns everything") {
        auto results = q.search(std::nullopt, {});
        REQUIRE(results.size() == 5);
        // Filter-only mode: scores are NaN.
        for (const auto& r : results) {
            REQUIRE(std::isnan(r.score));
        }
    }

    SECTION("family filter narrows the result set") {
        QueryFilters f;
        f.family = "drum";
        auto results = q.search(std::nullopt, f);
        REQUIRE(results.size() == 2);
        for (const auto& r : results) {
            REQUIRE(r.family == "drum");
        }
    }

    SECTION("combined family + shape filter") {
        QueryFilters f;
        f.family = "drum";
        f.shape = "one-shot";
        auto results = q.search(std::nullopt, f);
        REQUIRE(results.size() == 2);
        for (const auto& r : results) {
            REQUIRE(r.shape == "one-shot");
        }
    }

    SECTION("bpm range filter") {
        QueryFilters f;
        f.bpmMin = 100.0;
        f.bpmMax = 145.0;
        auto results = q.search(std::nullopt, f);
        // 808_140bpm.wav -> 140 bpm, in range.
        REQUIRE(results.size() == 1);
        REQUIRE(results.front().bpm.value() == 140.0);
    }

    SECTION("limit caps result count") {
        auto results = q.search(std::nullopt, {}, 2);
        REQUIRE(results.size() == 2);
    }

    SECTION("name sort orders by displayed filename") {
        auto asc = q.search(std::nullopt, {}, 5, 0, {}, QuerySort{QuerySortField::Name, true});
        REQUIRE(asc.size() == 5);
        REQUIRE(asc.front().path.filename().string() == "808_140bpm.wav");
        REQUIRE(asc.back().path.filename().string() == "warm_analog.wav");

        auto desc = q.search(std::nullopt, {}, 5, 0, {}, QuerySort{QuerySortField::Name, false});
        REQUIRE(desc.size() == 5);
        REQUIRE(desc.front().path.filename().string() == "warm_analog.wav");
        REQUIRE(desc.back().path.filename().string() == "808_140bpm.wav");
    }
}

TEST_CASE("query: FTS-only when text given without encoder", "[media_db][query]") {
    TempDir dir;
    MediaDatabase db(":memory:");
    populateCorpus(dir.path(), db);

    MediaDbQuery q(db, nullptr, nullptr);

    SECTION("'kick' matches the kick one-shot via path FTS") {
        auto results = q.search("kick", {});
        REQUIRE_FALSE(results.empty());
        // The kick file should be the top hit. Score is a real number, not NaN.
        REQUIRE_FALSE(std::isnan(results.front().score));
        REQUIRE(results.front().path.filename().string().find("kick") != std::string::npos);
    }

    SECTION("'vocal' picks up the vocal file") {
        auto results = q.search("vocal", {});
        REQUIRE_FALSE(results.empty());
        REQUIRE(results.front().family == "vocal");
    }

    SECTION("nonsense text returns nothing") {
        auto results = q.search("zzzz_no_match_xyz", {});
        REQUIRE(results.empty());
    }

    SECTION("text + family filter intersect") {
        // "drum" matches both /Drums/Kicks/ and /Drums/Snares/ via path FTS.
        // Filter to shape=one-shot — both drums are one-shots, vocals/pads aren't.
        QueryFilters f;
        f.shape = "one-shot";
        auto results = q.search("drum", f);
        for (const auto& r : results) {
            REQUIRE(r.shape == "one-shot");
        }
    }
}

TEST_CASE("query: limit respected with text", "[media_db][query]") {
    TempDir dir;
    MediaDatabase db(":memory:");
    populateCorpus(dir.path(), db);

    MediaDbQuery q(db, nullptr, nullptr);
    // Add a query that potentially matches multiple rows
    auto results = q.search("snare", {}, /*limit=*/1);
    REQUIRE(results.size() <= 1);
}

TEST_CASE("query: similarTo scores embedding blobs without requiring encoder",
          "[media_db][query][embedding]") {
    MediaDatabase db(":memory:");

    const auto seed = insertAudioRow(db, "seed.wav", 10);
    const auto closest = insertAudioRow(db, "closest.wav", 9);
    const auto farther = insertAudioRow(db, "farther.wav", 8);
    const auto wrongDim = insertAudioRow(db, "wrong_dim.wav", 7);
    const auto badBytes = insertAudioRow(db, "bad_bytes.wav", 6);

    insertEmbedding(db, seed, std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});
    insertEmbedding(db, closest, std::vector<float>{0.75F, 0.25F, 0.0F, 0.0F});
    insertEmbedding(db, farther, std::vector<float>{0.5F, 0.5F, 0.0F, 0.0F});
    insertEmbedding(db, wrongDim, std::vector<float>{1.0F, 0.0F});

    const float truncated[] = {1.0F, 0.0F, 0.0F};
    insertEmbedding(db, badBytes, 4, truncated, static_cast<int>(sizeof(truncated)));

    MediaDbQuery q(db, nullptr, nullptr);
    const auto results = q.similarTo(seed, {}, 10);

    REQUIRE(results.size() == 2);
    REQUIRE(results[0].fileId == closest);
    REQUIRE(results[0].score == Catch::Approx(0.75F).epsilon(0.0001F));
    REQUIRE(results[1].fileId == farther);
    REQUIRE(results[1].score == Catch::Approx(0.5F).epsilon(0.0001F));
}
