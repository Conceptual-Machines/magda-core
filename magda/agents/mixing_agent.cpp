#include "mixing_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_config_utils.hpp"
#include "llm_presets.hpp"

namespace magda {

const std::vector<std::string>& MixAnalysisAgent::tonalBandLabels() {
    static const std::vector<std::string> labels = {"sub", "low",      "low-mid",
                                                    "mid", "high-mid", "high"};
    return labels;
}

const char* MixAnalysisAgent::getSystemPrompt() {
    return "You are a senior mixing engineer assessing a song. You cannot hear the audio; "
           "instead you are given objective measurements for every track and for the master bus.\n"
           "A 'Song context' line may give the genre, tempo (BPM) and key. Judge everything "
           "against the genre's conventions, not a single generic 'flat/balanced' target. Tonal "
           "balance especially is genre-relative: many genres are intentionally far from flat -- "
           "e.g. deep/lo-fi house is warm and low-end-forward with deliberately gentle, "
           "non-aggressive highs; trap leans on heavy sub; etc. Do NOT flag a genre-appropriate "
           "balance, brightness or loudness as a fault. Only call out tonal/dynamic traits that "
           "are problems even within that genre's norms.\n"
           "Each row is: name [role] | LUFS-I (integrated loudness) | peak dBFS (sample) | TP "
           "(true peak dBTP, inter-sample; >0 means real clipping) | PLR (peak-to-loudness ratio, "
           "i.e. crest / how dynamic the track is, in LU) | PSR (peak-to-short-term) | corr "
           "(stereo correlation: 1 mono, ~0 wide, negative means out-of-phase) | width (0 mono .. "
           "1 fully wide). A 'tonal:' line may follow with macro-band energy in dB "
           "(sub/low/low-mid/mid/high-mid/high) plus spectral descriptors: centroid "
           "(brightness, energy-weighted mean frequency), flat (spectral flatness 0 tonal .. 1 "
           "noisy -- high flat means a noisy/percussive source), rolloff (frequency below which "
           "85% of energy sits).\n"
           "You may also get inter-track masking findings: pairs of tracks whose energy competes "
           "in a frequency band, with a severity 0..1. These are measured, not guesses -- trust "
           "them over inference.\n"
           "Reference tracks may be given ([REF] rows): well-regarded mixes in the target genre. "
           "When present they are the ground truth for what is appropriate -- compare the subject "
           "master's loudness, tonal balance, dynamics (PLR) and width against them and flag where "
           "the subject deviates, rather than against any generic target. If the subject matches "
           "the references, say so; do not invent problems.\n"
           "A Timeline may follow: the master mix sliced over time (song sections, or fixed "
           "windows) with per-slice loudness, brightness, width and coarse tonal. Use it to reason "
           "about the arrangement -- e.g. whether choruses lift, whether the low end drops out, "
           "whether sections are consistent.\n\n"
           "Assess the mix, biggest problems first: level balance, whether anything is too loud or "
           "buried, tonal balance, dynamics, stereo image and phase risks (negative correlation), "
           "and the worst masking conflicts.\n"
           "Important nuances:\n"
           "- Do NOT over-flag high peaks or high PLR on transient/percussive sources (kick, "
           "snare, hats, toms, percussion). Brief transients near 0 dBFS are normal and often "
           "desirable there; a high PLR on a drum is healthy, not a fault.\n"
           "- Treat clipping as a real issue mainly for sustained/tonal material and the master "
           "bus, and especially when true-peak (TP) exceeds 0 dBTP. A sample peak at 0 with no TP "
           "over on a percussive source usually needs no action.\n"
           "- Very low PLR on sustained material suggests over-compression/limiting; that is the "
           "dynamics problem to flag.\n"
           "- A track may list its insert chain (the effects already on it, in order). An empty "
           "chain means no processing yet -- an early/raw-stage track; existing processing (EQ, "
           "compression, etc.) is work already done. Give contextual advice: refine what is there, "
           "and do not suggest adding processing a track already has.\n"
           "- If context that would materially change your assessment is missing (genre, the "
           "user's goal for the mix, or whether a source is a live take or programmed), ask one "
           "brief clarifying question instead of guessing.\n"
           "Reference tracks by name with concrete, actionable suggestions. Be concise. If a "
           "question is provided, answer it directly; otherwise give an overall assessment.";
}

juce::String MixAnalysisAgent::buildUserMessage(const Input& input) {
    auto row = [](const TrackMix& t) -> juce::String {
        juce::String r;
        r << t.name;
        if (!t.role.empty())
            r << " [" << t.role << "]";
        const juce::String tp = t.truePeakValid ? juce::String(t.truePeakDb, 1) : juce::String("-");
        r << " | " << juce::String(t.integratedLufs, 1) << " | " << juce::String(t.samplePeakDb, 1)
          << " | " << tp << " | " << juce::String(t.plr, 1) << " | " << juce::String(t.psr, 1)
          << " | " << juce::String(t.correlation, 2) << " | " << juce::String(t.width, 2);
        if (!t.tonalDb.empty()) {
            r << "\n    tonal:";
            const auto& labels = tonalBandLabels();
            for (size_t i = 0; i < t.tonalDb.size(); ++i)
                r << " " << (i < labels.size() ? juce::String(labels[i]) : juce::String((int)i))
                  << "=" << juce::String(t.tonalDb[i], 1);
            r << " | centroid=" << juce::String(juce::roundToInt(t.spectralCentroidHz)) << "Hz"
              << " flat=" << juce::String(t.spectralFlatness, 2)
              << " rolloff=" << juce::String(juce::roundToInt(t.spectralRolloffHz)) << "Hz";
        }
        if (!t.chain.empty()) {
            r << "\n    chain:";
            for (const auto& d : t.chain)
                r << " [" << juce::String(d) << "]";
        }
        return r;
    };

    juce::String m;
    if (!input.question.empty())
        m << "Question: " << juce::String(input.question) << "\n\n";

    if (!input.priorContext.empty())
        m << juce::String(input.priorContext) << "\n\n";

    if (input.bpm > 0.0f || !input.genre.empty()) {
        m << "Song context:";
        if (!input.genre.empty())
            m << " genre=" << juce::String(input.genre);
        if (input.bpm > 0.0f)
            m << " | BPM=" << juce::String(input.bpm, 1);
        m << "\n\n";
    }

    m << "Mix measurements (" << static_cast<int>(input.tracks.size()) << " tracks).\n";
    m << "Columns: name [role] | LUFS-I | peak dB | TP | PLR | PSR | corr | width\n";
    for (const auto& t : input.tracks)
        m << row(t) << "\n";

    if (input.master)
        m << "\n[MASTER] " << row(*input.master) << "\n";

    if (!input.references.empty()) {
        m << "\nReference tracks (well-mixed examples in the target genre -- compare the master "
             "against these; they define what 'right' looks like here):\n";
        for (const auto& r : input.references)
            m << "[REF] " << row(r) << "\n";
    }

    if (!input.masking.empty()) {
        m << "\nMasking conflicts (measured; competing tracks per band):\n";
        for (const auto& k : input.masking)
            m << "  " << k.a << " vs " << k.b << " @ " << juce::String(juce::roundToInt(k.loHz))
              << "-" << juce::String(juce::roundToInt(k.hiHz)) << " Hz, severity "
              << juce::String(k.severity, 2) << "\n";
    }

    if (!input.timeline.empty()) {
        m << "\nTimeline (master over time -- how the arrangement evolves):\n";
        for (const auto& s : input.timeline) {
            m << "  " << s.label << " [" << juce::String(s.startSec, 0) << "-"
              << juce::String(s.endSec, 0) << "s]: " << juce::String(s.integratedLufs, 1)
              << " LUFS, centroid " << juce::String(juce::roundToInt(s.spectralCentroidHz)) << "Hz"
              << ", width " << juce::String(s.width, 2);
            if (!s.tonalDb.empty()) {
                static const char* k3[] = {"low", "mid", "high"};
                m << ", tonal";
                for (size_t i = 0; i < s.tonalDb.size(); ++i)
                    m << " " << (i < 3 ? k3[i] : "?") << "=" << juce::String(s.tonalDb[i], 1);
            }
            m << "\n";
        }
    }

    return m;
}

MixAnalysisAgent::Result MixAnalysisAgent::generate(const Input& input) {
    return generateStreaming(input, {});
}

MixAnalysisAgent::Result MixAnalysisAgent::generateStreaming(const Input& input,
                                                             TokenCallback onToken) {
    Result result;
    result.payload = buildUserMessage(input).toStdString();

    if (input.tracks.empty()) {
        result.hasError = true;
        result.error = "No tracks to analyse.";
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::COMMAND);
    auto providerConfig = toLLMProviderConfig(agentConfig, "mix_analysis");
    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty() &&
        agentConfig.provider != provider::LLAMA_LOCAL) {
        result.hasError = true;
        result.error = "AI is not configured (no API key).";
        return result;
    }

    auto client = createLLMClient(agentConfig, "mix_analysis");
    if (client == nullptr) {
        result.hasError = true;
        result.error = "Could not create the LLM client.";
        return result;
    }

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String(result.payload);
    request.temperature = 0.3f;

    llm::Response response;
    if (onToken) {
        response = client->sendStreamingRequest(
            request, [&](const juce::String& token) { return onToken(token); });
    } else {
        response = client->sendRequest(request);
    }
    result.wallSeconds = response.wallSeconds;
    result.inputTokens = response.inputTokens;
    result.outputTokens = response.outputTokens;
    result.totalTokens = response.totalTokens;
    result.rawOutput = response.text.toStdString();
    if (!response.success) {
        result.hasError = true;
        result.error = response.error.toStdString();
        return result;
    }

    result.analysis = response.text.toStdString();
    return result;
}

}  // namespace magda
