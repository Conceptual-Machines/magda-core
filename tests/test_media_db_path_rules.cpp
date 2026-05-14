// Tests for the Phase B path-derived metadata helpers (issue #768).
// Mirrors prototypes/media_db/tests/test_derive.py and test_parse_key.py.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>

#include "../magda/daw/media_db/PathRules.hpp"

using magda::media::parseBpmFromPath;
using magda::media::ParsedKey;
using magda::media::parseKeyFromPath;
using magda::media::pathFamilyHint;
using magda::media::pathTags;

// ---- pathFamilyHint -------------------------------------------------------

TEST_CASE("pathFamilyHint picks vocal from /Vocals/ folder", "[media_db][path_rules]") {
    auto fam = pathFamilyHint("/SamplePacks/Some Pack/Vocals/MTVR_dry.wav");
    REQUIRE(fam.has_value());
    REQUIRE(*fam == "vocal");
}

TEST_CASE("pathFamilyHint leaf folder beats pack-name ancestor", "[media_db][path_rules]") {
    // Pack name has 'Bass' in 'Drum & Bass Toolkit', but the leaf folder
    // is /Snares/ — leaf-first traversal must win.
    auto fam =
        pathFamilyHint("/SamplePacks/LAUT 'Drum & Bass Toolkit'/Snares/snare_resonating.wav");
    REQUIRE(fam.has_value());
    REQUIRE(*fam == "drum");
}

TEST_CASE("pathFamilyHint falls back to filename when no folder hint", "[media_db][path_rules]") {
    auto fam = pathFamilyHint("/random/Vol2/MTVR_kick_punchy.wav");
    REQUIRE(fam.has_value());
    REQUIRE(*fam == "drum");
}

TEST_CASE("pathFamilyHint returns nullopt for unfamiliar names", "[media_db][path_rules]") {
    REQUIRE_FALSE(pathFamilyHint("/Music/track1.wav").has_value());
    REQUIRE_FALSE(pathFamilyHint("/random/Modern_Trap/x.wav").has_value());
}

// ---- pathTags -------------------------------------------------------------

TEST_CASE("pathTags emits leaf-first deduped keywords", "[media_db][path_rules]") {
    auto tags = pathTags("/Pack/Vocals/Adlibs/dry.wav");
    REQUIRE(tags.size() >= 2);
    REQUIRE(tags[0].first == "adlibs");  // leaf folder first
    REQUIRE(tags[0].second == 1.0F);
    REQUIRE(tags[1].first == "vocals");
}

TEST_CASE("pathTags is empty when no keyword matches", "[media_db][path_rules]") {
    REQUIRE(pathTags("/Music/random_song.wav").empty());
}

// ---- parseKeyFromPath: positive cases ------------------------------------

