// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/agents/dsl_interpreter.hpp"

using magda::dsl::containsStatement;

TEST_CASE("containsStatement tells DSL from a prose reply", "[dsl]") {
    CHECK(containsStatement("track(name=\"Bass\", new=true)"));
    CHECK(containsStatement("filter(tracks, track.name == \"Bass\").delete()"));
    CHECK(containsStatement("groove.list()"));
    CHECK(containsStatement("project.set(bpm=120)"));
    CHECK(containsStatement("Sure, here it is:\ntrack(id=1).fx.add(name=\"reverb\")"));

    CHECK_FALSE(containsStatement(""));
    CHECK_FALSE(containsStatement("I can create tracks, add a groove and set up a track."));
    CHECK_FALSE(containsStatement("Create a new track and add a plugin to it."));
    CHECK_FALSE(containsStatement("You can change the groove. Ask me about the project."));
}
