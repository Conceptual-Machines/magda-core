// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include "sampling/SamplerImport.hpp"
#include "sampling/SamplerImportBuilder.hpp"

// SoundFont 2.04 (#2210): RIFF `sfbk` with the samples in `sdta`/`smpl` and the presets,
// instruments and their generators in `pdta`. SF3 is the same with each sample an Ogg Vorbis
// stream, its start and end byte offsets and its loop relative to its own first frame.

namespace magda::sampler_import {

namespace {

using namespace detail;

// Generator operators, by their numbers in the specification.
enum Gen {
    kStartOffset = 0,
    kEndOffset = 1,
    kLoopStartOffset = 2,
    kLoopEndOffset = 3,
    kStartCoarse = 4,
    kModLfoToPitch = 5,
    kVibLfoToPitch = 6,
    kModEnvToPitch = 7,
    kFilterFc = 8,
    kFilterQ = 9,
    kModLfoToFilterFc = 10,
    kModEnvToFilterFc = 11,
    kEndCoarse = 12,
    kModLfoToVolume = 13,
    kPan = 17,
    kModEnvDelay = 25,
    kModEnvAttack = 26,
    kModEnvHold = 27,
    kModEnvDecay = 28,
    kModEnvSustain = 29,
    kModEnvRelease = 30,
    kKeyToModEnvHold = 31,
    kKeyToModEnvDecay = 32,
    kVolEnvDelay = 33,
    kVolEnvAttack = 34,
    kVolEnvHold = 35,
    kVolEnvDecay = 36,
    kVolEnvSustain = 37,
    kVolEnvRelease = 38,
    kKeyToVolEnvHold = 39,
    kKeyToVolEnvDecay = 40,
    kInstrument = 41,
    kKeyRange = 43,
    kVelRange = 44,
    kLoopStartCoarse = 45,
    kKeynum = 46,
    kVelocity = 47,
    kAttenuation = 48,
    kLoopEndCoarse = 50,
    kCoarseTune = 51,
    kFineTune = 52,
    kSampleId = 53,
    kSampleModes = 54,
    kScaleTuning = 56,
    kExclusiveClass = 57,
    kRootKey = 58,
};

int defaultOf(int gen) {
    switch (gen) {
        case kFilterFc:
            return 13500;
        case kModEnvDelay:
        case kModEnvAttack:
        case kModEnvHold:
        case kModEnvDecay:
        case kModEnvRelease:
        case kVolEnvDelay:
        case kVolEnvAttack:
        case kVolEnvHold:
        case kVolEnvDecay:
        case kVolEnvRelease:
            return -12000;
        case kScaleTuning:
            return 100;
        case kRootKey:
        case kKeynum:
        case kVelocity:
            return -1;
        case kKeyRange:
        case kVelRange:
            return 127 << 8;
        default:
            return 0;
    }
}

using Gens = std::map<int, int>;

struct Bag {
    int gen = 0;
    int mod = 0;
};

struct Header {
    juce::String name;
    int bag = 0;
    int bank = 0, program = 0;
};

struct SampleHeader {
    juce::String name;
    juce::uint32 start = 0, end = 0, loopStart = 0, loopEnd = 0, rate = 44100;
    int originalPitch = 60;
    int correction = 0;
    int link = 0;
    int type = 1;
};

constexpr int kRight = 2, kLeft = 4, kCompressed = 0x10, kRom = 0x8000;

int u16(const std::vector<juce::uint8>& data, size_t at) {
    return data[at] | (data[at + 1] << 8);
}
int s16(const std::vector<juce::uint8>& data, size_t at) {
    return static_cast<juce::int16>(u16(data, at));
}
juce::uint32 u32(const std::vector<juce::uint8>& data, size_t at) {
    return static_cast<juce::uint32>(u16(data, at)) |
           (static_cast<juce::uint32>(u16(data, at + 2)) << 16);
}
juce::String name20(const std::vector<juce::uint8>& data, size_t at) {
    size_t length = 0;
    while (length < 20 && data[at + length] != 0)
        ++length;
    return juce::String::fromUTF8(reinterpret_cast<const char*>(data.data() + at),
                                  static_cast<int>(length))
        .trim();
}

/// A SoundFont's tables, and where its sample data sits in the file.
struct SoundFont {
    juce::int64 smplOffset = 0;
    juce::int64 smplSize = 0;
    bool compressed = false;  ///< SF3.
    std::vector<Header> presets, instruments;
    std::vector<Bag> presetBags, instrumentBags;
    std::vector<std::pair<int, int>> presetGens, instrumentGens;
    int presetModulators = 0, instrumentModulators = 0;
    std::vector<SampleHeader> samples;
};

juce::uint32 readTag(juce::InputStream& in) {
    return static_cast<juce::uint32>(in.readInt());
}

constexpr juce::uint32 tag(const char (&name)[5]) {
    return static_cast<juce::uint32>(name[0]) | (static_cast<juce::uint32>(name[1]) << 8) |
           (static_cast<juce::uint32>(name[2]) << 16) | (static_cast<juce::uint32>(name[3]) << 24);
}

std::optional<SoundFont> readSoundFont(const juce::File& file) {
    juce::FileInputStream in(file);
    if (!in.openedOk() || readTag(in) != tag("RIFF"))
        return std::nullopt;
    const auto riffEnd =
        std::min<juce::int64>(8 + static_cast<juce::uint32>(in.readInt()), in.getTotalLength());
    if (readTag(in) != tag("sfbk"))
        return std::nullopt;

    SoundFont font;
    std::map<juce::uint32, std::vector<juce::uint8>> tables;
    while (in.getPosition() + 8 <= riffEnd) {
        const auto id = readTag(in);
        const auto size = static_cast<juce::uint32>(in.readInt());
        const auto next = in.getPosition() + size + (size & 1);
        if (id == tag("LIST")) {
            const auto list = readTag(in);
            while (in.getPosition() + 8 <= next) {
                const auto chunk = readTag(in);
                const auto chunkSize = static_cast<juce::uint32>(in.readInt());
                const auto chunkNext = in.getPosition() + chunkSize + (chunkSize & 1);
                if (list == tag("INFO") && chunk == tag("ifil") && chunkSize >= 2) {
                    font.compressed = in.readShort() >= 3;
                } else if (list == tag("sdta") && chunk == tag("smpl")) {
                    font.smplOffset = in.getPosition();
                    font.smplSize = chunkSize;
                } else if (list == tag("pdta")) {
                    auto& table = tables[chunk];
                    table.resize(chunkSize);
                    if (in.read(table.data(), static_cast<int>(chunkSize)) !=
                        static_cast<int>(chunkSize))
                        return std::nullopt;
                }
                in.setPosition(chunkNext);
            }
        }
        in.setPosition(next);
    }

    const auto records = [&tables](const char(&name)[5], size_t size) {
        const auto& table = tables[tag(name)];
        return table.size() / size;
    };
    const auto& phdr = tables[tag("phdr")];
    for (size_t i = 0; i < records("phdr", 38); ++i)
        font.presets.push_back({name20(phdr, i * 38), u16(phdr, i * 38 + 24),
                                u16(phdr, i * 38 + 22), u16(phdr, i * 38 + 20)});
    const auto& inst = tables[tag("inst")];
    for (size_t i = 0; i < records("inst", 22); ++i)
        font.instruments.push_back({name20(inst, i * 22), u16(inst, i * 22 + 20), 0, 0});
    const auto readBags = [](const std::vector<juce::uint8>& table, std::vector<Bag>& bags) {
        for (size_t i = 0; i + 4 <= table.size(); i += 4)
            bags.push_back({u16(table, i), u16(table, i + 2)});
    };
    readBags(tables[tag("pbag")], font.presetBags);
    readBags(tables[tag("ibag")], font.instrumentBags);
    const auto readGens = [](const std::vector<juce::uint8>& table,
                             std::vector<std::pair<int, int>>& gens) {
        for (size_t i = 0; i + 4 <= table.size(); i += 4)
            gens.emplace_back(u16(table, i), s16(table, i + 2));
    };
    readGens(tables[tag("pgen")], font.presetGens);
    readGens(tables[tag("igen")], font.instrumentGens);
    // Each list ends in a terminal record; more than that is a modulator the sampler cannot play.
    font.presetModulators = static_cast<int>(records("pmod", 10)) - 1;
    font.instrumentModulators = static_cast<int>(records("imod", 10)) - 1;
    const auto& shdr = tables[tag("shdr")];
    for (size_t i = 0; i < records("shdr", 46); ++i) {
        const auto at = i * 46;
        font.samples.push_back({name20(shdr, at), u32(shdr, at + 20), u32(shdr, at + 24),
                                u32(shdr, at + 28), u32(shdr, at + 32), u32(shdr, at + 36),
                                shdr[at + 40], static_cast<juce::int8>(shdr[at + 41]),
                                u16(shdr, at + 42), u16(shdr, at + 44)});
    }
    if (font.presets.size() < 2 || font.instruments.size() < 2 || font.samples.empty() ||
        font.smplSize == 0)
        return std::nullopt;
    return font;
}

/// Bag @p bag's generators, each operator once, the last written winning.
Gens gensOf(const std::vector<Bag>& bags, const std::vector<std::pair<int, int>>& gens,
            size_t bag) {
    Gens out;
    if (bag + 1 >= bags.size())
        return out;
    for (auto i = static_cast<size_t>(bags[bag].gen);
         i < static_cast<size_t>(bags[bag + 1].gen) && i < gens.size(); ++i)
        out[gens[i].first] = gens[i].second;
    return out;
}

/// The zones of a preset or instrument: its global zone's generators, then each local zone's,
/// over them. A zone is global when it is first and names no instrument or sample.
std::vector<Gens> zonesOf(const std::vector<Header>& headers, size_t index,
                          const std::vector<Bag>& bags,
                          const std::vector<std::pair<int, int>>& gens, int terminal) {
    std::vector<Gens> zones;
    Gens global;
    const auto first = static_cast<size_t>(headers[index].bag);
    const auto last = static_cast<size_t>(headers[index + 1].bag);
    for (auto bag = first; bag < last; ++bag) {
        const auto own = gensOf(bags, gens, bag);
        if (!own.contains(terminal)) {
            if (bag == first)
                global = own;
            continue;
        }
        auto merged = global;
        for (const auto& [gen, amount] : own)
            merged[gen] = amount;
        zones.push_back(std::move(merged));
    }
    return zones;
}

std::pair<int, int> rangeOf(const Gens& gens, int gen) {
    const auto it = gens.find(gen);
    const int amount = it != gens.end() ? it->second : defaultOf(gen);
    return {amount & 0xff, (amount >> 8) & 0xff};
}

double seconds(int timecents) {
    return std::exp2(timecents / 1200.0);
}

/// Samples written as WAV files the zones point at, each once per import.
class SampleWriter {
  public:
    SampleWriter(const juce::File& font, const SoundFont& data, const juce::File& folder)
        : font_(font), data_(data), folder_(folder) {}

