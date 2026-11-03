// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/Config.hpp"

using magda::Config;

TEST_CASE("Following a MIDI clock and Ableton Link turn each other off", "[settings][3190]") {
    auto config = Config::createDetached();

    config->setAbletonLinkEnabled(true);
    config->setMidiClockInput("Drum Machine");
    CHECK(!config->getAbletonLinkEnabled());

    config->setAbletonLinkEnabled(true);
    CHECK(config->getMidiClockInput().empty());

    // Clearing the input leaves Link alone.
    config->setMidiClockInput("");
    CHECK(config->getAbletonLinkEnabled());
}

TEST_CASE("MIDI clock settings survive a save", "[settings][3190]") {
    auto config = Config::createDetached();
    config->setMidiClockOutputs({"Synth A", "Synth B"});
    config->setMidiClockInput("Drum Machine");

    const auto copy = config->clone();
    CHECK(copy->getMidiClockOutputs() == std::vector<std::string>{"Synth A", "Synth B"});
    CHECK(copy->getMidiClockInput() == "Drum Machine");
}
