#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <iostream>

#include "magda/agents/mixing_agent.hpp"
#include "magda/daw/audio/analysis/MixAnalysisInput.hpp"
#include "magda/daw/core/Config.hpp"

using namespace magda;
namespace audio = magda::daw::audio;

// ============================================================================
// Real-audio mix-analysis harness (#886, exploratory).
//
// Loads every stem in a song folder, builds the agent input via the shared
// MixAnalysisInput pipeline (the same code the app uses on offline-rendered
// stems), and runs MixAnalysisAgent against the configured LLM(s). Hidden [.]
// test -- needs the stems + an API key, so it never runs in CI. Run it with:
//   ./cmake-build-debug/tests/magda_tests "[mix_analysis][audio]"
// Env: MIX_ANALYSIS_AUDIO_DIR (root of song folders), MIX_ANALYSIS_MODELS,
//      MIX_ANALYSIS_GENRE, MIX_ANALYSIS_BPM, MIX_ANALYSIS_REFERENCES,
//      MIX_ANALYSIS_SEGMENTS. API keys come from <repo>/.env.
// ============================================================================

namespace {

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
        if (val.isNotEmpty()) {
#if defined(_WIN32)
            if (std::getenv(key.toRawUTF8()) == nullptr)
                _putenv_s(key.toRawUTF8(), val.toRawUTF8());
#else
            ::setenv(key.toRawUTF8(), val.toRawUTF8(), /*overwrite*/ 0);
#endif
        }
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

// Map a model name to its provider wire id. GPT-5 family needs the Responses API.
std::string providerForModel(const juce::String& model) {
    auto m = model.toLowerCase();
    if (m.startsWith("gpt-5") || m.startsWith("o1") || m.startsWith("o3"))
        return "openai_responses";
    if (m.startsWith("gpt-"))
        return "openai_chat";
    if (m.startsWith("claude"))
        return "anthropic";
    if (m.startsWith("gemini"))
        return "gemini";
    if (m.startsWith("deepseek"))
        return "deepseek";
    return "openai_responses";
}

// Load reference masters once, from MIX_ANALYSIS_REFERENCES (a directory of wavs
// or a comma-separated list of files). Each is fingerprinted as a genre target.
std::vector<MixAnalysisAgent::TrackMix> loadReferences(juce::AudioFormatManager& fm) {
    std::vector<MixAnalysisAgent::TrackMix> refs;
    const char* env = std::getenv("MIX_ANALYSIS_REFERENCES");
    if (env == nullptr)
        return refs;

    juce::Array<juce::File> files;
    juce::File path(juce::String::fromUTF8(env));
    if (path.isDirectory()) {
        path.findChildFiles(files, juce::File::findFiles, false, "*.wav");
    } else {
        for (const auto& tok : juce::StringArray::fromTokens(juce::String::fromUTF8(env), ",", ""))
            if (tok.trim().isNotEmpty())
                files.add(juce::File(tok.trim()));
    }

    for (const auto& f : files) {
        juce::AudioBuffer<float> buf;
        double rsr = 0.0;
        if (!readWav(fm, f, buf, rsr))
            continue;
        refs.push_back(audio::MixAnalysisInput::fingerprint(
            buf, rsr, f.getFileNameWithoutExtension(), "reference"));
    }
    return refs;
}

}  // namespace

