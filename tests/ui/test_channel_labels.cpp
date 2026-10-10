#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/utils/ChannelLabels.hpp"

// A MOTU M4's inputs, as CoreAudio names them: the loopback channels are 5 to 8,
// which a number alone does not say (#2734).

namespace labels = magda::ChannelLabels;

namespace {
const juce::StringArray kM4Inputs{"In 1",       "In 2",       "In 3",           "In 4",
                                  "Loopback 1", "Loopback 2", "Loopback Mix 1", "Loopback Mix 2"};
}

TEST_CASE("Driver channel names are displayed verbatim", "[ui][channels][2734]") {
    CHECK(labels::pair(kM4Inputs, 4, 5) == "Loopback 1 + Loopback 2");
    CHECK(labels::pair(kM4Inputs, 6, 7) == "Loopback Mix 1 + Loopback Mix 2");
    CHECK(labels::mono(kM4Inputs, 6) == "Loopback Mix 1 (mono)");
}

TEST_CASE("Generic driver names are not rewritten", "[ui][channels][2734]") {
    CHECK(labels::pair(kM4Inputs, 0, 1) == "In 1 + In 2");
    CHECK(labels::mono(kM4Inputs, 2) == "In 3 (mono)");

    const juce::StringArray coreAudio{"1", "2"};
    CHECK(labels::pair(coreAudio, 0, 1) == "1 + 2");
    CHECK(labels::mono(coreAudio, 0) == "1 (mono)");

    const juce::StringArray generic{"Output 1", "Output 2"};
    CHECK(labels::pair(generic, 0, 1) == "Output 1 + Output 2");
}

TEST_CASE("A pair with one named channel still says which is which", "[ui][channels][2734]") {
    const juce::StringArray mixed{"In 1", "Talkback"};
    CHECK(labels::pair(mixed, 0, 1) == "In 1 + Talkback");
}

TEST_CASE("A channel past the names falls back to its number", "[ui][channels][2734]") {
    CHECK(labels::mono({}, 3) == "4 (mono)");
    CHECK(labels::pair({}, 0, 1) == "1-2");
}

TEST_CASE("A user's name leads and the socket still shows", "[ui][channels][2259]") {
    magda::ChannelNames user;
    user.setName(2, false, "Neumann");
    user.setName(4, true, "Drum OH");

    CHECK(labels::mono(kM4Inputs, 2, user) == "Neumann (In 3, mono)");
    CHECK(labels::pair(kM4Inputs, 4, 5, user) == "Drum OH (Loopback 1 + Loopback 2)");
    CHECK(labels::mono({}, 2, user) == "Neumann (3, mono)");
    CHECK(labels::pair({}, 4, 5, user) == "Drum OH (5-6)");
}

TEST_CASE("Mono and pair names are separate", "[ui][channels][2259]") {
    magda::ChannelNames user;
    user.setName(4, false, "Mic 1");

    CHECK(labels::mono({}, 4, user) == "Mic 1 (5, mono)");
    CHECK(labels::pair({}, 4, 5, user) == "5-6");
    CHECK(labels::mono({}, 5, user) == "6 (mono)");
}

TEST_CASE("Only an adjacent pair carries a pair name", "[ui][channels][2259]") {
    magda::ChannelNames user;
    user.setName(0, true, "Mains");

    CHECK(labels::pair({}, 0, 1, user) == "Mains (1-2)");
    CHECK(labels::pair({}, 0, 3, user) == "1-4");
}

TEST_CASE("Clearing a name brings the number back", "[ui][channels][2259]") {
    magda::ChannelNames user;
    user.setName(2, false, "Neumann");
    user.setName(2, false, "");

    CHECK(user.empty());
    CHECK(labels::mono({}, 2, user) == "3 (mono)");
}
