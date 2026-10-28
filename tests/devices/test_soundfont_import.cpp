// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// SoundFont presets into the sampler's zone map (#2210).

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/audio/plugins/MagdaSamplerPlugin.hpp"
#include "magda/daw/audio/sampling/SamplerImport.hpp"

using Sampler = magda::daw::audio::MagdaSamplerPlugin;
namespace si = magda::sampler_import;

namespace {

juce::File freshDir(const juce::String& name) {
    auto dir =
        juce::File(juce::SystemStats::getEnvironmentVariable(
                       "TMPDIR",
                       juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName()))
            .getChildFile("magda_soundfont_import")
            .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

/// Little-endian records, as the format writes them.
struct Bytes {
    juce::MemoryOutputStream out;
    Bytes& u8(int value) {
        out.writeByte(static_cast<char>(value));
        return *this;
    }
    Bytes& u16(int value) {
        out.writeShort(static_cast<short>(value));
        return *this;
    }
    Bytes& u32(juce::uint32 value) {
        out.writeInt(static_cast<int>(value));
        return *this;
    }
    Bytes& name(const juce::String& text) {
        char padded[20] = {};
        text.copyToUTF8(padded, 20);
        out.write(padded, 20);
        return *this;
    }
    Bytes& raw(const juce::MemoryBlock& block) {
        out.write(block.getData(), block.getSize());
        return *this;
    }
    juce::MemoryBlock block() const {
        return out.getMemoryBlock();
    }
};

juce::MemoryBlock chunk(const char* id, const juce::MemoryBlock& data) {
    Bytes bytes;
    bytes.out.write(id, 4);
    bytes.u32(static_cast<juce::uint32>(data.getSize())).raw(data);
    if (data.getSize() % 2 != 0)
        bytes.u8(0);
    return bytes.block();
}

juce::MemoryBlock list(const char* type, std::initializer_list<juce::MemoryBlock> chunks) {
    Bytes body;
    body.out.write(type, 4);
    for (const auto& c : chunks)
        body.raw(c);
    return chunk("LIST", body.block());
}

/// Presets "Lead" (program 5: 60 cB down, keys up to 100) and "Other" (program 1), both playing
/// instrument "Lead": a looped mono sample below C4 and a linked stereo pair from C4, rooted at
/// 72, under a global half-second attack.
juce::File writeSoundFont(const juce::File& file) {
    constexpr int kMono = 1000, kPair = 500, kGap = 46;
    Bytes smpl;
    const auto frames = [&smpl](int count, float hz) {
        for (int i = 0; i < count; ++i)
            smpl.u16(static_cast<juce::int16>(8000.0 * std::sin(i * hz)));
        for (int i = 0; i < kGap; ++i)
            smpl.u16(0);
    };
    frames(kMono, 0.05f);
    frames(kPair, 0.07f);
    frames(kPair, 0.11f);

    Bytes phdr;
    phdr.name("Lead").u16(5).u16(0).u16(0).u32(0).u32(0).u32(0);
    phdr.name("Other").u16(1).u16(0).u16(1).u32(0).u32(0).u32(0);
    phdr.name("EOP").u16(0).u16(0).u16(2).u32(0).u32(0).u32(0);
    Bytes pbag;
    pbag.u16(0).u16(0).u16(3).u16(0).u16(4).u16(0);
    Bytes pgen;
    pgen.u16(43).u16(0 | (100 << 8)).u16(48).u16(60).u16(41).u16(0);
    pgen.u16(41).u16(0);
    pgen.u16(0).u16(0);
    Bytes inst;
    inst.name("Lead").u16(0);
    inst.name("EOI").u16(3);
    Bytes ibag;
    ibag.u16(0).u16(0).u16(1).u16(0).u16(4).u16(0).u16(7).u16(0);
    Bytes igen;
    igen.u16(34).u16(-1200);
    igen.u16(43).u16(0 | (59 << 8)).u16(54).u16(1).u16(53).u16(0);
    igen.u16(43).u16(60 | (127 << 8)).u16(58).u16(72).u16(53).u16(1);
    igen.u16(0).u16(0);
    Bytes mods;
    for (int i = 0; i < 10; ++i)
        mods.u8(0);
    Bytes shdr;
    const auto sample = [&shdr](const char* name, juce::uint32 start, juce::uint32 end,
                                juce::uint32 loopStart, juce::uint32 loopEnd, int link, int type) {
        shdr.name(name).u32(start).u32(end).u32(loopStart).u32(loopEnd).u32(22050);
        shdr.u8(60).u8(0).u16(link).u16(type);
    };
    const juce::uint32 left = kMono + kGap, right = left + kPair + kGap;
    sample("Mono", 0, kMono, 100, 900, 0, 1);
    sample("PairL", left, left + kPair, left, left + kPair, 2, 4);
    sample("PairR", right, right + kPair, right, right + kPair, 1, 2);
    sample("EOS", 0, 0, 0, 0, 0, 0);

    Bytes ifil;
    ifil.u16(2).u16(4);
    Bytes body;
    body.out.write("sfbk", 4);
    body.raw(list("INFO", {chunk("ifil", ifil.block())}));
    body.raw(list("sdta", {chunk("smpl", smpl.block())}));
    body.raw(list("pdta", {chunk("phdr", phdr.block()), chunk("pbag", pbag.block()),
                           chunk("pmod", mods.block()), chunk("pgen", pgen.block()),
                           chunk("inst", inst.block()), chunk("ibag", ibag.block()),
                           chunk("imod", mods.block()), chunk("igen", igen.block()),
                           chunk("shdr", shdr.block())}));
    REQUIRE(file.replaceWithData(chunk("RIFF", body.block()).getData(),
                                 chunk("RIFF", body.block()).getSize()));
    return file;
}

int channelsOf(const juce::String& path) {
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(juce::File(path).createInputStream().release(), true));
    return reader != nullptr ? static_cast<int>(reader->numChannels) : 0;
}

}  // namespace

