#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "magda/agents/mixing_agent.hpp"
#include "magda/daw/audio/analysis/BandSpectrum.hpp"
#include "magda/daw/audio/analysis/MaskingDetector.hpp"
#include "magda/daw/audio/analysis/TrackMeasurer.hpp"
#include "magda/daw/core/Config.hpp"

using namespace magda;
namespace audio = magda::daw::audio;
using audio::TrackMeasurementSnapshot;
using audio::TrackMeasurer;

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

// 2048 so each processed block is exactly one masking-FFT frame: after each
// block we pull the band spectrum from the measurer's ring, tiling the song.
constexpr int kBlock = 2048;

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

using BandArray = std::array<float, audio::kNumMaskingBands>;

// Measure a buffer; also produces the song-averaged 1/3-octave band spectrum
// (energy mean over all frames) when bandsOut is given.
TrackMeasurementSnapshot measure(const juce::AudioBuffer<float>& buf, double sr,
                                 BandArray* bandsOut = nullptr, bool enableTruePeak = false) {
    // True-peak (4x oversampler) is ~60 ops/sample and dominates the cost across
    // 20+ stems, so it's off for per-track / per-segment (sample peak there,
    // matching production policy) and only on for the master.
    TrackMeasurer m;
    m.prepare(sr, kBlock, enableTruePeak);
    if (bandsOut != nullptr)
        m.setSpectrumCaptureEnabled(true);

    const int len = buf.getNumSamples();
    const int nch = juce::jmin(2, buf.getNumChannels());
    std::array<double, audio::kNumMaskingBands> acc{};
    long frames = 0;
    BandArray frameBands{};

    // computeMaskingBandsDb rebuilds an FFT + window per call (cheap once-a-poll
    // in production, ruinous every block here). A song-average spectrum only
    // needs ~128 frames, so only analyse every `hop`-th block.
    const int totalBlocks = (len + kBlock - 1) / kBlock;
    const int hop = juce::jmax(1, totalBlocks / 128);
    int blockIdx = 0;

    for (int pos = 0; pos < len; pos += kBlock, ++blockIdx) {
        const int n = juce::jmin(kBlock, len - pos);
        const float* chans[2];
        chans[0] = buf.getReadPointer(0) + pos;
        chans[1] = nch > 1 ? buf.getReadPointer(1) + pos : chans[0];
        m.process(chans, nch, n);
        if (bandsOut != nullptr && n >= 2048 && (blockIdx % hop) == 0) {
            audio::computeMaskingBandsDb(m.getSpectrumRing(), sr, frameBands);
            for (int b = 0; b < audio::kNumMaskingBands; ++b)
                acc[static_cast<size_t>(b)] +=
                    std::pow(10.0, frameBands[static_cast<size_t>(b)] / 10.0);
            ++frames;
        }
    }
    if (bandsOut != nullptr)
        for (int b = 0; b < audio::kNumMaskingBands; ++b)
            (*bandsOut)[static_cast<size_t>(b)] =
                frames > 0
                    ? static_cast<float>(10.0 * std::log10(acc[static_cast<size_t>(b)] / frames))
                    : -120.0f;
    return m.read();
}

// Collapse the 30 1/3-octave bands into 6 macro bands (summed energy, dB),
// ordered to match MixAnalysisAgent::tonalBandLabels().
std::vector<float> collapseToMacro(const BandArray& bandsDb) {
    const float upper[6] = {60.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 1.0e9f};
    std::array<double, 6> acc{};
    for (int b = 0; b < audio::kNumMaskingBands; ++b) {
        const float center =
            std::sqrt(audio::maskingBandEdgeHz(b) * audio::maskingBandEdgeHz(b + 1));
        int mi = 0;
        while (mi < 5 && center >= upper[mi])
            ++mi;
        acc[static_cast<size_t>(mi)] += std::pow(10.0, bandsDb[static_cast<size_t>(b)] / 10.0);
    }
    std::vector<float> out(6);
    for (int i = 0; i < 6; ++i)
        out[static_cast<size_t>(i)] =
            acc[static_cast<size_t>(i)] > 0.0
                ? static_cast<float>(10.0 * std::log10(acc[static_cast<size_t>(i)]))
                : -120.0f;
    return out;
}