    /// Sample @p index, with @p right as its right channel; an empty file if it cannot be read.
    juce::File write(int index, std::optional<int> right) {
        const auto& header = data_.samples[static_cast<size_t>(index)];
        auto name = juce::File::createLegalFileName(header.name).replaceCharacter(' ', '_');
        const auto file = folder_.getChildFile(
            juce::String(index) + (right ? "+" + juce::String(*right) : juce::String()) + "-" +
            name + ".wav");
        if (file.existsAsFile() && file.getSize() > 0)
            return file;

        auto left = read(index);
        if (left.getNumSamples() == 0)
            return {};
        juce::AudioBuffer<float> audio(right ? 2 : 1, left.getNumSamples());
        audio.copyFrom(0, 0, left, 0, 0, left.getNumSamples());
        if (right) {
            const auto other = read(*right);
            audio.clear(1, 0, audio.getNumSamples());
            audio.copyFrom(1, 0, other, 0, 0,
                           std::min(other.getNumSamples(), audio.getNumSamples()));
        }

        folder_.createDirectory();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        if (stream == nullptr)
            return {};
        juce::WavAudioFormat wav;
        auto writer =
            wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                            .withSampleRate(header.rate)
                                            .withNumChannels(audio.getNumChannels())
                                            .withBitsPerSample(data_.compressed ? 24 : 16));
        if (writer == nullptr ||
            !writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
            return {};
        return file;
    }

