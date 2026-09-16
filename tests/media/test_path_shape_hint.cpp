// What a producer called a file (#2674).

#include <catch2/catch_test_macros.hpp>

#include "../../magda/daw/media_db/PathRules.hpp"

using magda::media::pathShapeHint;

TEST_CASE("A name that says one-shot is one, however long the file is", "[media][shape]") {
    // The case from a real library: 60 seconds of drone, classified a loop
    // because it has transients and lasts more than two seconds.
    REQUIRE(pathShapeHint("/x/SPLC-0499_FX_Oneshot_Factory_Drones_Ambience.wav") == "one-shot");
    REQUIRE(pathShapeHint("/x/AUPVF_Vocal_Shot135_A.wav") == "one-shot");
    REQUIRE(pathShapeHint("/x/SH_FFX_123BPM_SFX_HIT_10.wav") == "one-shot");
    REQUIRE(pathShapeHint("/x/brass_stabs_03.wav") == "one-shot");
}

TEST_CASE("A name that says loop is one", "[media][shape]") {
    REQUIRE(pathShapeHint("/x/OS_NDNB_174_Hat_Loop_22__PercTops_.wav") == "loop");
    REQUIRE(pathShapeHint("/x/TEDDY_KILLERZ_drum_loop_hihat_140.wav") == "loop");
}

TEST_CASE("Loop wins over a hit inside the same name", "[media][shape]") {
    // A loop containing a hit, not a hit.
    REQUIRE(pathShapeHint("/x/drum_loop_hit_05.wav") == "loop");
    REQUIRE(pathShapeHint("/x/TS_stabs_loop_120.wav") == "loop");
}

TEST_CASE("The filename decides before its folders do", "[media][shape]") {
    REQUIRE(pathShapeHint("/packs/One Shots/drum_loop_120.wav") == "loop");
    REQUIRE(pathShapeHint("/packs/Loops/vocal_shot_01.wav") == "one-shot");
}

TEST_CASE("A name that says neither says nothing", "[media][shape]") {
    REQUIRE(!pathShapeHint("/x/MNT_FT_vocal_opera_A.wav").has_value());
    REQUIRE(!pathShapeHint("/x/SO_MTS_120_ruby_C.wav").has_value());
}
