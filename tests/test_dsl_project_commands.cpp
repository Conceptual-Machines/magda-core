#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/agents/dsl_interpreter.hpp"

using namespace magda;
using Catch::Approx;

TEST_CASE("DSL project.set changes tempo and time signature", "[dsl][project]") {
    test::MockMagdaApi api;
    dsl::Interpreter interp(api);

    REQUIRE(interp.execute("project.set(bpm=132, time_signature=\"7/8\")"));
    CHECK(api.project_.info.tempo == Approx(132.0));
    CHECK(api.project_.info.timeSignatureNumerator == 7);
    CHECK(api.project_.info.timeSignatureDenominator == 8);
}

TEST_CASE("DSL clip placement uses the active project time signature", "[dsl][project][clips]") {
    test::MockMagdaApi api;
    dsl::Interpreter interp(api);

    REQUIRE(interp.execute("project.set(time_signature=\"3/4\")\n"
                           "track(name=\"Lead\", new=true).clip.new(bar=2, length_bars=4)"));

    REQUIRE(api.clips_.midiCreations.size() == 1);
    CHECK(api.clips_.midiCreations.front().startBeats == Approx(3.0));
    CHECK(api.clips_.midiCreations.front().lengthBeats == Approx(12.0));
}

TEST_CASE("DSL project state snapshot includes timing context", "[dsl][project][context]") {
    test::MockMagdaApi api;
    api.project_.info.tempo = 98.0;
    api.project_.info.timeSignatureNumerator = 5;
    api.project_.info.timeSignatureDenominator = 4;

    const auto snapshot = dsl::Interpreter::buildStateSnapshot(api);
    CHECK(snapshot.contains("\"tempo_bpm\": 98"));
    CHECK(snapshot.contains("\"time_signature\": \"5/4\""));
}