TEST_CASE("A SoundFont's presets list in bank and program order", "[sampler][import][2210]") {
    const auto dir = freshDir("presets");
    const auto font = writeSoundFont(dir.getChildFile("Test.sf2"));

    const auto presets = si::soundFontPresets(font);
    REQUIRE(presets.size() == 2);
    CHECK(presets[0].name == "Other");
    CHECK(presets[0].program == 1);
    CHECK(presets[0].index == 1);
    CHECK(presets[1].name == "Lead");
    CHECK(presets[1].index == 0);

    CHECK(si::soundFontPresets(dir.getChildFile("missing.sf2")).empty());
    CHECK(si::isSoundFont(font));
    CHECK(si::isInstrumentFile(font));
}

TEST_CASE("A SoundFont preset becomes zones over its samples, written out as WAVs",
          "[sampler][import][2210]") {
    const auto dir = freshDir("import");
    const auto font = writeSoundFont(dir.getChildFile("Test.sf2"));
    const auto samples = dir.getChildFile("samples");

    const auto lead = si::importSoundFont(font, 0, samples);
    REQUIRE(lead.error.isEmpty());
    REQUIRE(lead.zones.size() == 2);

    const auto& mono = lead.zones[0];
    CHECK(juce::File(mono.source).isAChildOf(samples));
    CHECK(channelsOf(mono.source) == 1);
    CHECK(mono.zones.keyLow == 0);
    CHECK(mono.zones.keyHigh == 59);
    CHECK(mono.rootNote == 60);
    // 60 cB, scaled by 0.4 as players do.
    CHECK(mono.gainDb == Catch::Approx(-2.4f));
    CHECK(mono.region.end == Catch::Approx(1000.0 / 22050.0));
    CHECK(mono.region.loop);
    CHECK(mono.region.loopStart == Catch::Approx(100.0 / 22050.0));
    CHECK(mono.region.loopEnd == Catch::Approx(900.0 / 22050.0));

    // The linked pair is one stereo zone, its keys inside the preset's.
    const auto& pair = lead.zones[1];
    CHECK(channelsOf(pair.source) == 2);
    CHECK(pair.zones.keyLow == 60);
    CHECK(pair.zones.keyHigh == 100);
    CHECK(pair.rootNote == 72);
    CHECK_FALSE(pair.region.loop);

    CHECK(lead.parameters.at(Sampler::kAttack) == Catch::Approx(0.5f));

    // Another preset over the same instrument reuses the written samples.
    const auto other = si::importSoundFont(font, 1, samples);
    REQUIRE(other.zones.size() == 2);
    CHECK(other.zones[0].source == mono.source);
    CHECK(other.zones[0].gainDb == Catch::Approx(0.0f));
    CHECK(other.zones[1].zones.keyHigh == 127);
}

TEST_CASE("A file that is not a SoundFont says so", "[sampler][import][2210]") {
    const auto dir = freshDir("bad");
    const auto file = dir.getChildFile("Bad.sf2");
    file.replaceWithText("not a soundfont");
    const auto imported = si::importSoundFont(file, 0, dir);
    CHECK(imported.zones.empty());
    CHECK(imported.error == "Not a SoundFont");
    CHECK(si::importSoundFont(writeSoundFont(dir.getChildFile("ok.sf2")), 5, dir).error ==
          "No such preset");
}
