#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <iostream>

#include "magda/agents/mixing_agent.hpp"
#include "magda/daw/audio/analysis/TrackMeasurer.hpp"
#include "magda/daw/core/Config.hpp"

using namespace magda;
using daw::audio::TrackMeasurementSnapshot;
using daw::audio::TrackMeasurer;

// ============================================================================
// Real-audio mix-analysis harness (#886, exploratory).
//
// Measures every stem in the (gitignored) fixtures dir offline with the exact
// production DSP (TrackMeasurer), sums a normalised master mixdown, and runs the
// MixAnalysisAgent against the configured LLM. Hidden [.] test -- needs the
// stems + an API key, so it never runs in CI. Run it with:
//   ./cmake-build-debug/tests/magda_tests "[mix_analysis][audio]"
// API keys are read from <repo>/.env (OPENAI_API_KEY / ANTHROPIC_API_KEY / ...).
// Optionally override the model: MIX_ANALYSIS_PROVIDER + MIX_ANALYSIS_MODEL.
// ============================================================================

namespace {

constexpr int kBlock = 8192;

// Load <repo>/.env into the process environment (without clobbering anything
// already set), so the agent's env-var key fallback picks the keys up.
void loadDotEnv(const juce::File& envFile) {
    if (!envFile.existsAsFile())
        return;
    for (const auto& raw : juce::StringArray::fromLines(envFile.loadFileAsString())) {
        auto line = raw.trim();
        if (line.isEmpty() || line.startsWithChar('#'))
            continue;
        const int eq = line.indexOfChar('=');
        if (eq <= 0)
            continue;
        auto key = line.substring(0, eq).trim();
        auto val = line.substring(eq + 1).trim();
        if (val.startsWithChar('"') && val.endsWithChar('"'))
            val = val.substring(1, val.length() - 1);
        if (val.isNotEmpty())
            ::setenv(key.toRawUTF8(), val.toRawUTF8(), /*overwrite*/ 0);
    }
}

// "DRUM KICK - SENN 421 {..}" -> "DRUM KICK"; "BASS DI" -> "BASS DI".
juce::String cleanName(juce::String stem) {
    const int dash = stem.indexOf(" - ");
    if (dash > 0)
        stem = stem.substring(0, dash);
    return stem.trim();
}

std::string inferRole(const juce::String& name) {
    auto u = name.toUpperCase();
    auto has = [&u](const char* s) { return u.contains(s); };
    if (has("KICK"))
        return "kick";
    if (has("SNARE"))
        return "snare";
    if (has("HIHAT") || has("HI-HAT") || has("HAT"))
        return "hats";
    if (has("OVERHEAD"))
        return "oh";
    if (has("ROOM"))
        return "room";
    if (has("TOM"))
        return "tom";
    if (has("BASS"))
        return "bass";
    if (has("GUITAR") || has("GTR"))
        return "guitar";
    if (has("LESLIE") || has("ORGAN") || has("KEY"))
        return "keys";
    if (has("TRUMPET") || has("HORN") || has("SAX"))
        return "horn";
    if (has("CHAMBER") || has("REVERB"))
        return "fx";
    if (has("VOX") || has("VOCAL") || has("BARITONE") || has("TENOR"))
        return "vocal";
    return "";
}

// One loaded source: 1 channel (mono stem) or 2 (an .L/.R pair).
struct LoadedTrack {
    juce::String name;
    std::string role;
    juce::AudioBuffer<float> buf;
};

bool readWav(juce::AudioFormatManager& fm, const juce::File& f, juce::AudioBuffer<float>& out,
             double& srOut) {
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f.createInputStream()));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    srOut = reader->sampleRate;
    out.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples),
                false, true, false);
    reader->read(&out, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    return true;
}

TrackMeasurementSnapshot measure(const juce::AudioBuffer<float>& buf, double sr) {
    TrackMeasurer m;
    m.prepare(sr, kBlock, /*enableTruePeak*/ true);
    const int len = buf.getNumSamples();
    const int nch = juce::jmin(2, buf.getNumChannels());
    for (int pos = 0; pos < len; pos += kBlock) {
        const int n = juce::jmin(kBlock, len - pos);
        const float* chans[2];
        chans[0] = buf.getReadPointer(0) + pos;
        chans[1] = nch > 1 ? buf.getReadPointer(1) + pos : chans[0];
        m.process(chans, nch, n);
    }
    return m.read();
}

MixAnalysisAgent::TrackMix toTrackMix(const juce::String& name, const std::string& role,
                                      const TrackMeasurementSnapshot& s) {
    MixAnalysisAgent::TrackMix t;
    t.name = name.toStdString();
    t.role = role;
    t.integratedLufs = s.integratedLufs;
    t.shortTermLufs = s.shortTermLufs;
    t.samplePeakDb = s.truePeakValid && s.truePeakDb > -200.0f ? s.truePeakDb : s.samplePeakDb;
    t.plr = s.plr;
    t.psr = s.psr;
    t.correlation = s.correlation;
    t.width = s.width;
    return t;
}

}  // namespace

