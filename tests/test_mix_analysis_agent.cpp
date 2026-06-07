#include <catch2/catch_test_macros.hpp>
#include <iostream>

#include "magda/agents/mixing_agent.hpp"

using namespace magda;

// ============================================================================
// Whole-mix "listening" agent (#886, exploratory).
//
// Two tests:
//   * a deterministic payload test that runs in CI (no network) and reports the
//     heaviest-case payload size, so we can watch token cost as we iterate;
//   * a hidden [.]-tagged live test that actually calls the configured LLM and
//     prints the analysis. Run it explicitly:
//       ./cmake-build-debug/tests/magda_tests "[mix_analysis][llm]"
//     It is skipped from the normal suite (no keys / cost / nondeterminism).
// ============================================================================

namespace {

// A realistic, busy session: full drum kit, layered bass, doubled guitars,
// keys, synths, a vocal stack, FX and a couple of busses -- ~32 sources, which
// is roughly the heaviest payload we expect to send in one shot.
MixAnalysisAgent::Input buildHeavyMix() {
    MixAnalysisAgent::Input in;

    auto add = [&in](const char* name, const char* role, float lufs, float peak, float plr,
                     float psr, float corr, float width) {
        MixAnalysisAgent::TrackMix t;
        t.name = name;
        t.role = role;
        t.integratedLufs = lufs;
        t.shortTermLufs = lufs + 3.0f;
        t.samplePeakDb = peak;
        t.truePeakDb = peak;
        t.truePeakValid = true;
        t.plr = plr;
        t.psr = psr;
        t.correlation = corr;
        t.width = width;
        in.tracks.push_back(t);
    };

    // name, role, LUFS-I, peak dB, PLR, PSR, corr, width
    add("Kick In", "kick", -9.0f, -3.0f, 11.0f, 8.0f, 1.00f, 0.00f);
    add("Kick Sub", "kick", -10.0f, -4.0f, 9.0f, 7.0f, 1.00f, 0.00f);
    add("Snare Top", "snare", -11.0f, -4.0f, 14.0f, 10.0f, 1.00f, 0.02f);
    add("Snare Bottom", "snare", -16.0f, -8.0f, 13.0f, 9.0f, 1.00f, 0.02f);
    add("Hi-Hats", "hats", -18.0f, -10.0f, 16.0f, 12.0f, 0.85f, 0.20f);
    add("Overheads", "oh", -15.0f, -6.0f, 17.0f, 13.0f, 0.30f, 0.65f);
    add("Room", "room", -17.0f, -7.0f, 18.0f, 14.0f, 0.10f, 0.80f);
    add("Tom 1", "tom", -20.0f, -10.0f, 12.0f, 9.0f, 1.00f, 0.05f);
    add("Tom 2", "tom", -20.0f, -10.0f, 12.0f, 9.0f, 1.00f, 0.05f);
    add("Floor Tom", "tom", -19.0f, -9.0f, 12.0f, 9.0f, 1.00f, 0.05f);

    add("Bass DI", "bass", -10.0f, -5.0f, 8.0f, 6.0f, 1.00f, 0.00f);
    add("Bass Amp", "bass", -11.0f, -5.0f, 9.0f, 6.0f, 1.00f, 0.05f);

    add("Rhythm Gtr L", "guitar", -14.0f, -7.0f, 10.0f, 8.0f, 1.00f, 1.00f);
    add("Rhythm Gtr R", "guitar", -14.0f, -7.0f, 10.0f, 8.0f, 1.00f, 1.00f);
    add("Lead Gtr", "guitar", -13.0f, -6.0f, 12.0f, 9.0f, 0.95f, 0.30f);
    add("Acoustic Gtr", "guitar", -16.0f, -8.0f, 13.0f, 10.0f, 0.60f, 0.45f);

    add("Piano", "keys", -15.0f, -7.0f, 14.0f, 10.0f, 0.50f, 0.55f);
    add("Rhodes", "keys", -17.0f, -9.0f, 13.0f, 10.0f, 0.55f, 0.50f);
    add("Pad", "keys", -19.0f, -12.0f, 6.0f, 4.0f, 0.20f, 0.75f);

    add("Synth Bass", "synth", -12.0f, -6.0f, 7.0f, 5.0f, 1.00f, 0.10f);
    add("Arp", "synth", -18.0f, -10.0f, 9.0f, 7.0f, 0.40f, 0.60f);
    add("Synth Lead", "synth", -14.0f, -7.0f, 10.0f, 8.0f, 0.70f, 0.35f);

    add("Lead Vox", "vocal", -9.0f, -4.0f, 11.0f, 7.0f, 1.00f, 0.05f);
    add("Lead Vox Dbl", "vocal", -15.0f, -8.0f, 11.0f, 7.0f, 0.90f, 0.40f);
    add("Harmony 1", "vocal", -17.0f, -9.0f, 10.0f, 7.0f, 0.85f, 0.45f);
    add("Harmony 2", "vocal", -17.0f, -9.0f, 10.0f, 7.0f, 0.85f, 0.45f);
    add("BGV Group", "vocal", -16.0f, -8.0f, 12.0f, 9.0f, 0.40f, 0.70f);
    add("Ad-libs", "vocal", -19.0f, -11.0f, 13.0f, 9.0f, 0.75f, 0.30f);

    add("Riser FX", "fx", -22.0f, -14.0f, 8.0f, 6.0f, 0.30f, 0.70f);
    add("Impact FX", "fx", -18.0f, -8.0f, 16.0f, 12.0f, 0.50f, 0.55f);

    add("Drum Bus", "bus", -8.0f, -2.0f, 12.0f, 9.0f, 0.80f, 0.30f);
    add("Vocal Bus", "bus", -8.0f, -3.0f, 11.0f, 8.0f, 0.90f, 0.20f);

    // The final mix bus -- "possibly also the final mix".
    MixAnalysisAgent::TrackMix master;
    master.name = "Master";
    master.role = "master";
    master.integratedLufs = -8.0f;
    master.shortTermLufs = -5.0f;
    master.samplePeakDb = -0.3f;
    master.truePeakDb = -0.1f;
    master.truePeakValid = true;
    master.plr = 7.5f;
    master.psr = 5.5f;
    master.correlation = 0.65f;
    master.width = 0.35f;
    in.master = master;

    in.masking.push_back({"Kick In", "Bass DI", 40.0f, 90.0f, 0.72f});
    in.masking.push_back({"Bass DI", "Synth Bass", 60.0f, 160.0f, 0.81f});
    in.masking.push_back({"Lead Vox", "Lead Gtr", 1500.0f, 3500.0f, 0.58f});
    in.masking.push_back({"Rhythm Gtr L", "Piano", 400.0f, 1200.0f, 0.49f});

    return in;
}

}  // namespace

