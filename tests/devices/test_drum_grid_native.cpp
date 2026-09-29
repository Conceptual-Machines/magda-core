#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "exec/PlanValues.hpp"
#include "magda/daw/core/DrumGridPads.hpp"

// ============================================================================
// Chain Management Tests
// ============================================================================

TEST_CASE("Native Drum Grid constants are consistent", "[drumgrid][constants]") {
    REQUIRE(magda::kPadCount == 64);
    REQUIRE(magda::kPadBaseNote == 24);

    SECTION("Pad 0 maps to MIDI note 24 (C0)") {
        int midiNote = magda::kPadBaseNote + 0;
        REQUIRE(midiNote == 24);
    }

    SECTION("Last pad maps to MIDI note 87") {
        int midiNote = magda::kPadBaseNote + (magda::kPadCount - 1);
        REQUIRE(midiNote == 87);
    }
}

// ============================================================================
// Note-to-Pad Mapping Tests
// ============================================================================

TEST_CASE("Native Drum Grid pad-to-note mapping is invertible", "[drumgrid][mapping]") {
    SECTION("Round-trip: padIndex -> midiNote -> padIndex") {
        for (int pad = 0; pad < magda::kPadCount; ++pad) {
            int midiNote = magda::kPadBaseNote + pad;
            int recoveredPad = midiNote - magda::kPadBaseNote;
            REQUIRE(recoveredPad == pad);
        }
    }

    SECTION("Notes below baseNote are out of pad range") {
        int midiNote = magda::kPadBaseNote - 1;
        int padIdx = midiNote - magda::kPadBaseNote;
        REQUIRE(padIdx < 0);
    }

    SECTION("Notes at or above baseNote + maxPads are out of range") {
        int midiNote = magda::kPadBaseNote + magda::kPadCount;
        int padIdx = midiNote - magda::kPadBaseNote;
        REQUIRE(padIdx >= magda::kPadCount);
    }
}

// ============================================================================
// Native Pan Law Tests
// ============================================================================

