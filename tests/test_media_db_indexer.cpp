// Phase E1 tests — MediaDbIndexer (issue #768).
//
// Spins up a temp dir with a few tiny WAVs, indexes against an in-memory DB,
// and asserts the expected rows appear. Encoder is null (semantic embedding
// path is tested separately by the encoder's own gated test).

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../magda/daw/media_db/MediaDatabase.hpp"
#include "../magda/daw/media_db/MediaDbIndexer.hpp"

namespace fs = std::filesystem;
using magda::media::MediaDatabase;
using magda::media::MediaDbIndexer;

namespace {

class TempDir {
  public:
    TempDir() {
        path_ = fs::temp_directory_path() /
                ("magda_indexer_test_" + std::to_string(std::random_device{}()));
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

int countRows(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

}  // namespace

TEST_CASE("indexer: empty directory returns zero stats", "[media_db][indexer]") {
    TempDir dir;
    MediaDatabase db(":memory:");
    MediaDbIndexer indexer(db, nullptr);
    auto stats = indexer.indexDirectory(dir.path());
    REQUIRE(stats.inserted == 0);
    REQUIRE(stats.updated == 0);
    REQUIRE(stats.skipped == 0);
    REQUIRE(stats.failed == 0);
}

TEST_CASE("indexer: inserts a single audio file with all expected rows", "[media_db][indexer]") {
    TempDir dir;
    // 3-second loop, not a one-shot — so BPM survives the indexer's
    // "one-shots have no tempo" policy.
    writeMonoWav(dir.path() / "Vocals" / "MTVR_warm_120bpm_Cm.wav", 3.0, 440.0);

    MediaDatabase db(":memory:");
    MediaDbIndexer indexer(db, nullptr);
    auto stats = indexer.indexDirectory(dir.path());
    REQUIRE(stats.inserted == 1);
    REQUIRE(stats.skipped == 0);

    sqlite3* sql = db.handle();
    REQUIRE(countRows(sql, "SELECT COUNT(*) FROM media_file") == 1);
    REQUIRE(countRows(sql, "SELECT COUNT(*) FROM media_fts") == 1);
    REQUIRE(countRows(sql, "SELECT COUNT(*) FROM media_tag WHERE source_model='path'") > 0);
    REQUIRE(countRows(sql, "SELECT COUNT(*) FROM media_embedding") == 0);  // null encoder

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(sql, "SELECT kind, format, family, bpm, key_root, key_scale FROM media_file",
                       -1, &stmt, nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) == "audio");
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) == "wav");
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))) == "vocal");
    REQUIRE(sqlite3_column_double(stmt, 3) == 120.0);  // bpm from filename
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))) == "C");
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))) == "minor");
    sqlite3_finalize(stmt);
}

TEST_CASE("indexer: skips unchanged files on rescan", "[media_db][indexer]") {
    TempDir dir;
    writeMonoWav(dir.path() / "kick_120bpm.wav", 0.5, 100.0);

    MediaDatabase db(":memory:");
    MediaDbIndexer indexer(db, nullptr);
    auto first = indexer.indexDirectory(dir.path());
    REQUIRE(first.inserted == 1);

    auto second = indexer.indexDirectory(dir.path());
    REQUIRE(second.inserted == 0);
    REQUIRE(second.updated == 0);
    REQUIRE(second.skipped == 1);
}

TEST_CASE("indexer: indexes one imported audio file", "[media_db][indexer]") {
    TempDir dir;
    const auto imported = dir.path() / "imported_128bpm.wav";
    writeMonoWav(imported, 3.0, 100.0);

    MediaDatabase db(":memory:");
    MediaDbIndexer indexer(db, nullptr);
    auto first = indexer.indexFile(imported);
    REQUIRE(first.inserted == 1);
    REQUIRE(first.updated == 0);
    REQUIRE(first.skipped == 0);
    REQUIRE(first.failed == 0);
    REQUIRE(countRows(db.handle(), "SELECT COUNT(*) FROM media_file") == 1);

    auto second = indexer.indexFile(imported, MediaDbIndexer::Mode::Incremental);
    REQUIRE(second.inserted == 0);
    REQUIRE(second.updated == 0);
    REQUIRE(second.skipped == 1);
    REQUIRE(second.failed == 0);
    REQUIRE(countRows(db.handle(), "SELECT COUNT(*) FROM media_file") == 1);
}

TEST_CASE("indexer: walks recursively and respects file kinds", "[media_db][indexer]") {
    TempDir dir;
    writeMonoWav(dir.path() / "Drums" / "Kicks" / "kick_120bpm.wav", 0.4, 80.0);
    writeMonoWav(dir.path() / "Pads" / "warm_Cm.wav", 3.0, 220.0);
    // A MIDI file — counts as kind=clip but has no audio features.
    fs::create_directories(dir.path() / "Clips");
    std::ofstream(dir.path() / "Clips" / "groove.mid") << "MThd";

    MediaDatabase db(":memory:");
    MediaDbIndexer indexer(db, nullptr);
    auto stats = indexer.indexDirectory(dir.path());
    REQUIRE(stats.inserted == 3);

    sqlite3* sql = db.handle();
    REQUIRE(countRows(sql, "SELECT COUNT(*) FROM media_file WHERE kind='audio'") == 2);
    REQUIRE(countRows(sql, "SELECT COUNT(*) FROM media_file WHERE kind='clip'") == 1);
    REQUIRE(countRows(sql,
                      "SELECT COUNT(*) FROM media_file WHERE family='drum' AND shape='one-shot'") ==
            1);
    // Pad file is a 3-second tonal sample; should be loop or sustained but
    // crucially NOT one-shot.
    REQUIRE(countRows(sql,
                      "SELECT COUNT(*) FROM media_file WHERE family='pad' AND shape!='one-shot'") ==
            1);
}

TEST_CASE("indexer: progress callback fires for each file", "[media_db][indexer]") {
    TempDir dir;
    writeMonoWav(dir.path() / "a.wav", 0.2, 100.0);
    writeMonoWav(dir.path() / "b.wav", 0.2, 200.0);
    writeMonoWav(dir.path() / "c.wav", 0.2, 300.0);

    MediaDatabase db(":memory:");
    MediaDbIndexer indexer(db, nullptr);
    int lastDone = -1;
    int lastTotal = -1;
    indexer.setProgress([&](int done, int total, const std::filesystem::path&) {
        lastDone = done;
        lastTotal = total;
    });
    indexer.indexDirectory(dir.path());
    REQUIRE(lastTotal == 3);
    REQUIRE(lastDone == 3);
}