TEST_CASE("MixAnalysisAgent: heavy payload builds and is reasonably compact", "[mix_analysis]") {
    auto mix = buildHeavyMix();
    auto payload = MixAnalysisAgent::buildUserMessage(mix);

    REQUIRE(payload.isNotEmpty());
    REQUIRE(payload.contains("[MASTER]"));
    REQUIRE(payload.contains("Masking conflicts"));
    REQUIRE(payload.contains("Lead Vox"));

    const int chars = payload.length();
    const int approxTokens = chars / 4;  // rough heuristic, good enough to track trend
    std::cout << "\n[mix_analysis] heaviest payload: " << mix.tracks.size() << " tracks, " << chars
              << " chars (~" << approxTokens << " tokens)\n";

    // Guardrail: if a formatting change blows the payload up, fail loudly so we
    // notice. ~32 tracks of tabular data should sit comfortably under this.
    REQUIRE(chars < 8000);
}

// Hidden: hits the configured provider. Run with "[mix_analysis][llm]".
TEST_CASE("MixAnalysisAgent: live analysis of a full mix", "[.][mix_analysis][llm]") {
    auto mix = buildHeavyMix();
    mix.question = "Is anything fighting for space, and is the master over-compressed?";

    MixAnalysisAgent agent;
    auto result = agent.generate(mix);

    if (result.hasError) {
        WARN("Mix analysis not run (LLM not configured?): " << result.error);
        return;
    }

    std::cout << "\n==== payload (" << result.payload.size() << " chars) ====\n"
              << result.payload << "\n==== analysis (" << result.wallSeconds << "s) ====\n"
              << result.analysis << "\n========\n";

    CHECK_FALSE(result.analysis.empty());
}
