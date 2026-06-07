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
        return r;
    };

    juce::String m;
    if (!input.question.empty())
        m << "Question: " << juce::String(input.question) << "\n\n";

    m << "Mix measurements (" << static_cast<int>(input.tracks.size()) << " tracks).\n";
    m << "Columns: name [role] | LUFS-I | peak dB | TP | PLR | PSR | corr | width\n";
    for (const auto& t : input.tracks)
        m << row(t) << "\n";

    if (input.master)
        m << "\n[MASTER] " << row(*input.master) << "\n";

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

    auto response = client->sendRequest(request);
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