TEST_CASE("Native Drum Grid pan law is linear with unity at centre", "[drumgrid][pan]") {
    // Calls the production gain calculation directly — the audio path in
    // the native executor uses this same function, so the two cannot drift apart.
    auto computeGains = [](float panValue, float levelLinear = 1.0f) -> std::pair<float, float> {
        float leftGain = 0.0f;
        float rightGain = 0.0f;
        magda::engine::applyLinearPanLaw(levelLinear, panValue, leftGain, rightGain);
        return {leftGain, rightGain};
    };

    SECTION("Centre pan (0.0) is unity on both channels") {
        auto [left, right] = computeGains(0.0f);
        REQUIRE(left == Catch::Approx(right).margin(0.001f));
        // Unity, NOT the 0.707 of an equal-power law: a pad at centre must be
        // as loud as the same device sitting directly on a track. Regression
        // guard for pads playing back 3 dB quiet.
        REQUIRE(left == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("Centre pan scales with level") {
        auto [left, right] = computeGains(0.0f, 0.5f);
        REQUIRE(left == Catch::Approx(0.5f).margin(0.001f));
        REQUIRE(right == Catch::Approx(0.5f).margin(0.001f));
    }

    SECTION("Hard left (-1.0) silences the right channel") {
        auto [left, right] = computeGains(-1.0f);
        REQUIRE(right == Catch::Approx(0.0f).margin(0.001f));
        // The favoured channel reaches 2x for both pads and tracks.
        REQUIRE(left == Catch::Approx(2.0f).margin(0.001f));
    }

    SECTION("Hard right (1.0) silences the left channel") {
        auto [left, right] = computeGains(1.0f);
        REQUIRE(left == Catch::Approx(0.0f).margin(0.001f));
        REQUIRE(right == Catch::Approx(2.0f).margin(0.001f));
    }

    SECTION("Sum of both channels is constant across pan positions") {
        // Linear pan law: L + R is invariant (equal-power would hold L^2 + R^2).
        for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f) {
            auto [left, right] = computeGains(pan);
            REQUIRE(left + right == Catch::Approx(2.0f).margin(0.001f));
        }
    }
}

// ============================================================================
// MIDI Note Remapping Logic Tests
// ============================================================================

TEST_CASE("Native Drum Grid MIDI note remapping formula", "[drumgrid][midi]") {
    // Mirrors the remapping logic in applyToBuffer:
    // remappedNote = rootNote + (incoming - lowNote)

    SECTION("Single-note chain: rootNote == lowNote == highNote, no remap") {
        int lowNote = 60, rootNote = 60;
        int incoming = 60;
        int remapped = rootNote + (incoming - lowNote);
        REQUIRE(remapped == 60);
    }

    SECTION("Multi-note range with rootNote offset") {
        int lowNote = 36, rootNote = 60;
        // Incoming note 36 -> remapped to 60
        REQUIRE(rootNote + (36 - lowNote) == 60);
        // Incoming note 48 -> remapped to 72
        REQUIRE(rootNote + (48 - lowNote) == 72);
        // Incoming note 42 -> remapped to 66
        REQUIRE(rootNote + (42 - lowNote) == 66);
    }

    SECTION("Range check: note must be within [lowNote, highNote]") {
        int lowNote = 40, highNote = 50;
        REQUIRE(39 < lowNote);    // below range
        REQUIRE(40 >= lowNote);   // at range start
        REQUIRE(50 <= highNote);  // at range end
        REQUIRE(51 > highNote);   // above range
    }
}

// ============================================================================
// Solo/Mute Logic Tests
// ============================================================================

TEST_CASE("Native Drum Grid solo/mute logic", "[drumgrid][mixer]") {
    // Mirrors the solo detection and skip logic in applyToBuffer

    struct MockChain {
        bool mute = false;
        bool solo = false;
        bool hasPlugins = true;
    };

    auto shouldProcess = [](const MockChain& chain, bool anySoloed) {
        if (!chain.hasPlugins)
            return false;
        if (chain.mute)
            return false;
        if (anySoloed && !chain.solo)
            return false;
        return true;
    };

    SECTION("Normal: all chains process when none soloed/muted") {
        MockChain c1, c2;
        REQUIRE(shouldProcess(c1, false));
        REQUIRE(shouldProcess(c2, false));
    }

    SECTION("Muted chain is skipped") {
        MockChain c1{true, false, true};
        REQUIRE_FALSE(shouldProcess(c1, false));
    }

    SECTION("Solo: only soloed chains process") {
        MockChain soloed{false, true, true};
        MockChain unsoloed{false, false, true};
        bool anySoloed = true;
        REQUIRE(shouldProcess(soloed, anySoloed));
        REQUIRE_FALSE(shouldProcess(unsoloed, anySoloed));
    }

    SECTION("Mute takes precedence over solo") {
        MockChain mutedAndSoloed{true, true, true};
        REQUIRE_FALSE(shouldProcess(mutedAndSoloed, true));
    }

    SECTION("Empty chain is always skipped") {
        MockChain empty{false, false, false};
        REQUIRE_FALSE(shouldProcess(empty, false));
    }
}

// ============================================================================
// Gain Calculation Tests
// ============================================================================

TEST_CASE("Native Drum Grid level dB to linear conversion", "[drumgrid][gain]") {
    SECTION("0 dB = unity gain") {
        float gain = juce::Decibels::decibelsToGain(0.0f);
        REQUIRE(gain == Catch::Approx(1.0f));
    }

    SECTION("-6 dB ~ half amplitude") {
        float gain = juce::Decibels::decibelsToGain(-6.0f);
        REQUIRE(gain == Catch::Approx(0.5012f).margin(0.01f));
    }

    SECTION("-inf dB = silence") {
        float gain = juce::Decibels::decibelsToGain(-100.0f);
        REQUIRE(gain < 0.00001f);
    }

    SECTION("+6 dB ~ double amplitude") {
        float gain = juce::Decibels::decibelsToGain(6.0f);
        REQUIRE(gain == Catch::Approx(1.9953f).margin(0.01f));
    }
}