// Run the whole pipeline on one song folder: load stems, build the agent input
// via the shared module, then run each model and print.
void analyzeSong(const juce::File& dir, const juce::StringArray& models,
                 const std::vector<MixAnalysisAgent::TrackMix>& references) {
    juce::Array<juce::File> wavs;
    dir.findChildFiles(wavs, juce::File::findFiles, false, "*.wav");
    if (wavs.isEmpty()) {
        WARN("No .wav stems in " << dir.getFullPathName());
        return;
    }

    std::cout << "\n################################################################\n"
              << "# SONG: " << dir.getFileName() << "  (" << wavs.size() << " files)\n"
              << "################################################################\n";

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    // A "Full Mix" stem (stem packs often ship one) is the finished master:
    // use it as the master and keep it out of the component tracks.
    juce::StringArray consumed;
    juce::File fullMixFile;
    for (const auto& f : wavs) {
        const auto u = f.getFileName().toUpperCase();
        if (u.contains("FULL") && u.contains("MIX")) {
            fullMixFile = f;
            consumed.add(f.getFullPathName());
            break;
        }
    }

    // Pair ".L"/".R" stems into one stereo source; everything else is mono.
    std::vector<LoadedTrack> tracks;
    double sr = 0.0;
    auto stemOf = [](const juce::File& f) { return f.getFileNameWithoutExtension(); };

    for (const auto& f : wavs) {
        if (consumed.contains(f.getFullPathName()))
            continue;
        auto stem = stemOf(f);

        LoadedTrack lt;
        if (stem.endsWithIgnoreCase(".L")) {
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
        consumed.add(f.getFullPathName());
        tracks.push_back(std::move(lt));
    }

    REQUIRE(!tracks.empty());
    REQUIRE(sr > 0.0);

    // Master: the finished Full Mix when present (measured as-is), else let the
    // pipeline build a normalised stem sum (master == nullptr).
    juce::AudioBuffer<float> master;
    juce::String masterName = "Master (stem sum, -1 dBFS)";
    if (fullMixFile.existsAsFile()) {
        double msr = sr;
        juce::AudioBuffer<float> fmix;
        if (readWav(fm, fullMixFile, fmix, msr)) {
            const int n = fmix.getNumSamples();
            master.setSize(2, n, false, true, false);
            master.clear();
            master.copyFrom(0, 0, fmix, 0, 0, n);
            master.copyFrom(1, 0, fmix, fmix.getNumChannels() > 1 ? 1 : 0, 0, n);
        }
        masterName = "Master (full mix)";
        std::cout << "[audio] master = Full Mix stem (as-is)\n";
    } else {
        std::cout << "[audio] master = normalised stem sum\n";
    }

    int numSegments = 16;
    if (const char* env = std::getenv("MIX_ANALYSIS_SEGMENTS"))
        numSegments = juce::jlimit(2, 64, juce::String(env).getIntValue());

    // Build the agent input via the shared production pipeline.
    std::vector<audio::MixAnalysisInput::Source> sources;
    sources.reserve(tracks.size());
    for (const auto& t : tracks)
        sources.push_back({t.name, t.role, &t.buf});

    audio::MixAnalysisInput::Options opts;
    opts.numSegments = numSegments;
    auto input = audio::MixAnalysisInput::build(
        sr, sources, master.getNumSamples() > 0 ? &master : nullptr, {}, opts);
    if (input.master)
        input.master->name = masterName.toStdString();
    input.references = references;

    std::cout << "[audio] " << input.tracks.size() << " stems @ " << sr << " Hz, "
              << input.masking.size() << " masking, " << input.timeline.size() << " segments\n";

    // Song context (BPM, genre) is project/transport-supplied in the app, not
    // detected. This harness only sets them from env; otherwise omitted.
    if (const char* b = std::getenv("MIX_ANALYSIS_BPM"))
        input.bpm = juce::String(b).getFloatValue();
    if (const char* g = std::getenv("MIX_ANALYSIS_GENRE"))
        input.genre = g;
    std::cout << "[audio] context: genre=" << (input.genre.empty() ? "(none)" : input.genre)
              << " bpm=" << input.bpm << "\n";

    input.question = "Assess this multitrack: balance, dynamics, stereo image, frequency "
                     "clashes, and how the arrangement evolves. What would you address first?";

    std::cout << "\n==== payload (" << MixAnalysisAgent::buildUserMessage(input).length()
              << " chars) ====\n"
              << MixAnalysisAgent::buildUserMessage(input) << "\n";

    // Run each model against the identical input and print them back to back.
    for (const auto& model : models) {
        Config::AgentLLMConfig cfg;
        cfg.provider = providerForModel(model);
        cfg.model = model.toStdString();
        Config::getInstance().setAgentLLMConfig("command", cfg);

        std::cout << "\n################################################################\n"
                  << "# MODEL: " << model << "  (provider " << cfg.provider << ")\n"
                  << "################################################################\n";

        MixAnalysisAgent agent;
        auto result = agent.generate(input);
        if (result.hasError) {
            std::cout << "[ERROR] " << result.error << "\n";
            WARN("Model " << model << " failed: " << result.error);
            continue;
        }
        std::cout << "---- " << model << ": " << result.wallSeconds
                  << "s | tokens in/out/total = " << result.inputTokens << "/"
                  << result.outputTokens << "/" << result.totalTokens << " ----\n"
                  << result.analysis << "\n";
        CHECK_FALSE(result.analysis.empty());
    }
}

TEST_CASE("MixAnalysisAgent: analyse real multitrack sessions", "[.][mix_analysis][audio]") {
    std::cout << std::unitbuf;  // flush each <<, so progress shows when redirected
    loadDotEnv(juce::File(juce::String(MAGDA_REPO_ROOT) + "/.env"));

    // Models to compare. Comma-separated MIX_ANALYSIS_MODELS (provider inferred
    // per name); defaults to a single gpt-5.
    juce::StringArray models;
    if (const char* env = std::getenv("MIX_ANALYSIS_MODELS"))
        models.addTokens(juce::String(env), ",", "");
    else
        models.add("gpt-5");
    models.trim();
    models.removeEmptyStrings();

    // Root dir: MIX_ANALYSIS_AUDIO_DIR (a folder of per-song subfolders, or a
    // single song's stems), else the bundled turkuaz fixture.
    const char* rootEnv = std::getenv("MIX_ANALYSIS_AUDIO_DIR");
    juce::File root = rootEnv != nullptr
                          ? juce::File(juce::String::fromUTF8(rootEnv))
                          : juce::File(juce::String(MAGDA_AUDIO_FIXTURES_DIR) + "/turkuaz");
    if (!root.isDirectory()) {
        WARN("Audio dir not found: " << root.getFullPathName());
        return;
    }

    // Each immediate subfolder holding stems is a song; if the root itself has
    // stems, treat it as a single song.
    std::vector<juce::File> songs;
    juce::Array<juce::File> rootWavs;
    root.findChildFiles(rootWavs, juce::File::findFiles, false, "*.wav");
    if (!rootWavs.isEmpty()) {
        songs.push_back(root);
    } else {
        juce::Array<juce::File> subs;
        root.findChildFiles(subs, juce::File::findDirectories, false);
        for (const auto& s : subs) {
            juce::Array<juce::File> w;
            s.findChildFiles(w, juce::File::findFiles, false, "*.wav");
            if (!w.isEmpty())
                songs.push_back(s);
        }
    }
    std::sort(songs.begin(), songs.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFullPathName() < b.getFullPathName();
    });

    if (songs.empty()) {
        WARN("No songs (stem folders) under " << root.getFullPathName());
        return;
    }
    std::cout << "[audio] " << songs.size() << " song(s) under " << root.getFullPathName() << "\n";

    // Reference masters (measured once, shared across all songs).
    juce::AudioFormatManager refFm;
    refFm.registerBasicFormats();
    auto references = loadReferences(refFm);
    std::cout << "[audio] " << references.size() << " reference track(s)\n";

    for (const auto& song : songs)
        analyzeSong(song, models, references);
}
