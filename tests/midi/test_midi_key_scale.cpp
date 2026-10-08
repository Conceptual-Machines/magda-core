#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/components/pianoroll/MidiEditorKey.hpp"

using namespace magda;

TEST_CASE("Key scale membership", "[midi][key]") {
    SECTION("C major") {
        const KeyScale key{0, 0};
        CHECK(key.contains(60));
        CHECK(key.contains(64));
        CHECK_FALSE(key.contains(61));
        CHECK_FALSE(key.contains(68));
        CHECK(key.isRoot(48));
        CHECK_FALSE(key.isRoot(55));
    }

    SECTION("A minor shares C major's pitches with a different root") {
        const KeyScale key{9, 1};
        for (int note = 0; note < 12; ++note)
            CHECK(key.contains(note) == KeyScale{0, 0}.contains(note));
        CHECK(key.isRoot(57));
    }

    SECTION("no key holds every pitch") {
        const KeyScale key;
        CHECK(key.contains(61));
        CHECK_FALSE(key.isRoot(60));
    }
}
