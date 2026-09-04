#include <catch2/catch_test_macros.hpp>

#include "plugins/MagdaSamplerPlugin.hpp"

/**
 * @file test_sampler_playback.cpp
 * @brief The sampler's one cross-thread boundary (#2384).
 *
 * What the audio thread may read about the loaded sound, and nothing else: an
 * installed SamplerSound is immutable, so anything a reader needs is published
 * here or it does not cross.
 */

using magda::daw::audio::SamplerPlayback;
using magda::daw::audio::SamplerSound;

TEST_CASE("A sampler that has loaded nothing reads its fallbacks", "[devices][sampler][playback]") {
    // What the audio thread gets before any sound is installed, which is a
    // rate it can divide by rather than a zero.
    const SamplerPlayback playback;
    const auto facts = playback.read();

    CHECK(facts.sourceRate == 44100.0);
    CHECK(facts.lengthSeconds == 0.0);
    CHECK(facts.rootNote == 60);
}

TEST_CASE("A publication carries every field the audio thread reads",
          "[devices][sampler][playback]") {
    SamplerPlayback playback;
    playback.publish(SamplerPlayback::Facts{96000.0, 2.5, 48});

    const auto facts = playback.read();

    CHECK(facts.sourceRate == 96000.0);
    CHECK(facts.lengthSeconds == 2.5);
    CHECK(facts.rootNote == 48);
}

TEST_CASE("Moving the root note leaves the loaded sound's facts alone",
          "[devices][sampler][playback]") {
    // The one field a user changes without loading anything, so it publishes on
    // its own rather than round-tripping the sound.
    SamplerPlayback playback;
    playback.publish(SamplerPlayback::Facts{48000.0, 1.25, 60});

    playback.publishRootNote(72);

    const auto facts = playback.read();

    CHECK(facts.rootNote == 72);
    CHECK(facts.sourceRate == 48000.0);
    CHECK(facts.lengthSeconds == 1.25);
}

TEST_CASE("An installed sound carries nothing that changes", "[devices][sampler][playback]") {
    // The invariant the boundary rests on: every field of a SamplerSound is set
    // before the synthesiser takes it and never written again, so the audio
    // thread reading one cannot race a message-thread edit.
    static_assert(std::is_same_v<decltype(SamplerSound::sourceSampleRate), double>,
                  "a sound's rate is plain data, not something published through it");

    SamplerSound sound;
    CHECK_FALSE(sound.hasData());
    CHECK(sound.sourceSampleRate == 44100.0);
}
