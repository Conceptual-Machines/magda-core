// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sampling/SamplerImport.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <string>

#include "sampling/SamplerImportBuilder.hpp"

namespace magda::sampler_import {

namespace {

using namespace detail;

/// Digits as N, so `amp_velcurve_64` and `amp_velcurve_127` report once.
juce::String kindOf(const juce::String& name) {
    juce::String kind;
    for (int i = 0; i < name.length(); ++i) {
        if (juce::CharacterFunctions::isDigit(name[i])) {
            if (!kind.endsWithChar('N'))
                kind << "N";
        } else {
            kind << juce::String::charToString(name[i]);
        }
    }
    return kind;
}

/// How an opcode the sampler cannot play is reported, families as one.
juce::String reportedAs(const juce::String& name) {
    const auto kind = kindOf(name);
    if (kind.contains("ccN"))
        return "MIDI CC modulation";
    if (kind.contains("lfo"))
        return "LFOs";
    if (kind.startsWith("bend_"))
        return "pitch bend range";
    if (kind == "group" || kind == "off_by" || kind == "off_mode")
        return "choke groups";
    if (kind.startsWith("amp_velcurve"))
        return "velocity curves";
    return "opcode " + kind;
}

//==============================================================================
// SFZ

using Opcodes = std::map<juce::String, juce::String>;

// The scanning is over bytes: SFZ's syntax is ASCII, and juce::String indexes UTF-8 from its
// start, which made a large instrument quadratic.
using Defines = std::map<std::string, std::string>;

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// One pass, so `//****` is a line comment rather than a block's start.
std::string stripComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t from = 0;
    for (size_t i = 0; i + 1 < text.size();) {
        if (text[i] == '/' && (text[i + 1] == '/' || text[i + 1] == '*')) {
            const bool line = text[i + 1] == '/';
            out.append(text, from, i - from).push_back(' ');
            const auto close = line ? text.find('\n', i) : text.find("*/", i + 2);
            i = from = close == std::string::npos ? text.size() : close + (line ? 0 : 2);
            continue;
        }
        ++i;
    }
    return out.append(text, from);
}

/// The word at @p at, past spaces and tabs; @p at ends after it.
std::string nextWord(const std::string& text, size_t& at) {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
        ++at;
    const auto start = at;
    while (at < text.size() && !isBlank(text[at]))
        ++at;
    return text.substr(start, at - start);
}

std::string substitute(std::string text, const Defines& defines) {
    if (text.find('$') == std::string::npos)
        return text;
    // Longest first, so $NOTE is not read as $NOTE_LOW's prefix.
    std::vector<std::pair<std::string, std::string>> sorted(defines.begin(), defines.end());
    std::ranges::sort(sorted,
                      [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    for (const auto& [name, value] : sorted)
        for (auto at = text.find(name); !name.empty() && at != std::string::npos;
             at = text.find(name, at + value.size()))
            text.replace(at, name.size(), value);
    return text;
}

/// The file's text with comments gone, #define applied and #include inlined, wherever on a line
/// they sit. Included paths are relative to the instrument's folder, as SFZ players read them.
std::string expand(const juce::File& file, const juce::File& folder, Defines& defines, int depth,
                   Builder& builder) {
    const auto text = stripComments(file.loadFileAsString().toStdString());
    std::string out;
    size_t from = 0;
    for (auto at = text.find('#'); at != std::string::npos; at = text.find('#', from)) {
        const bool define = text.compare(at, 7, "#define") == 0;
        const bool include = text.compare(at, 8, "#include") == 0;
        out += substitute(text.substr(from, (define || include ? at : at + 1) - from), defines);
        from = at + 1;
        if (define) {
            from = at + 7;
            const auto name = nextWord(text, from);
            defines[name] = nextWord(text, from);
        } else if (include) {
            const auto open = text.find('"', at);
            const auto close = open == std::string::npos ? open : text.find('"', open + 1);
            if (close == std::string::npos)
                continue;
            from = close + 1;
            const auto path = juce::String::fromUTF8(
                substitute(text.substr(open + 1, close - open - 1), defines).c_str());
            const auto included = folder.getChildFile(path.replaceCharacter('\\', '/'));
            if (depth < 8 && included.existsAsFile())
                out += "\n" + expand(included, folder, defines, depth + 1, builder) + "\n";
            else
                builder.drop("#include \"" + path + "\" (not found)");
        }
    }
    return out + substitute(text.substr(from), defines);
}

bool opcodeStartsAt(const std::string& text, size_t i) {
    auto j = i;
    while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_'))
        ++j;
    return j > i && j < text.size() && text[j] == '=';
}

struct Token {
    bool header = false;
    juce::String name, value;
};

juce::String utf8(const std::string& text, size_t from, size_t to) {
    return juce::String::fromUTF8(text.data() + from, static_cast<int>(to - from)).trim();
}

/// Headers and opcodes. A value runs to the next opcode, header or line end, so a sample path
/// may hold spaces.
std::vector<Token> tokenize(const std::string& text) {
    std::vector<Token> tokens;
    const auto n = text.size();
    size_t i = 0;
    while (i < n) {
        if (isBlank(text[i])) {
            ++i;
            continue;
        }
        if (text[i] == '<') {
            const auto close = text.find('>', i);
            if (close == std::string::npos)
                break;
            tokens.push_back({true, utf8(text, i + 1, close).toLowerCase(), {}});
            i = close + 1;
            continue;
        }
        if (!opcodeStartsAt(text, i)) {
            while (i < n && !isBlank(text[i]) && text[i] != '<')
                ++i;
            continue;
        }
        const auto equals = text.find('=', i);
        auto end = equals + 1;
        while (end < n && text[end] != '\n' && text[end] != '\r' && text[end] != '<') {
            if (text[end] == ' ' || text[end] == '\t') {
                auto next = end;
                while (next < n && (text[next] == ' ' || text[next] == '\t'))
                    ++next;
                if (opcodeStartsAt(text, next))
                    break;
                end = next;
                continue;
            }
            ++end;
        }
        tokens.push_back({false, utf8(text, i, equals).toLowerCase(), utf8(text, equals + 1, end)});
        i = end;
    }
    return tokens;
}

const std::set<juce::String>& sfzOpcodesRead() {
    static const std::set<juce::String> known{"sample",
                                              "key",
                                              "lokey",
                                              "hikey",
                                              "pitch_keycenter",
                                              "lovel",
                                              "hivel",
                                              "xfin_lokey",
                                              "xfin_hikey",
                                              "xfout_lokey",
                                              "xfout_hikey",
                                              "xfin_lovel",
                                              "xfin_hivel",
                                              "xfout_lovel",
                                              "xfout_hivel",
                                              "xf_keycurve",
                                              "xf_velcurve",
                                              "tune",
                                              "transpose",
                                              "volume",
                                              "amplitude",
                                              "offset",
                                              "end",
                                              "loop_mode",
                                              "loopmode",
                                              "loop_start",
                                              "loopstart",
                                              "loop_end",
                                              "loopend",
                                              "seq_length",
                                              "seq_position",
                                              "lorand",
                                              "hirand",
                                              "trigger",
                                              "ampeg_attack",
                                              "ampeg_decay",
                                              "ampeg_sustain",
                                              "ampeg_release",
                                              "amp_veltrack",
                                              "pitcheg_attack",
                                              "pitcheg_decay",
                                              "pitcheg_sustain",
                                              "pitcheg_release",
                                              "pitcheg_depth",
                                              "fil_type",
                                              "filtype",
                                              "cutoff",
                                              "resonance",
                                              "fil_keytrack",
                                              "fil_veltrack",
                                              "fileg_attack",
                                              "fileg_decay",
                                              "fileg_sustain",
                                              "fileg_release",
                                              "fileg_depth",
                                              "default_path",
                                              "note_offset",
                                              "octave_offset",
                                              "global_volume",
                                              "master_volume",
                                              "group_volume",
                                              "loprog",
                                              "hiprog"};
    return known;
}

/// An SFZ filter type as the sampler's type, mode and poles; nullopt for one it lacks.
std::optional<std::array<int, 3>> sfzFilter(const juce::String& type) {
    constexpr int kSvf = 1;
    const auto poles = type.endsWith("_4p") ? 1 : type.endsWith("_6p") ? 2 : 0;
    if (type.startsWith("lpf_"))
        return std::array{kSvf, 0, poles};
    if (type.startsWith("bpf_"))
        return std::array{kSvf, 1, poles};
    if (type.startsWith("hpf_"))
        return std::array{kSvf, 2, poles};
    if (type.startsWith("brf_"))
        return std::array{kSvf, 3, poles};
    return std::nullopt;
}

/// Whether a region plays as the instrument loads: each CC at its set_cc value (else 0), MIDI
/// program 0 and the default keyswitch. Layers a player would switch to are left out, as
/// stacking them all would play every articulation at once.
bool activeAtLoad(Builder& builder, const Opcodes& control, const Opcodes& op) {
    const auto setting = [&control, &op](const juce::String& name) -> std::optional<double> {
        for (const auto* scope : {&op, &control})
            if (const auto it = scope->find(name); it != scope->end())
                return it->second.getDoubleValue();
        return std::nullopt;
    };
    for (const auto& [name, value] : op) {
        const auto number = name.substring(4);
        if (!(name.startsWith("locc") || name.startsWith("hicc")) ||
            !number.containsOnly("0123456789"))
            continue;
        const auto cc =
            setting("set_cc" + number).value_or(127.0 * setting("set_hdcc" + number).value_or(0.0));
        if (name.startsWith("locc") ? cc < value.getDoubleValue() : cc > value.getDoubleValue()) {
            builder.drop("layers switched by MIDI CC (the defaults play)");
            return false;
        }
    }

    constexpr double kProgram = 0.0;
    if (kProgram < setting("loprog").value_or(0.0) ||
        kProgram > setting("hiprog").value_or(127.0)) {
        builder.drop("layers on other MIDI programs");
        return false;
    }

    if (setting("sw_down") || setting("sw_up") || setting("sw_previous")) {
        builder.drop("layers played while a key is held");
        return false;
    }
    const auto low = setting("sw_lolast") ? setting("sw_lolast") : setting("sw_last");
    const auto high = setting("sw_hilast") ? setting("sw_hilast") : setting("sw_last");
    if (low || high) {
        builder.drop("keyswitched articulations (the default plays)");
        if (!builder.keyswitch)
            builder.keyswitch = setting("sw_default").value_or(low ? *low : *high);
        if (*builder.keyswitch < low.value_or(0.0) || *builder.keyswitch > high.value_or(127.0))
            return false;
    }
    return true;
}

void addSfzRegion(Builder& builder, const juce::File& sfz, const Opcodes& control,
                  const Opcodes& op) {
    const auto get = [&op](const char* name) -> std::optional<juce::String> {
        if (const auto it = op.find(name); it != op.end())
            return it->second;
        return std::nullopt;
    };
    const auto number = [&get](const char* name) -> std::optional<double> {
        if (const auto value = get(name))
            return value->getDoubleValue();
        return std::nullopt;
    };
    const auto note = [&get](const char* name) -> std::optional<int> {
        if (const auto value = get(name))
            return sfzNote(*value);
        return std::nullopt;
    };

    for (const auto& [name, value] : op)
        if (!sfzOpcodesRead().contains(name) && !name.endsWith("_label") && value != "0" &&
            !name.startsWith("label_cc") && !name.startsWith("locc") && !name.startsWith("hicc") &&
            !name.startsWith("sw_") && !name.startsWith("set_"))
            builder.drop(reportedAs(name));
    if (!activeAtLoad(builder, control, op))
        return;

    if (const auto trigger = get("trigger").value_or("attack"); trigger != "attack") {
        builder.drop("regions with trigger=" + trigger);
        return;
    }
    const auto sample = get("sample");
    if (!sample)
        return;
    if (sample->startsWith("*")) {
        builder.drop("generated waveforms (" + *sample + ")");
        return;
    }

    const auto controlValue = [&control](const char* name) {
        const auto it = control.find(name);
        return it != control.end() ? it->second : juce::String();
    };
    const int shift = controlValue("note_offset").getIntValue() +
                      12 * controlValue("octave_offset").getIntValue();
    const auto path = (controlValue("default_path") + *sample).replaceCharacter('\\', '/');

    Region region;
    region.file = sfz.getParentDirectory().getChildFile(path);
    region.root = 60;
    auto& zones = region.zones;
    if (const auto key = note("key")) {
        zones.keyLow = zones.keyHigh = *key;
        region.root = *key;
    }
    zones.keyLow = note("lokey").value_or(zones.keyLow);
    zones.keyHigh = note("hikey").value_or(zones.keyHigh);
    if (number("hikey").value_or(0.0) < 0.0)
        return;  // A key switch's region, no key of its own.
    if (const auto center = get("pitch_keycenter"))
        region.root = *center == "sample" ? std::nullopt : sfzNote(*center);
    zones.keyLow += shift;
    zones.keyHigh += shift;
    if (region.root)
        *region.root += shift;
    zones.velocityLow = static_cast<int>(number("lovel").value_or(1));
    zones.velocityHigh = static_cast<int>(number("hivel").value_or(127));

    // A crossfade reaches full level at xfin_hi and leaves it at xfout_lo.
    if (const auto in = note("xfin_hikey"))
        zones.keyFadeLow = std::max(0, *in + shift - zones.keyLow);
    if (const auto out = note("xfout_lokey"))
        zones.keyFadeHigh = std::max(0, zones.keyHigh - (*out + shift));
    if (const auto in = number("xfin_hivel"))
        zones.velocityFadeLow = std::max(0, static_cast<int>(*in) - zones.velocityLow);
    if (const auto out = number("xfout_lovel"))
        zones.velocityFadeHigh = std::max(0, zones.velocityHigh - static_cast<int>(*out));

    region.tuneCents = static_cast<float>(number("tune").value_or(0.0) +
                                          100.0 * number("transpose").value_or(0.0));
    // SFZ v2's level per header adds to the region's.
    region.gainDb = static_cast<float>(
        number("volume").value_or(0.0) + number("global_volume").value_or(0.0) +
        number("master_volume").value_or(0.0) + number("group_volume").value_or(0.0));
    if (const auto amplitude = number("amplitude"))
        region.gainDb += decibels(*amplitude / 100.0);

    region.offset = static_cast<juce::int64>(number("offset").value_or(0.0));
    if (const auto end = number("end"); end && *end > 0.0)
        region.end = static_cast<juce::int64>(*end) + 1;
    if (const auto mode = get("loop_mode") ? get("loop_mode") : get("loopmode")) {
        region.loop = *mode == "loop_continuous" || *mode == "loop_sustain";
        if (*mode == "one_shot")
            builder.drop("one_shot (stops at note-off)");
        else if (*mode == "loop_sustain")
            builder.drop("loop_sustain (loops through the release)");
    }
    if (const auto start = number("loop_start") ? number("loop_start") : number("loopstart"))
        region.loopStart = static_cast<juce::int64>(*start);
    if (const auto end = number("loop_end") ? number("loop_end") : number("loopend"))
        region.loopEnd = static_cast<juce::int64>(*end) + 1;

    zones.roundRobin = number("seq_length").value_or(1.0) > 1.0;
    if (number("lorand").value_or(0.0) > 0.0 || number("hirand").value_or(1.0) < 1.0) {
        zones.roundRobin = true;
        builder.drop("random round robin (plays in turn)");
    }

    const auto param = [&builder, &number](const char* name, int index, double scale = 1.0) {
        if (const auto value = number(name))
            builder.parameter(index, *value * scale, name);
    };
    param("ampeg_attack", Sampler::kAttack);
    param("ampeg_decay", Sampler::kDecay);
    param("ampeg_sustain", Sampler::kSustain, 0.01);
    param("ampeg_release", Sampler::kRelease);
    param("amp_veltrack", Sampler::kVelAmount, 0.01);
    param("pitcheg_attack", Sampler::kPitchEnvAttack);
    param("pitcheg_decay", Sampler::kPitchEnvDecay);
    param("pitcheg_sustain", Sampler::kPitchEnvSustain, 0.01);
    param("pitcheg_release", Sampler::kPitchEnvRelease);
    param("pitcheg_depth", Sampler::kPitchEnvAmount, 0.01);

    // SFZ has no filter until a cutoff is set.
    if (number("cutoff")) {
        const auto type = get("fil_type").value_or(get("filtype").value_or("lpf_2p"));
        if (const auto filter = sfzFilter(type)) {
            builder.parameter(Sampler::kFilterType, (*filter)[0], "fil_type");
            builder.parameter(Sampler::kFilterMode, (*filter)[1], "fil_type");
            builder.parameter(Sampler::kFilterPoles, (*filter)[2], "fil_type");
            param("cutoff", Sampler::kFilterCutoff);
            // Resonance in dB, its control's top taken as 24 dB.
            param("resonance", Sampler::kFilterResonance, 1.0 / 24.0);
            param("fil_keytrack", Sampler::kFilterKeyTrack, 0.01);
            param("fil_veltrack", Sampler::kFilterVelocity,
                  0.01 / static_cast<double>(daw::audio::SamplerVoice::kVelocityRangeSemitones));
            param("fileg_attack", Sampler::kFilterEnvAttack);
            param("fileg_decay", Sampler::kFilterEnvDecay);
            param("fileg_sustain", Sampler::kFilterEnvSustain, 0.01);
            param("fileg_release", Sampler::kFilterEnvRelease);
            param("fileg_depth", Sampler::kFilterEnvAmount, 0.01);
        } else {
            builder.drop("fil_type=" + type);
        }
    }

    builder.add(region);
}

//==============================================================================
// DecentSampler

const std::set<juce::String>& decentAttributesRead() {
    static const std::set<juce::String> known{"path",
                                              "rootNote",
                                              "loNote",
                                              "hiNote",
                                              "loVel",
                                              "hiVel",
                                              "tuning",
                                              "volume",
                                              "start",
                                              "end",
                                              "loopEnabled",
                                              "loopStart",
                                              "loopEnd",
                                              "seqMode",
                                              "seqPosition",
                                              "seqLength",
                                              "trigger",
                                              "attack",
                                              "decay",
                                              "sustain",
                                              "release",
                                              "ampVelTrack",
                                              "tags",
                                              "name",
                                              "enabled",
                                              "attackCurve",
                                              "decayCurve",
                                              "releaseCurve",
                                              "loopCrossfadeMode",
                                              "silencedByTags",
                                              "silencingMode"};
    return known;
}

void addDecentEffects(Builder& builder, const juce::XmlElement* effects) {
    if (effects == nullptr)
        return;
    for (const auto* effect : effects->getChildWithTagNameIterator("effect")) {
        const auto type = effect->getStringAttribute("type");
        const auto mode = type == "lowpass" || type == "lowpass_4pl" ? 0
                          : type == "bandpass"                       ? 1
                          : type == "highpass"                       ? 2
                          : type == "notch"                          ? 3
                                                                     : -1;
        if (mode < 0) {
            builder.drop("effect " + type);
            continue;
        }
        builder.parameter(Sampler::kFilterType, 1, "filter type");
        builder.parameter(Sampler::kFilterMode, mode, "filter type");
        builder.parameter(Sampler::kFilterPoles, type == "lowpass_4pl" ? 1 : 0, "filter type");
        if (effect->hasAttribute("frequency"))
            builder.parameter(Sampler::kFilterCutoff, effect->getDoubleAttribute("frequency"),
                              "filter frequency");
        // Its Q, 0.7 flat to 5, onto the resonance control.
        if (effect->hasAttribute("resonance"))
            builder.parameter(Sampler::kFilterResonance,
                              (effect->getDoubleAttribute("resonance") - 0.7) / 4.3,
                              "filter resonance");
    }
}

void addDecentSample(Builder& builder, const juce::File& folder,
                     std::initializer_list<const juce::XmlElement*> scopes) {
    const auto get = [&scopes](const char* name) -> std::optional<juce::String> {
        for (const auto* scope : scopes)
            if (scope != nullptr && scope->hasAttribute(name))
                return scope->getStringAttribute(name);
        return std::nullopt;
    };
    const auto number = [&get](const char* name) -> std::optional<double> {
        if (const auto value = get(name))
            return value->getDoubleValue();
        return std::nullopt;
    };

    for (const auto* scope : scopes)
        if (scope != nullptr)
            for (int i = 0; i < scope->getNumAttributes(); ++i)
                if (const auto name = scope->getAttributeName(i);
                    !decentAttributesRead().contains(name) && scope->getAttributeValue(i) != "0")
                    builder.drop("attribute " + name);

    if (get("silencedByTags"))
        builder.drop("choke groups");
    if (const auto trigger = get("trigger").value_or("attack"); trigger != "attack") {
        builder.drop("samples with trigger=" + trigger);
        return;
    }
    const auto path = get("path");
    if (!path)
        return;

    Region region;
    region.file = folder.getChildFile(path->replaceCharacter('\\', '/'));
    if (const auto root = get("rootNote"))
        region.root = sfzNote(*root);
    auto& zones = region.zones;
    if (const auto low = get("loNote"))
        zones.keyLow = sfzNote(*low).value_or(0);
    if (const auto high = get("hiNote"))
        zones.keyHigh = sfzNote(*high).value_or(127);
    zones.velocityLow = static_cast<int>(number("loVel").value_or(1));
    zones.velocityHigh = static_cast<int>(number("hiVel").value_or(127));
    region.tuneCents = static_cast<float>(100.0 * number("tuning").value_or(0.0));
    if (const auto volume = get("volume"))
        region.gainDb = volume->endsWithIgnoreCase("db")
                            ? static_cast<float>(volume->getDoubleValue())
                            : decibels(volume->getDoubleValue());
    region.offset = static_cast<juce::int64>(number("start").value_or(0.0));
    if (const auto end = number("end"); end && *end > 0.0)
        region.end = static_cast<juce::int64>(*end);
    region.loop = get("loopEnabled").value_or("false") == "true";
    if (const auto start = number("loopStart"))
        region.loopStart = static_cast<juce::int64>(*start);
    if (const auto end = number("loopEnd"))
        region.loopEnd = static_cast<juce::int64>(*end);

    const auto sequence = get("seqMode").value_or("always");
    zones.roundRobin = sequence != "always";
    if (sequence == "random" || sequence == "true_random")
        builder.drop("random round robin (plays in turn)");

    const auto param = [&builder, &number](const char* name, int index) {
        if (const auto value = number(name))
            builder.parameter(index, *value, name);
    };
    param("attack", Sampler::kAttack);
    param("decay", Sampler::kDecay);
    param("sustain", Sampler::kSustain);
    param("release", Sampler::kRelease);
    param("ampVelTrack", Sampler::kVelAmount);

    builder.add(region);
}

}  // namespace

std::optional<int> sfzNote(const juce::String& text) {
    const auto name = text.trim().toLowerCase();
    if (name.isEmpty())
        return std::nullopt;
    std::optional<int> note;
    if (name.containsOnly("-0123456789")) {
        note = name.getIntValue();
    } else {
        constexpr int kSemitones[] = {9, 11, 0, 2, 4, 5, 7};  // a b c d e f g
        const auto letter = name[0];
        if (letter < 'a' || letter > 'g')
            return std::nullopt;
        int semitone = kSemitones[letter - 'a'];
        int at = 1;
        if (name[at] == '#')
            ++semitone, ++at;
        else if (name[at] == 'b')
            --semitone, ++at;
        const auto octave = name.substring(at);
        if (octave.isEmpty() || !octave.containsOnly("-0123456789"))
            return std::nullopt;
        note = (octave.getIntValue() + 1) * 12 + semitone;
    }
    if (*note < 0 || *note > 127)
        return std::nullopt;
    return note;
}

bool isInstrumentFile(const juce::File& file) {
    return file.hasFileExtension("sfz;dspreset") || isSoundFont(file);
}

bool isSoundFont(const juce::File& file) {
    return file.hasFileExtension("sf2;sf3");
}

Instrument importSfz(const juce::File& file) {
    Builder builder;
    if (!file.existsAsFile()) {
        builder.out.error = "File not found";
        return builder.finish({});
    }
    Defines defines;
    const auto text = expand(file, file.getParentDirectory(), defines, 0, builder);

    enum class Level { Global, Region, Other };
    auto level = Level::Global;
    Opcodes control, global, master, group, region;
    const auto flush = [&] {
        if (level == Level::Region) {
            // Each level overrides the one above it.
            Opcodes merged = global;
            for (const auto* scope : {&master, &group, &region})
                for (const auto& [name, value] : *scope)
                    merged[name] = value;
            addSfzRegion(builder, file, control, merged);
        }
        region.clear();
    };
    Opcodes* target = &global;
    for (const auto& token : tokenize(text)) {
        if (token.header) {
            flush();
            level = Level::Global;
            if (token.name == "control") {
                target = &control;
            } else if (token.name == "global") {
                global.clear(), master.clear(), group.clear();
                target = &global;
            } else if (token.name == "master") {
                master.clear(), group.clear();
                target = &master;
            } else if (token.name == "group") {
                group.clear();
                target = &group;
            } else if (token.name == "region") {
                level = Level::Region;
                target = &region;
            } else {
                level = Level::Other;
                target = nullptr;
                builder.drop("<" + token.name + "> sections");
            }
            continue;
        }
        if (target != nullptr)
            (*target)[token.name] = token.value;
    }
    flush();
    return builder.finish("No region plays a sample");
}

Instrument importDecentSampler(const juce::File& file) {
    Builder builder;
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("DecentSampler")) {
        builder.out.error = "Not a DecentSampler preset";
        return builder.finish({});
    }
    const auto* groups = xml->getChildByName("groups");
    for (const auto* child : xml->getChildIterator())
        if (const auto tag = child->getTagName();
            tag != "groups" && tag != "effects" && tag != "ui" && tag != "tags")
            builder.drop("<" + tag + ">");

    addDecentEffects(builder, xml->getChildByName("effects"));
    if (groups != nullptr)
        for (const auto* group : groups->getChildWithTagNameIterator("group")) {
            if (group->getStringAttribute("enabled", "true") == "false")
                continue;
            for (const auto* sample : group->getChildWithTagNameIterator("sample"))
                addDecentSample(builder, file.getParentDirectory(), {sample, group, groups});
            addDecentEffects(builder, group->getChildByName("effects"));
        }
    return builder.finish("The preset plays no samples");
}

Instrument importInstrument(const juce::File& file) {
    return file.hasFileExtension("dspreset") ? importDecentSampler(file) : importSfz(file);
}

}  // namespace magda::sampler_import