TEST_CASE("parseKeyFromPath: bare root", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("kick_C.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "C");
    REQUIRE_FALSE(k->scale.has_value());
}

TEST_CASE("parseKeyFromPath: inline minor", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("synth_Cm.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "C");
    REQUIRE(k->scale == "minor");
}

TEST_CASE("parseKeyFromPath: inline minor long-form", "[media_db][path_rules][key]") {
    REQUIRE(parseKeyFromPath("synth_Cmin.wav")->scale == "minor");
    REQUIRE(parseKeyFromPath("synth_Cminor.wav")->scale == "minor");
}

TEST_CASE("parseKeyFromPath: inline major long-form", "[media_db][path_rules][key]") {
    REQUIRE(parseKeyFromPath("synth_Cmaj.wav")->scale == "major");
    REQUIRE(parseKeyFromPath("synth_Cmajor.wav")->scale == "major");
}

TEST_CASE("parseKeyFromPath: sharp accidental", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("synth_C#.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "C#");
    REQUIRE_FALSE(k->scale.has_value());
}

TEST_CASE("parseKeyFromPath: sharp + minor", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("synth_F#m.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "F#");
    REQUIRE(k->scale == "minor");
}

TEST_CASE("parseKeyFromPath: flat normalizes to sharp", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("synth_Bb.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "A#");
    REQUIRE_FALSE(k->scale.has_value());

    REQUIRE(parseKeyFromPath("synth_Bbmin.wav")->root == "A#");
    REQUIRE(parseKeyFromPath("synth_Bbmin.wav")->scale == "minor");
    REQUIRE(parseKeyFromPath("synth_Db_loop.wav")->root == "C#");
}

TEST_CASE("parseKeyFromPath: split-token form C_minor", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("pad_C_minor.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "C");
    REQUIRE(k->scale == "minor");
}

TEST_CASE("parseKeyFromPath: split-token form F#_major", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("pad_F#_major.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "F#");
    REQUIRE(k->scale == "major");
}

TEST_CASE("parseKeyFromPath: last match wins", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("C_to_F.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "F");
}

TEST_CASE("parseKeyFromPath: nested in middle of name", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("MTVR2_120bpm_Cm.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "C");
    REQUIRE(k->scale == "minor");
}

TEST_CASE("parseKeyFromPath: bare letter at end", "[media_db][path_rules][key]") {
    auto k = parseKeyFromPath("kick_punchy_F.wav");
    REQUIRE(k.has_value());
    REQUIRE(k->root == "F");
    REQUIRE_FALSE(k->scale.has_value());
}

// ---- parseKeyFromPath: negative cases ------------------------------------

TEST_CASE("parseKeyFromPath: words like Modern/Trap/Vocals don't match",
          "[media_db][path_rules][key]") {
    REQUIRE_FALSE(parseKeyFromPath("Modern_Trap_Vocals.wav").has_value());
}

TEST_CASE("parseKeyFromPath: lowercase letters never match root", "[media_db][path_rules][key]") {
    REQUIRE_FALSE(parseKeyFromPath("guitar_strum.wav").has_value());
}

TEST_CASE("parseKeyFromPath: filename with no key marker", "[media_db][path_rules][key]") {
    REQUIRE_FALSE(parseKeyFromPath("kick_punchy.wav").has_value());
}

TEST_CASE("parseKeyFromPath: words like Animal don't match", "[media_db][path_rules][key]") {
    REQUIRE_FALSE(parseKeyFromPath("Animal_Sounds.wav").has_value());
}

// ---- parseBpmFromPath ----------------------------------------------------

TEST_CASE("parseBpmFromPath: inline integer with bpm suffix", "[media_db][path_rules][bpm]") {
    REQUIRE(parseBpmFromPath("kick_120bpm.wav") == 120.0);
    REQUIRE(parseBpmFromPath("loop_85bpm.wav") == 85.0);
    REQUIRE(parseBpmFromPath("hard_174BPM.wav") == 174.0);
}

TEST_CASE("parseBpmFromPath: separator between number and bpm", "[media_db][path_rules][bpm]") {
    REQUIRE(parseBpmFromPath("kick_120_bpm.wav") == 120.0);
    REQUIRE(parseBpmFromPath("kick_120-BPM.wav") == 120.0);
    REQUIRE(parseBpmFromPath("kick 120 bpm.wav") == 120.0);
}

TEST_CASE("parseBpmFromPath: fractional BPM", "[media_db][path_rules][bpm]") {
    REQUIRE(parseBpmFromPath("trap_85.5bpm.wav") == 85.5);
}

TEST_CASE("parseBpmFromPath: last match wins", "[media_db][path_rules][bpm]") {
    REQUIRE(parseBpmFromPath("variant_92bpm_then_140BPM.wav") == 140.0);
}

TEST_CASE("parseBpmFromPath: bare number without bpm marker is not a match",
          "[media_db][path_rules][bpm]") {
    REQUIRE_FALSE(parseBpmFromPath("kick_120.wav").has_value());
    REQUIRE_FALSE(parseBpmFromPath("MTVR2_dry.wav").has_value());
}

TEST_CASE("parseBpmFromPath: out-of-range values are rejected", "[media_db][path_rules][bpm]") {
    REQUIRE_FALSE(parseBpmFromPath("weird_20bpm.wav").has_value());   // < 30
    REQUIRE_FALSE(parseBpmFromPath("weird_500bpm.wav").has_value());  // > 300
}
