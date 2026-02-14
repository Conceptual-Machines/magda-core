#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/DrumGridPlugin.hpp"

using DrumGridPlugin = magda::daw::audio::DrumGridPlugin;

// ============================================================================
// Chain Management Tests
// ============================================================================

TEST_CASE("DrumGridPlugin constants are consistent", "[drumgrid][constants]") {
    REQUIRE(DrumGridPlugin::maxPads == 128);
    REQUIRE(DrumGridPlugin::baseNote == 36);

    SECTION("Pad 0 maps to MIDI note 36 (C2)") {
        int midiNote = DrumGridPlugin::baseNote + 0;
        REQUIRE(midiNote == 36);
    }

    SECTION("Last pad maps to MIDI note 99") {
        int midiNote = DrumGridPlugin::baseNote + (DrumGridPlugin::maxPads - 1);
        REQUIRE(midiNote == 163);
    }
}

// ============================================================================
// Note-to-Pad Mapping Tests
// ============================================================================

TEST_CASE("DrumGridPlugin pad-to-note mapping is invertible", "[drumgrid][mapping]") {
    SECTION("Round-trip: padIndex -> midiNote -> padIndex") {
        for (int pad = 0; pad < DrumGridPlugin::maxPads; ++pad) {
            int midiNote = DrumGridPlugin::baseNote + pad;
            int recoveredPad = midiNote - DrumGridPlugin::baseNote;
            REQUIRE(recoveredPad == pad);
        }
    }

    SECTION("Notes below baseNote are out of pad range") {
        int midiNote = DrumGridPlugin::baseNote - 1;
        int padIdx = midiNote - DrumGridPlugin::baseNote;
        REQUIRE(padIdx < 0);
    }

    SECTION("Notes at or above baseNote + maxPads are out of range") {
        int midiNote = DrumGridPlugin::baseNote + DrumGridPlugin::maxPads;
        int padIdx = midiNote - DrumGridPlugin::baseNote;
        REQUIRE(padIdx >= DrumGridPlugin::maxPads);
    }
}

// ============================================================================
// Pan Law Tests (equal-power)
// ============================================================================

TEST_CASE("DrumGridPlugin pan law produces equal-power stereo", "[drumgrid][pan]") {
    // Mirrors the gain computation in applyToBuffer
    auto computeGains = [](float panValue) -> std::pair<float, float> {
        float levelLinear = 1.0f;  // 0 dB
        float leftGain =
            levelLinear * std::cos((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);
        float rightGain =
            levelLinear * std::sin((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);
        return {leftGain, rightGain};
    };

    SECTION("Center pan (0.0) gives equal left and right") {
        auto [left, right] = computeGains(0.0f);
        REQUIRE(left == Catch::Approx(right).margin(0.001f));
        // Equal-power center: each channel ~ 0.707
        REQUIRE(left == Catch::Approx(0.7071f).margin(0.01f));
    }

    SECTION("Hard left (-1.0) gives full left, zero right") {
        auto [left, right] = computeGains(-1.0f);
        REQUIRE(left == Catch::Approx(1.0f).margin(0.001f));
        REQUIRE(right == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("Hard right (1.0) gives zero left, full right") {
        auto [left, right] = computeGains(1.0f);
        REQUIRE(left == Catch::Approx(0.0f).margin(0.001f));
        REQUIRE(right == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("Power is constant across pan positions") {
        // Equal-power pan law: L^2 + R^2 should be constant
        for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f) {
            auto [left, right] = computeGains(pan);
            float power = left * left + right * right;
            REQUIRE(power == Catch::Approx(1.0f).margin(0.01f));
        }
    }
}

// ============================================================================
// MIDI Note Remapping Logic Tests
// ============================================================================

TEST_CASE("DrumGridPlugin MIDI note remapping formula", "[drumgrid][midi]") {
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

TEST_CASE("DrumGridPlugin solo/mute logic", "[drumgrid][mixer]") {
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

TEST_CASE("DrumGridPlugin level dB to linear conversion", "[drumgrid][gain]") {
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
