// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lame.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

/**
 * @file test_lame_encoder.cpp
 * @brief The vendored libmp3lame builds and encodes (#3032).
 *
 * A smoke test of the library alone, before anything in MAGDA writes MP3: one
 * second of sine in, a stream of well-formed MPEG-1 Layer III frames out.
 */

namespace {

constexpr int kSampleRate = 44100;
constexpr int kSamplesPerFrame = 1152;

// MPEG-1 Layer III bitrates by header index, in kbps (ISO 11172-3, 2.4.2.3).
constexpr std::array<int, 15> kBitratesKbps = {0,   32,  40,  48,  56,  64,  80, 96,
                                               112, 128, 160, 192, 224, 256, 320};

std::vector<unsigned char> encodeSine(int bitrateKbps) {
    std::vector<float> left(kSampleRate);
    std::vector<float> right(kSampleRate);
    for (auto i = 0; i < kSampleRate; ++i) {
        left[static_cast<std::size_t>(i)] =
            0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * i / kSampleRate));
        right[static_cast<std::size_t>(i)] = left[static_cast<std::size_t>(i)];
    }

    auto* flags = lame_init();
    REQUIRE(flags != nullptr);
    lame_set_in_samplerate(flags, kSampleRate);
    lame_set_num_channels(flags, 2);
    lame_set_brate(flags, bitrateKbps);
    lame_set_write_id3tag_automatic(flags, 0);
    REQUIRE(lame_init_params(flags) >= 0);

    // LAME's own worst case for the output buffer: 1.25 times the samples plus 7200.
    std::vector<unsigned char> mp3(static_cast<std::size_t>((kSampleRate * 5 / 4) + 7200));
    const auto written = lame_encode_buffer_ieee_float(
        flags, left.data(), right.data(), kSampleRate, mp3.data(), static_cast<int>(mp3.size()));
    REQUIRE(written >= 0);

    const auto flushed =
        lame_encode_flush(flags, mp3.data() + written, static_cast<int>(mp3.size()) - written);
    REQUIRE(flushed >= 0);
    lame_close(flags);

    mp3.resize(static_cast<std::size_t>(written + flushed));
    return mp3;
}

}  // namespace

TEST_CASE("LAME encodes a sine as MPEG-1 Layer III frames", "[lame]") {
    const auto mp3 = encodeSine(128);
    REQUIRE(mp3.size() > 4);

    // Walk the stream frame by frame: each header has to be in sync and say
    // MPEG-1, Layer III, 44.1 kHz, and its length has to land on the next one.
    std::size_t offset = 0;
    auto frames = 0;
    while (offset + 4 <= mp3.size()) {
        const auto* header = mp3.data() + offset;
        REQUIRE(header[0] == 0xFF);
        REQUIRE((header[1] & 0xFE) == 0xFA);      // sync, MPEG-1, Layer III
        REQUIRE(((header[2] >> 2) & 0x03) == 0);  // 44.1 kHz

        const auto bitrate = kBitratesKbps.at(static_cast<std::size_t>(header[2] >> 4));
        REQUIRE(bitrate > 0);
        const auto padding = (header[2] >> 1) & 0x01;
        offset += static_cast<std::size_t>((144000 * bitrate / kSampleRate) + padding);
        ++frames;
    }

    CHECK(offset == mp3.size());
    CHECK(frames >= kSampleRate / kSamplesPerFrame);
}

TEST_CASE("LAME's output grows with the bitrate", "[lame]") {
    CHECK(encodeSine(320).size() > encodeSine(64).size() * 3);
}