  private:
    juce::AudioBuffer<float> read(int index) {
        const auto& header = data_.samples[static_cast<size_t>(index)];
        juce::AudioBuffer<float> audio;
        if (header.end <= header.start)
            return audio;
        juce::FileInputStream in(font_);
        if (!in.openedOk())
            return audio;

        if (data_.compressed || (header.type & kCompressed) != 0) {
            juce::MemoryBlock bytes(header.end - header.start);
            in.setPosition(data_.smplOffset + header.start);
            in.read(bytes.getData(), static_cast<int>(bytes.getSize()));
            juce::OggVorbisAudioFormat ogg;
            std::unique_ptr<juce::AudioFormatReader> reader(
                ogg.createReaderFor(new juce::MemoryInputStream(bytes, true), true));
            if (reader == nullptr)
                return audio;
            audio.setSize(1, static_cast<int>(reader->lengthInSamples));
            reader->read(&audio, 0, audio.getNumSamples(), 0, true, false);
            return audio;
        }

        const auto frames = static_cast<int>(header.end - header.start);
        if (data_.smplOffset + 2 * static_cast<juce::int64>(header.end) >
            data_.smplOffset + data_.smplSize)
            return audio;
        // 16-bit little-endian PCM; an SF2.04 `sm24` low byte is not read.
        juce::MemoryBlock pcm(static_cast<size_t>(frames) * 2);
        in.setPosition(data_.smplOffset + 2 * static_cast<juce::int64>(header.start));
        if (in.read(pcm.getData(), frames * 2) != frames * 2)
            return audio;
        audio.setSize(1, frames);
        const auto* bytes = static_cast<const char*>(pcm.getData());
        auto* out = audio.getWritePointer(0);
        for (int i = 0; i < frames; ++i)
            out[i] = static_cast<juce::int16>(juce::ByteOrder::littleEndianShort(bytes + 2 * i)) /
                     32768.0f;
        return audio;
    }

