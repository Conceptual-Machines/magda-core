#include "mixing_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"
#include "llm_config_utils.hpp"
#include "llm_presets.hpp"

namespace magda {

const char* MixAnalysisAgent::getSystemPrompt() {
    return "You are a senior mixing engineer assessing a song. You cannot hear the audio; "
           "instead you are given objective measurements for every track and for the master bus.\n"
           "Each row is: name [role] | LUFS-I (integrated loudness) | peak dBFS | PLR "
           "(peak-to-loudness ratio, i.e. crest / how dynamic the track is in LU) | PSR "
           "(peak-to-short-term) | corr (stereo correlation: 1 mono, ~0 wide, negative means "
           "out-of-phase) | width (0 mono .. 1 fully wide).\n"
           "You may also get inter-track masking findings: pairs of tracks whose energy competes "
           "in a frequency band, with a severity 0..1.\n\n"
           "Assess the mix and call out the biggest problems first: level balance between tracks, "
           "whether anything is too loud or buried, dynamics (very low PLR suggests "
           "over-compression "
           "/ limiting; very high suggests an uncontrolled track), stereo image and any phase "
           "risks "
           "(negative correlation), and the worst masking conflicts. Reference tracks by name and "
           "give concrete, actionable suggestions. Be concise -- prioritise signal over "
           "completeness. "
           "If a question is provided, answer it directly; otherwise give an overall assessment.";
}

juce::String MixAnalysisAgent::buildUserMessage(const Input& input) {
    auto row = [](const TrackMix& t) -> juce::String {
        juce::String r;
        r << t.name;
        if (!t.role.empty())
            r << " [" << t.role << "]";
        r << " | " << juce::String(t.integratedLufs, 1) << " | " << juce::String(t.samplePeakDb, 1)
          << " | " << juce::String(t.plr, 1) << " | " << juce::String(t.psr, 1) << " | "
          << juce::String(t.correlation, 2) << " | " << juce::String(t.width, 2);
        return r;
    };

    juce::String m;
    if (!input.question.empty())
        m << "Question: " << juce::String(input.question) << "\n\n";

    m << "Mix measurements (" << static_cast<int>(input.tracks.size()) << " tracks).\n";
    m << "Columns: name [role] | LUFS-I | peak dB | PLR | PSR | corr | width\n";
    for (const auto& t : input.tracks)
        m << row(t) << "\n";

    if (input.master)
        m << "\n[MASTER] " << row(*input.master) << "\n";

    if (!input.masking.empty()) {
        m << "\nMasking conflicts (competing tracks per band):\n";
        for (const auto& k : input.masking)
            m << "  " << k.a << " vs " << k.b << " @ " << juce::String(juce::roundToInt(k.loHz))
              << "-" << juce::String(juce::roundToInt(k.hiHz)) << " Hz, severity "
              << juce::String(k.severity, 2) << "\n";
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