TEST_CASE("MixAnalysisAgent: analyse a real multitrack session", "[.][mix_analysis][audio]") {
    juce::File dir(juce::String(MAGDA_AUDIO_FIXTURES_DIR) + "/turkuaz");
    if (!dir.isDirectory()) {
        WARN("Audio fixtures not found at " << dir.getFullPathName()
                                            << " -- copy stems there to run this.");
        return;
    }

    juce::Array<juce::File> wavs;
    dir.findChildFiles(wavs, juce::File::findFiles, false, "*.wav");
    if (wavs.isEmpty()) {
        WARN("No .wav stems in " << dir.getFullPathName());
        return;
    }

    loadDotEnv(juce::File(juce::String(MAGDA_REPO_ROOT) + "/.env"));

    // Pick the provider/model for the COMMAND role the agent uses. Start from
    // whatever the app config resolved, let env override it, and fall back to a
    // sane default so the call always has a model. Set MIX_ANALYSIS_PROVIDER /
    // MIX_ANALYSIS_MODEL in .env to target a specific (e.g. stronger) model.
    {
        auto cfg = Config::getInstance().getAgentLLMConfig("command");
        if (const char* prov = std::getenv("MIX_ANALYSIS_PROVIDER"))
            cfg.provider = prov;
        if (const char* model = std::getenv("MIX_ANALYSIS_MODEL"))
            cfg.model = model;
        if (cfg.model.empty()) {
            cfg.provider = "openai_responses";
            cfg.model = "gpt-5";
        }
        // GPT-5 only runs on the Responses API; force it if a gpt-5* model was
        // selected without the matching provider.
        if (juce::String(cfg.model).startsWithIgnoreCase("gpt-5"))
            cfg.provider = "openai_responses";
        Config::getInstance().setAgentLLMConfig("command", cfg);
        std::cout << "[audio] LLM provider=" << cfg.provider << " model=" << cfg.model << "\n";
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    // Pair ".L"/".R" stems into one stereo source; everything else is mono.
    juce::StringArray consumed;
    std::vector<LoadedTrack> tracks;
    double sr = 0.0;
    int maxLen = 0;

    auto stemOf = [](const juce::File& f) { return f.getFileNameWithoutExtension(); };

    for (const auto& f : wavs) {
        if (consumed.contains(f.getFullPathName()))
            continue;
        auto stem = stemOf(f);

        LoadedTrack lt;
        if (stem.endsWithIgnoreCase(".L")) {
            // find the matching .R
            auto base = stem.dropLastCharacters(2);  // strip ".L"
            juce::File rFile = f.getSiblingFile(base + ".R.wav");
            juce::AudioBuffer<float> l, r;
            double srL = 0, srR = 0;
            if (!readWav(fm, f, l, srL))
                continue;
            const bool haveR = rFile.existsAsFile() && readWav(fm, rFile, r, srR);
            const int len =
                haveR ? juce::jmax(l.getNumSamples(), r.getNumSamples()) : l.getNumSamples();
            lt.buf.setSize(2, len, false, true, false);
            lt.buf.clear();
            lt.buf.copyFrom(0, 0, l, 0, 0, l.getNumSamples());
            if (haveR) {
                lt.buf.copyFrom(1, 0, r, 0, 0, r.getNumSamples());
                consumed.add(rFile.getFullPathName());
            } else {
                lt.buf.copyFrom(1, 0, l, 0, 0, l.getNumSamples());
            }
            lt.name = cleanName(base);
            sr = srL;
        } else if (stem.endsWithIgnoreCase(".R")) {
            continue;  // handled by its .L sibling
        } else {
            if (!readWav(fm, f, lt.buf, sr))
                continue;
            lt.name = cleanName(stem);
        }

        lt.role = inferRole(lt.name);
        maxLen = juce::jmax(maxLen, lt.buf.getNumSamples());
        consumed.add(f.getFullPathName());
        tracks.push_back(std::move(lt));
    }

    REQUIRE(!tracks.empty());
    REQUIRE(sr > 0.0);

    // Sum a master mixdown (raw stem sum), then normalise to -1 dBFS peak so the
    // master loudness/dynamics are sensible (gain is PLR-invariant).
    juce::AudioBuffer<float> master(2, maxLen);
    master.clear();
    for (const auto& t : tracks) {
        const int n = t.buf.getNumSamples();
        const int nch = t.buf.getNumChannels();
        master.addFrom(0, 0, t.buf, 0, 0, n);
        master.addFrom(1, 0, t.buf, nch > 1 ? 1 : 0, 0, n);
    }
    const float peak = master.getMagnitude(0, maxLen);
    if (peak > 0.0f)
        master.applyGain(juce::Decibels::decibelsToGain(-1.0f) / peak);

    // Build the agent input from the measured stems + master.
    MixAnalysisAgent::Input input;
    std::cout << "\n[audio] measured " << tracks.size() << " stems @ " << sr << " Hz\n";
    std::cout << "name | LUFS-I | peak dB | PLR | corr | width\n";
    for (const auto& t : tracks) {
        auto snap = measure(t.buf, sr);
        auto mix = toTrackMix(t.name, t.role, snap);
        std::cout << mix.name << " | " << juce::String(mix.integratedLufs, 1) << " | "
                  << juce::String(mix.samplePeakDb, 1) << " | " << juce::String(mix.plr, 1) << " | "
                  << juce::String(mix.correlation, 2) << " | " << juce::String(mix.width, 2)
                  << "\n";
        input.tracks.push_back(std::move(mix));
    }
    input.master = toTrackMix("Master (stem sum, -1 dBFS)", "master", measure(master, sr));
    input.question = "Assess this raw multitrack: balance, dynamics, stereo image, and any "
                     "frequency clashes. What would you address first?";

    MixAnalysisAgent agent;
    auto result = agent.generate(input);

    std::cout << "\n==== payload (" << result.payload.size() << " chars) ====\n"
              << result.payload << "\n";

    if (result.hasError) {
        WARN("LLM call failed (key/model configured?): " << result.error);
        return;
    }

    std::cout << "==== analysis (" << result.wallSeconds << "s) ====\n"
              << result.analysis << "\n========\n";
    CHECK_FALSE(result.analysis.empty());
}