    juce::File font_;
    const SoundFont& data_;
    juce::File folder_;
};

}  // namespace

std::vector<SoundFontPreset> soundFontPresets(const juce::File& file) {
    std::vector<SoundFontPreset> presets;
    const auto font = readSoundFont(file);
    if (!font)
        return presets;
    for (size_t i = 0; i + 1 < font->presets.size(); ++i)
        presets.push_back({font->presets[i].name, font->presets[i].bank, font->presets[i].program,
                           static_cast<int>(i)});
    std::ranges::sort(presets, [](const auto& a, const auto& b) {
        return std::pair{a.bank, a.program} < std::pair{b.bank, b.program};
    });
    return presets;
}

namespace {

/// One zone a preset plays: its instrument zone's generators and the preset zone's over them.
struct Voice {
    Gens zone;
    Gens preset;
    bool stereo = false;
    magda::ChainZones range;
};

void applyParameters(Builder& builder, const Voice& voice) {
    // The instrument's value; the preset's adds to it, as the specification layers them.
    const auto own = [&voice](int gen) {
        const auto it = voice.zone.find(gen);
        return it != voice.zone.end() ? it->second : defaultOf(gen);
    };
    const auto value = [&own, &voice](int gen) {
        const auto it = voice.preset.find(gen);
        return own(gen) + (it != voice.preset.end() ? it->second : 0);
    };

    builder.parameter(Sampler::kAttack, seconds(value(kVolEnvAttack)), "attack");
    builder.parameter(Sampler::kDecay, seconds(value(kVolEnvDecay)), "decay");
    // Sustain is an attenuation in centibels.
    builder.parameter(Sampler::kSustain, std::pow(10.0, -value(kVolEnvSustain) / 200.0), "sustain");
    builder.parameter(Sampler::kRelease, seconds(value(kVolEnvRelease)), "release");

    // The filter is off at its 13500-cent default; its cutoff counts cents from 8.176 Hz.
    const auto cutoff = value(kFilterFc);
    if (cutoff < 13500) {
        builder.parameter(Sampler::kFilterType, 1, "filter");
        builder.parameter(Sampler::kFilterMode, 0, "filter");
        builder.parameter(Sampler::kFilterPoles, 0, "filter");
        builder.parameter(Sampler::kFilterCutoff, 8.176 * std::exp2(cutoff / 1200.0), "cutoff");
        builder.parameter(Sampler::kFilterResonance, value(kFilterQ) / 10.0 / 24.0, "resonance");
    }
    // One modulation envelope drives pitch and filter; each of the sampler's takes it.
    const auto modEnvelope = [&](int attack, int decay, int sustain, int release) {
        builder.parameter(attack, seconds(value(kModEnvAttack)), "modulation envelope");
        builder.parameter(decay, seconds(value(kModEnvDecay)), "modulation envelope");
        builder.parameter(sustain, 1.0 - value(kModEnvSustain) / 1000.0, "modulation envelope");
        builder.parameter(release, seconds(value(kModEnvRelease)), "modulation envelope");
    };
    if (const auto depth = value(kModEnvToFilterFc); depth != 0 && cutoff < 13500) {
        modEnvelope(Sampler::kFilterEnvAttack, Sampler::kFilterEnvDecay, Sampler::kFilterEnvSustain,
                    Sampler::kFilterEnvRelease);
        builder.parameter(Sampler::kFilterEnvAmount, depth / 100.0, "filter envelope");
    }
    if (const auto depth = value(kModEnvToPitch); depth != 0) {
        modEnvelope(Sampler::kPitchEnvAttack, Sampler::kPitchEnvDecay, Sampler::kPitchEnvSustain,
                    Sampler::kPitchEnvRelease);
        builder.parameter(Sampler::kPitchEnvAmount, depth / 100.0, "pitch envelope");
    }

    if (value(kModLfoToPitch) != 0 || value(kVibLfoToPitch) != 0 || value(kModLfoToFilterFc) != 0 ||
        value(kModLfoToVolume) != 0)
        builder.drop("LFOs");
    if (value(kPan) != 0 && !voice.stereo)
        builder.drop("pan");
    if (value(kVolEnvHold) > -12000 || value(kVolEnvDelay) > -12000 ||
        value(kModEnvHold) > -12000 || value(kModEnvDelay) > -12000)
        builder.drop("envelope delay and hold");
    if (value(kKeyToVolEnvHold) != 0 || value(kKeyToVolEnvDecay) != 0 ||
        value(kKeyToModEnvHold) != 0 || value(kKeyToModEnvDecay) != 0)
        builder.drop("envelopes scaled by key");
    if (own(kExclusiveClass) != 0)
        builder.drop("choke groups");
    if (own(kScaleTuning) != 100)
        builder.drop("scale tuning");
    if (own(kKeynum) >= 0 || own(kVelocity) >= 0)
        builder.drop("fixed key or velocity");
}

}  // namespace

Instrument importSoundFont(const juce::File& file, int preset, const juce::File& sampleFolder) {
    Builder builder;
    const auto font = readSoundFont(file);
    if (!font) {
        builder.out.error = "Not a SoundFont";
        return builder.finish({});
    }
    if (preset < 0 || preset + 1 >= static_cast<int>(font->presets.size())) {
        builder.out.error = "No such preset";
        return builder.finish({});
    }
    if (font->presetModulators > 0 || font->instrumentModulators > 0)
        builder.dropLast("modulators");
    builder.usedZone = "key (middle C's zone's are used)";

    SampleWriter writer(file, *font, sampleFolder);
    std::vector<Voice> voices;
    const auto presetZones = zonesOf(font->presets, static_cast<size_t>(preset), font->presetBags,
                                     font->presetGens, kInstrument);
    for (const auto& presetZone : presetZones) {
        const auto instrument = presetZone.at(kInstrument);
        if (instrument < 0 || instrument + 1 >= static_cast<int>(font->instruments.size()))
            continue;
        const auto [presetKeyLow, presetKeyHigh] = rangeOf(presetZone, kKeyRange);
        const auto [presetVelLow, presetVelHigh] = rangeOf(presetZone, kVelRange);

        for (const auto& zone : zonesOf(font->instruments, static_cast<size_t>(instrument),
                                        font->instrumentBags, font->instrumentGens, kSampleId)) {
            const auto sampleIndex = zone.at(kSampleId);
            if (sampleIndex < 0 || sampleIndex >= static_cast<int>(font->samples.size()))
                continue;
            const auto& sample = font->samples[static_cast<size_t>(sampleIndex)];
            if ((sample.type & kRom) != 0) {
                builder.drop("ROM samples");
                continue;
            }
            // The instrument's value; the preset's adds to it, as the specification layers them.
            const auto own = [&zone](int gen) {
                const auto it = zone.find(gen);
                return it != zone.end() ? it->second : defaultOf(gen);
            };
            const auto value = [&own, &presetZone](int gen) {
                const auto it = presetZone.find(gen);
                return own(gen) + (it != presetZone.end() ? it->second : 0);
            };

            // A linked pair is one stereo zone: the left sample's, its partner's skipped. Each
            // must name the other, as some files link a lone sample to sample 0.
            const auto partnerOf = [&font](int index, int type) -> std::optional<int> {
                const auto& own = font->samples[static_cast<size_t>(index)];
                if ((own.type & type) == 0 || own.link < 0 || own.link == index ||
                    own.link >= static_cast<int>(font->samples.size()))
                    return std::nullopt;
                const auto& other = font->samples[static_cast<size_t>(own.link)];
                const auto otherType = type == kLeft ? kRight : kLeft;
                if ((other.type & otherType) == 0 || other.link != index)
                    return std::nullopt;
                return own.link;
            };
            if (partnerOf(sampleIndex, kRight))
                continue;
            const auto right = partnerOf(sampleIndex, kLeft);

            const auto [keyLow, keyHigh] = rangeOf(zone, kKeyRange);
            const auto [velLow, velHigh] = rangeOf(zone, kVelRange);
            Region region;
            region.file = writer.write(sampleIndex, right);
            if (region.file == juce::File())
                region.file = sampleFolder.getChildFile("unreadable");
            region.zones.keyLow = std::max(keyLow, presetKeyLow);
            region.zones.keyHigh = std::min(keyHigh, presetKeyHigh);
            region.zones.velocityLow = std::max({1, velLow, presetVelLow});
            region.zones.velocityHigh = std::min(velHigh, presetVelHigh);
            if (region.zones.keyLow > region.zones.keyHigh ||
                region.zones.velocityLow > region.zones.velocityHigh)
                continue;

            region.root = own(kRootKey) >= 0            ? own(kRootKey)
                          : sample.originalPitch <= 127 ? sample.originalPitch
                                                        : 60;
            region.tuneCents =
                static_cast<float>(100 * value(kCoarseTune) + value(kFineTune) + sample.correction);
            // Scaled by 0.4 as the E-mu hardware and FluidSynth do, which General MIDI banks are
            // voiced for; the specification's full centibels leave them 10-20 dB quiet.
            region.gainDb = -0.4f * static_cast<float>(value(kAttenuation)) / 10.0f;

            // Compressed samples count their loop from their own first frame.
            const juce::int64 base = (sample.type & kCompressed) != 0 || font->compressed
                                         ? 0
                                         : static_cast<juce::int64>(sample.start);
            region.offset = own(kStartOffset) + 32768 * own(kStartCoarse);
            if (const auto endOffset = own(kEndOffset) + 32768 * own(kEndCoarse); endOffset != 0)
                region.end = static_cast<juce::int64>(sample.end - sample.start) + endOffset;
            const auto mode = own(kSampleModes) & 3;
            region.loop = mode == 1 || mode == 3;
            if (mode == 3)
                builder.drop("loop until release (loops through the release)");
            region.loopStart = static_cast<juce::int64>(sample.loopStart) - base +
                               own(kLoopStartOffset) + 32768 * own(kLoopStartCoarse);
            region.loopEnd = static_cast<juce::int64>(sample.loopEnd) - base + own(kLoopEndOffset) +
                             32768 * own(kLoopEndCoarse);
            builder.add(region);
            voices.push_back({zone, presetZone, right.has_value(), region.zones});
        }
    }

    // Device-wide values from the zone a player hears most, middle C's at full velocity, then
    // each other zone's for the report.
    const auto heard = std::ranges::max_element(voices, {}, [](const Voice& voice) {
        const auto& zones = voice.range;
        const bool middle = zones.keyLow <= 60 && zones.keyHigh >= 60;
        return std::pair{middle, middle ? zones.velocityHigh : -std::abs(zones.keyLow - 60)};
    });
    if (heard != voices.end())
        applyParameters(builder, *heard);
    for (const auto& voice : voices)
        applyParameters(builder, voice);
    return builder.finish("The preset plays no samples");
}

}  // namespace magda::sampler_import