// Collapse the 30 bands into 3 coarse bands (low / mid / high) for the timeline.
std::vector<float> collapseTo3(const BandArray& bandsDb) {
    const float upper[3] = {250.0f, 2500.0f, 1.0e9f};
    std::array<double, 3> acc{};
    for (int b = 0; b < audio::kNumMaskingBands; ++b) {
        const float center =
            std::sqrt(audio::maskingBandEdgeHz(b) * audio::maskingBandEdgeHz(b + 1));
        int mi = 0;
        while (mi < 2 && center >= upper[mi])
            ++mi;
        acc[static_cast<size_t>(mi)] += std::pow(10.0, bandsDb[static_cast<size_t>(b)] / 10.0);
    }
    std::vector<float> out(3);
    for (int i = 0; i < 3; ++i)
        out[static_cast<size_t>(i)] =
            acc[static_cast<size_t>(i)] > 0.0
                ? static_cast<float>(10.0 * std::log10(acc[static_cast<size_t>(i)]))
                : -120.0f;
    return out;
}

struct SpectralFeatures {
    float centroidHz = 0.0f;
    float flatness = 0.0f;
    float rolloffHz = 0.0f;
};

// Derive brightness / flatness / rolloff from the averaged band spectrum.
SpectralFeatures spectralFeatures(const BandArray& bandsDb) {
    std::array<double, audio::kNumMaskingBands> e{};
    std::array<float, audio::kNumMaskingBands> center{};
    float peakDb = -1000.0f;
    for (int b = 0; b < audio::kNumMaskingBands; ++b) {
        e[static_cast<size_t>(b)] = std::pow(10.0, bandsDb[static_cast<size_t>(b)] / 10.0);
        center[static_cast<size_t>(b)] =
            std::sqrt(audio::maskingBandEdgeHz(b) * audio::maskingBandEdgeHz(b + 1));
        peakDb = juce::jmax(peakDb, bandsDb[static_cast<size_t>(b)]);
    }

    double sumE = 0.0, sumFE = 0.0;
    for (int b = 0; b < audio::kNumMaskingBands; ++b) {
        sumE += e[static_cast<size_t>(b)];
        sumFE += static_cast<double>(center[static_cast<size_t>(b)]) * e[static_cast<size_t>(b)];
    }

    SpectralFeatures f;
    f.centroidHz = sumE > 0.0 ? static_cast<float>(sumFE / sumE) : 0.0f;

    // Flatness over the audible bands (within 60 dB of the peak), so empty
    // floor bands don't drag the geometric mean to zero.
    double logSum = 0.0, ariSum = 0.0;
    int cnt = 0;
    for (int b = 0; b < audio::kNumMaskingBands; ++b) {
        if (bandsDb[static_cast<size_t>(b)] < peakDb - 60.0f)
            continue;
        logSum += std::log(e[static_cast<size_t>(b)] + 1.0e-20);
        ariSum += e[static_cast<size_t>(b)];
        ++cnt;
    }
    if (cnt > 0) {
        const double geo = std::exp(logSum / cnt);
        const double ari = ariSum / cnt;
        f.flatness = ari > 0.0 ? static_cast<float>(juce::jlimit(0.0, 1.0, geo / ari)) : 0.0f;
    }

    // 85%-energy rolloff.
    const double target = 0.85 * sumE;
    double cum = 0.0;
    for (int b = 0; b < audio::kNumMaskingBands; ++b) {
        cum += e[static_cast<size_t>(b)];
        if (cum >= target) {
            f.rolloffHz = center[static_cast<size_t>(b)];
            break;
        }
    }
    return f;
}

