// Tests for the Phase B file walker + classifier (issue #768).
// Mirrors prototypes/media_db/tests/test_scan.py.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../magda/daw/media_db/Scan.hpp"

namespace fs = std::filesystem;

namespace {

// RAII tmp dir that cleans itself up on destruction.
class TempDir {
  public:
    TempDir() {
        path_ = fs::temp_directory_path() /
                ("magda_media_db_test_" + std::to_string(std::random_device{}()));
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

void touch(const fs::path& p, std::string_view content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream{p} << content;
}

}  // namespace

TEST_CASE("classify recognises audio extension", "[media_db][scan]") {
    TempDir dir;
    auto p = dir.path() / "kick.wav";
    touch(p);
    auto sf = magda::media::classify(p);
    REQUIRE(sf.has_value());
    REQUIRE(sf->kind == "audio");
    REQUIRE(sf->format == "wav");
    REQUIRE(sf->sizeBytes == 1);
}

TEST_CASE("classify recognises preset extension", "[media_db][scan]") {
    TempDir dir;
    auto p = dir.path() / "lead.vstpreset";
    touch(p);
    auto sf = magda::media::classify(p);
    REQUIRE(sf.has_value());
    REQUIRE(sf->kind == "preset");
    REQUIRE(sf->format == "vstpreset");
}

TEST_CASE("classify recognises clip extension", "[media_db][scan]") {
    TempDir dir;
    auto p = dir.path() / "loop.mid";
    touch(p);
    auto sf = magda::media::classify(p);
    REQUIRE(sf.has_value());
    REQUIRE(sf->kind == "clip");
}

TEST_CASE("classify is case-insensitive on extension", "[media_db][scan]") {
    TempDir dir;
    auto p = dir.path() / "Kick.WAV";
    touch(p);
    auto sf = magda::media::classify(p);
    REQUIRE(sf.has_value());
    REQUIRE(sf->kind == "audio");
    REQUIRE(sf->format == "wav");
}

TEST_CASE("classify returns nullopt for unknown extensions", "[media_db][scan]") {
    TempDir dir;
    auto p = dir.path() / "readme.txt";
    touch(p);
    REQUIRE_FALSE(magda::media::classify(p).has_value());
}

TEST_CASE("classify returns nullopt for missing files", "[media_db][scan]") {
    REQUIRE_FALSE(magda::media::classify("/no/such/path.wav").has_value());
}

TEST_CASE("walk recurses and filters non-classified files", "[media_db][scan]") {
    TempDir dir;
    touch(dir.path() / "a/b/kick.wav");
    touch(dir.path() / "a/snare.aiff");
    touch(dir.path() / "a/notes.txt");
    touch(dir.path() / "loop.mid");

    std::set<std::string> names;
    magda::media::walk(dir.path(), [&](const magda::media::ScannedFile& f) {
        names.insert(f.path.filename().string());
    });

    REQUIRE(names.size() == 3);
    REQUIRE(names.count("kick.wav") == 1);
    REQUIRE(names.count("snare.aiff") == 1);
    REQUIRE(names.count("loop.mid") == 1);
    REQUIRE(names.count("notes.txt") == 0);
}
