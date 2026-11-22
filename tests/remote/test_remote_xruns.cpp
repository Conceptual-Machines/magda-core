// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/api/remote_xruns.hpp"

/// @file engine.health's xrun problems: the log's events against the polled count (#3386).

using namespace magda;
using namespace magda::remote;

namespace {

XrunLog::Event eventAt(std::int64_t atMs, int count,
                       std::initializer_list<XrunLog::Callback> before) {
    XrunLog::Event event;
    event.atMs = atMs;
    event.count = count;
    for (const auto& callback : before)
        event.callbacks[static_cast<std::size_t>(event.numCallbacks++)] = callback;
    return event;
}

XrunLog::Callback took(float renderMs, float writerWaitMs = 0.0f) {
    return {.atMs = 1, .renderMs = renderMs, .lengthMs = 2.0f, .writerWaitMs = writerWaitMs};
}

}  // namespace

TEST_CASE("An xrun the log saw is reported at its own time, once", "[remote][xrun][3386]") {
    XrunProblems problems;
    const auto read = problems.read({eventAt(1200, 1, {took(1.0f)})}, 1, 5000);
    REQUIRE(read.size() == 1);
    CHECK(static_cast<juce::int64>(read[0]["atMs"]) == 1200);
    CHECK(read[0]["callbacks"].size() == 1);

    CHECK(problems.read({}, 0, 6000).empty());
}

TEST_CASE("An xrun the log saw only after the count is not reported twice",
          "[remote][xrun][3386]") {
    XrunProblems problems;
    CHECK(problems.read({}, 1, 5000).empty());
    const auto read = problems.read({eventAt(4990, 1, {})}, 0, 6000);
    REQUIRE(read.size() == 1);
    CHECK(static_cast<juce::int64>(read[0]["atMs"]) == 4990);
    CHECK(problems.read({}, 0, 7000).empty());
}

TEST_CASE("A rise the log never explains is reported at the read that saw it",
          "[remote][xrun][3386]") {
    XrunProblems problems;
    CHECK(problems.read({}, 2, 5000).empty());
    const auto read = problems.read({}, 0, 6000);
    REQUIRE(read.size() == 1);
    CHECK(static_cast<juce::int64>(read[0]["atMs"]) == 5000);
    CHECK(static_cast<int>(read[0]["count"]) == 2);
    CHECK_FALSE(read[0].hasProperty("callbacks"));
}

TEST_CASE("An xrun's likely cause comes from the last callback that overran",
          "[remote][xrun][3401]") {
    CHECK(juce::String(likelyCause(eventAt(0, 1, {took(1.0f), took(1.5f)}))) == "outsideRender");
    CHECK(juce::String(likelyCause(eventAt(0, 1, {took(3.0f), took(1.0f)}))) == "overran");
    CHECK(juce::String(likelyCause(eventAt(0, 1, {took(1.0f), took(2.5f, 0.8f)}))) == "writerWait");
    CHECK(juce::String(likelyCause(eventAt(0, 1, {}))) == "outsideRender");
}

TEST_CASE("An xrun problem with its callbacks fits engine.health's schema",
          "[remote][xrun][3401]") {
    auto event = eventAt(1200, 1, {took(1.0f), took(3.0f)});
    event.sinceStart = 4;
    juce::Array<juce::var> list{xrunProblem(event), plainProblem("audio_xrun", 1300, 1)};

    auto* health = new juce::DynamicObject();
    health->setProperty("engine", "Native");
    health->setProperty("observedAtMs", 2000);
    health->setProperty("sinceMs", 500);
    health->setProperty("projectBound", true);
    health->setProperty("audioDeviceOpen", true);
    health->setProperty("xrunCount", 2);
    health->setProperty("dropoutCount", juce::var());
    health->setProperty("callbackLoad", 0.3);
    health->setProperty("renderAheadMisses", 0);
    health->setProperty("renderAheadDiscards", 0);
    health->setProperty("problemCoverage", "audioIoObservations");
    health->setProperty("problems", list);
    health->setProperty("discardedProblemCount", 0);

    const auto* operation = OperationRegistry::instance().find("engine.health");
    REQUIRE(operation != nullptr);
    CHECK(validateJson(juce::var(health), operation->outputSchema).empty());
}