// Exact whole-song stereo correlation + width from the buffer (the measurer's
// read() only exposes the smoothed end-of-song value).
void stereoCorrWidth(const juce::AudioBuffer<float>& buf, float& corr, float& width) {
    if (buf.getNumChannels() < 2) {
        corr = 1.0f;
        width = 0.0f;
        return;
    }
    const float* l = buf.getReadPointer(0);
    const float* r = buf.getReadPointer(1);
    const int n = buf.getNumSamples();
    double sumLR = 0, sumLL = 0, sumRR = 0, sumMid = 0, sumSide = 0;
    for (int i = 0; i < n; ++i) {
        const double xl = l[i], xr = r[i];
        sumLR += xl * xr;
        sumLL += xl * xl;
        sumRR += xr * xr;
        const double mid = 0.5 * (xl + xr), side = 0.5 * (xl - xr);
        sumMid += mid * mid;
        sumSide += side * side;
    }
    const double denom = std::sqrt(sumLL * sumRR);
    corr = denom > 1.0e-12 ? static_cast<float>(juce::jlimit(-1.0, 1.0, sumLR / denom)) : 1.0f;
    const double ms = sumMid + sumSide;
    width = ms > 1.0e-12 ? static_cast<float>(sumSide / ms) : 0.0f;
}

// Section boundaries. Auto = N equal fixed windows; this is the seam where
// real UI song-sections will plug in later (same {label, start, len} shape).
struct SectionBound {
    juce::String label;
    int start = 0;
    int len = 0;
};
std::vector<SectionBound> autoSections(int total, int n) {
    std::vector<SectionBound> out;
    for (int i = 0; i < n; ++i) {
        const int s = static_cast<int>(static_cast<int64_t>(total) * i / n);
        const int e = static_cast<int>(static_cast<int64_t>(total) * (i + 1) / n);
        out.push_back({juce::String(i + 1) + "/" + juce::String(n), s, e - s});
    }
    return out;
}

MixAnalysisAgent::TrackMix toTrackMix(const juce::String& name, const std::string& role,
                                      const TrackMeasurementSnapshot& s) {
    MixAnalysisAgent::TrackMix t;
    t.name = name.toStdString();
    t.role = role;
    t.integratedLufs = s.integratedLufs;
    t.shortTermLufs = s.shortTermLufs;
    t.samplePeakDb = s.samplePeakDb;
    t.truePeakDb = s.truePeakDb;
    t.truePeakValid = s.truePeakValid;
    t.plr = s.plr;
    t.psr = s.psr;
    t.correlation = s.correlation;
    t.width = s.width;
    return t;
}

// Map a model name to its provider wire id (so a comparison list can be just
// model names). GPT-5 family needs the Responses API.
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

}  // namespace

