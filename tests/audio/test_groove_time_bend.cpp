#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "music/GrooveLibrary.hpp"

/**
 * Grooves as time bends (#3044): a groove's curve plays every note where the groove engine did.
 */

using Catch::Approx;
using magda::GrooveTemplateData;

namespace {

/// The groove engine's map from written beat to played beat (GrooveTemplate::groovyBeat).
double groovyBeat(const GrooveTemplateData& groove, double beat) {
    const auto n = static_cast<long long>(groove.latenessProportions.size());
    const double grid = groove.notesPerBeat;
    const double step = std::floor(beat * grid);
    const double offset = beat * grid - step;
    const auto index = ((static_cast<long long>(step) % n) + n) % n;
    const double l = groove.latenessProportions[static_cast<size_t>(index)];
    const double next = groove.latenessProportions[static_cast<size_t>((index + 1) % n)];
    return (step + 0.5 * l + offset * (1.0 + 0.5 * (next - l))) / grid;
}

/// Where the bend plays a note written at @p beat, the first time in [0, 16).
double bentBeat(const magda::sdk::sequencer::TimeBend& bend, double beatsPerBar, double beat) {
    double played = NAN;
    magda::sdk::sequencer::forEachBentHit(
        bend, bend.cycleBeats(beatsPerBar), 0.0, 16.0,
        [beat](double lo, double hi, const auto& visit) {
            if (beat >= lo && beat <= hi)
                visit(beat, 0);
        },
        [&](double real, int) {
            if (std::isnan(played))
                played = real;
        });
    return played;
}

void checkMatches(const GrooveTemplateData& groove, double beatsPerBar) {
    INFO(groove.name.toStdString() << " in " << beatsPerBar << " beats a bar");
    const auto bend = magda::grooveTimeBend(groove, beatsPerBar);
    REQUIRE(bend.has_value());
    // Grid steps and the points between them, over two bars, kept clear of the window's ends.
    for (double beat = 0.125; beat < 8.0; beat += 1.0 / 24.0) {
        INFO("beat " << beat);
        CHECK(bentBeat(*bend, beatsPerBar, beat) == Approx(groovyBeat(groove, beat)).margin(1e-6));
    }
}

}  // namespace

TEST_CASE("A swing plays as a time bend", "[groove][timebend]") {
    checkMatches({"8th swing", 2, true, {0.0f, 0.66f}}, 4.0);
    checkMatches({"16th swing", 4, true, {0.0f, 1.0f}}, 4.0);
    checkMatches({"Quarter swing", 1, true, {0.0f, 0.4f}}, 4.0);
}

TEST_CASE("Pushes, pulls and slow-fast grooves play as time bends", "[groove][timebend]") {
    checkMatches({"PushSwing", 4, true, {0.0f, 1.0f, 0.0f, 1.0f, -0.5f, 1.0f, 0.0f, 1.0f}}, 4.0);
    checkMatches({"PushSnare", 2, true, {0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f}}, 4.0);
    checkMatches({"Slow Fast", 1, true, {0.0f, 0.5f, 1.0f, 0.5f}}, 4.0);
}

TEST_CASE("A groove with a late downbeat holds, then plays it late", "[groove][timebend]") {
    checkMatches({"slightly random",
                  2,
                  false,
                  {0.046f, -0.033f, -0.026f, 0.039f, -0.039f, 0.0f, 0.026f, -0.046f, 0.033f,
                   -0.053f, 0.039f, -0.046f, -0.033f, 0.039f, 0.053f, -0.046f}},
                 4.0);
}

TEST_CASE("A groove fits another meter by repeating inside one curve", "[groove][timebend]") {
    // A 2-beat groove in 3/4: two repeats make a 4-beat cycle, which two bars of 6 do not divide,
    // but four bars of 3 over 3 cycles do.
    checkMatches({"Slow Fast 2", 2, true, {0.0f, 0.25f, 0.5f, 0.25f}}, 3.0);
}

TEST_CASE("Amount is the groove's strength", "[groove][timebend]") {
    const GrooveTemplateData swing{"8th swing", 2, true, {0.0f, 0.66f}};
    const auto half = magda::grooveTimeBend(swing, 4.0, 0.5f);
    REQUIRE(half.has_value());
    CHECK(half->amount == 0.5f);
    CHECK_FALSE(magda::grooveTimeBend({"empty", 2, true, {}}, 4.0).has_value());
}