// Run the whole pipeline on one song folder: measure stems, sum the master,
// build the timeline + masking + context, then run each model and print.
void analyzeSong(const juce::File& dir, const juce::StringArray& models) {
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

    // Build the agent input from the measured stems + master. Each stem also
    // gets its averaged 1/3-octave spectrum, collapsed to a macro-band tonal
    // profile, and contributed to inter-track masking detection.
    MixAnalysisAgent::Input input;
    std::vector<audio::TrackBandEnergies> bandSet;
    std::cout << "\n[audio] measured " << tracks.size() << " stems @ " << sr << " Hz\n";
    std::cout << "name | LUFS-I | peak | TP | PLR | corr | width\n";
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
        const auto& t = tracks[static_cast<size_t>(i)];
        BandArray bands{};
        auto snap = measure(t.buf, sr, &bands);
        auto mix = toTrackMix(t.name, t.role, snap);
        mix.tonalDb = collapseToMacro(bands);
        const auto sf = spectralFeatures(bands);
        mix.spectralCentroidHz = sf.centroidHz;
        mix.spectralFlatness = sf.flatness;
        mix.spectralRolloffHz = sf.rolloffHz;
        stereoCorrWidth(t.buf, mix.correlation, mix.width);  // whole-song, not end-of-song

        audio::TrackBandEnergies be;
        be.trackId = i;
        be.name = t.name;
        be.bandDb = bands;
        bandSet.push_back(std::move(be));

        std::cout << mix.name << " | " << juce::String(mix.integratedLufs, 1) << " | "
                  << juce::String(mix.samplePeakDb, 1) << " | " << juce::String(mix.truePeakDb, 1)
                  << " | " << juce::String(mix.plr, 1) << " | " << juce::String(mix.correlation, 2)
                  << " | " << juce::String(mix.width, 2) << "\n";
        input.tracks.push_back(std::move(mix));
    }

    BandArray masterBands{};
    input.master = toTrackMix("Master (stem sum, -1 dBFS)", "master",
                              measure(master, sr, &masterBands, /*enableTruePeak*/ true));
    input.master->tonalDb = collapseToMacro(masterBands);
    {
        const auto sf = spectralFeatures(masterBands);
        input.master->spectralCentroidHz = sf.centroidHz;
        input.master->spectralFlatness = sf.flatness;
        input.master->spectralRolloffHz = sf.rolloffHz;
        stereoCorrWidth(master, input.master->correlation, input.master->width);
    }

    // Real inter-track masking from the measured spectra (#1390).
    auto findings = audio::detectMasking(bandSet);
    for (const auto& f : findings)
        input.masking.push_back(
            {f.nameA.toStdString(), f.nameB.toStdString(), f.loHz, f.hiHz, f.severity});
    std::cout << "[audio] masking findings: " << findings.size() << "\n";

    // Timeline: slice the master into sections (auto fixed windows for now; the
    // UI's song sections will feed the same {label,start,len} list later) and
    // measure each slice's loudness / brightness / width / coarse tonal.
    int numSegments = 16;
    if (const char* env = std::getenv("MIX_ANALYSIS_SEGMENTS"))
        numSegments = juce::jlimit(2, 64, juce::String(env).getIntValue());
    for (const auto& sec : autoSections(maxLen, numSegments)) {
        if (sec.len < 2048)
            continue;
        float* chans[2] = {master.getWritePointer(0) + sec.start,
                           master.getWritePointer(1) + sec.start};
        juce::AudioBuffer<float> win(chans, 2, sec.len);
        BandArray segBands{};
        auto segSnap = measure(win, sr, &segBands);

        MixAnalysisAgent::Segment seg;
        seg.label = sec.label.toStdString();
        seg.startSec = static_cast<float>(sec.start / sr);
        seg.endSec = static_cast<float>((sec.start + sec.len) / sr);
        seg.integratedLufs = segSnap.integratedLufs;
        seg.spectralCentroidHz = spectralFeatures(segBands).centroidHz;
        float segCorr = 1.0f;
        stereoCorrWidth(win, segCorr, seg.width);  // only width is sent per segment
        seg.tonalDb = collapseTo3(segBands);
        input.timeline.push_back(std::move(seg));
    }
    std::cout << "[audio] timeline segments: " << input.timeline.size() << "\n";

    // Song context (BPM, genre) comes from the project/transport in the real
    // app, not from detection. This stems-only harness has no transport, so it
    // only sets them when supplied via env; otherwise they're omitted entirely.
    if (const char* b = std::getenv("MIX_ANALYSIS_BPM"))
        input.bpm = juce::String(b).getFloatValue();
    if (const char* g = std::getenv("MIX_ANALYSIS_GENRE"))
        input.genre = g;
    std::cout << "[audio] context: genre=" << (input.genre.empty() ? "(none)" : input.genre)
              << " bpm=" << input.bpm << "\n";

    input.question = "Assess this raw multitrack: balance, dynamics, stereo image, frequency "
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

    for (const auto& song : songs)
        analyzeSong(song, models);
}
