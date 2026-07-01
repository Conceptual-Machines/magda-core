#include "LLMClientProvider.hpp"

#include <cstdlib>
#include <mutex>

#include "version.hpp"

namespace magda {
namespace {

std::mutex& providerMutex() {
    static std::mutex mutex;
    return mutex;
}

LLMClientProvider& providerSlot() {
    static LLMClientProvider provider;
    return provider;
}

LLMProviderShutdownHandler& shutdownHandlerSlot() {
    static LLMProviderShutdownHandler handler;
    return handler;
}

std::unique_ptr<llm::LLMClient> createDefaultLLMClient(const Config::AgentLLMConfig& config,
                                                       const std::string& agentName) {
    return llm::LLMClientFactory::create(toLLMProviderConfig(config, agentName));
}

}  // namespace

std::string normalizeOpenAIBaseUrl(std::string url) {
    const auto isWs = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    while (begin < url.size() && isWs(url[begin]))
        ++begin;
    std::size_t end = url.size();
    while (end > begin && isWs(url[end - 1]))
        --end;
    url = url.substr(begin, end - begin);

    if (url.empty())
        return url;

    while (!url.empty() && url.back() == '/')
        url.pop_back();

    if (url.empty())
        return url;

    const auto endsWithV1 = [](const std::string& s) {
        if (s.size() < 3)
            return false;
        const std::string tail = s.substr(s.size() - 3);
        return tail[0] == '/' && (tail[1] == 'v' || tail[1] == 'V') && tail[2] == '1';
    };

    if (!endsWithV1(url))
        url += "/v1";

    return url;
}

llm::Provider providerFromString(const std::string& s) {
    if (s == provider::OPENAI_RESPONSES)
        return llm::Provider::OpenAIResponses;
    if (s == provider::ANTHROPIC)
        return llm::Provider::Anthropic;
    if (s == provider::GEMINI)
        return llm::Provider::Gemini;
    return llm::Provider::OpenAIChat;
}

juce::String defaultBaseUrl(const std::string& providerStr) {
    if (providerStr == provider::DEEPSEEK)
        return "https://api.deepseek.com";
    if (providerStr == provider::OPENROUTER)
        return "https://openrouter.ai/api/v1";
    if (providerStr == provider::ANTHROPIC)
        return "https://api.anthropic.com/v1";
    if (providerStr == provider::GEMINI)
        return "https://generativelanguage.googleapis.com";
    return "https://api.openai.com/v1";
}

llm::ProviderConfig toLLMProviderConfig(const Config::AgentLLMConfig& config,
                                        const std::string& agentName) {
    auto providerEnum = providerFromString(config.provider);
    const bool isLocalServer = config.provider == provider::LOCAL_SERVER;

    llm::ProviderConfig pc;
    pc.provider = providerEnum;
    pc.model = juce::String(config.model);

    if (isLocalServer && pc.model.isEmpty())
        pc.model = juce::String(Config::getInstance().getLocalServerModel());

    if (pc.model.startsWith("claude-opus-"))
        pc.model = model::CLAUDE_OPUS;
    else if (pc.model.startsWith("claude-sonnet-"))
        pc.model = model::CLAUDE_SONNET;
    else if (pc.model.startsWith("claude-haiku-"))
        pc.model = model::CLAUDE_HAIKU;

    if (pc.model.startsWith("claude-opus-"))
        pc.noTemperature = true;

    if (isLocalServer) {
        std::string raw =
            !config.baseUrl.empty() ? config.baseUrl : Config::getInstance().getLocalServerUrl();
        if (raw.empty())
            raw = DEFAULT_LOCAL_SERVER_URL;
        pc.baseUrl = juce::String(normalizeOpenAIBaseUrl(raw));
    } else {
        pc.baseUrl =
            config.baseUrl.empty() ? defaultBaseUrl(config.provider) : juce::String(config.baseUrl);
    }

    if (isLocalServer) {
        std::string key =
            !config.apiKey.empty() ? config.apiKey : Config::getInstance().getLocalServerApiKey();
        pc.apiKey = key.empty() ? juce::String("local") : juce::String(key);
    } else if (!config.apiKey.empty()) {
        pc.apiKey = juce::String(config.apiKey);
    } else {
        auto credential = Config::getInstance().getAICredential(config.provider);

        if (credential.empty() && config.provider == provider::OPENAI_RESPONSES)
            credential = Config::getInstance().getAICredential(provider::OPENAI_CHAT);

        if (!credential.empty()) {
            pc.apiKey = juce::String(credential);
        } else {
            const char* envVar = nullptr;
            if (providerEnum == llm::Provider::OpenAIChat ||
                providerEnum == llm::Provider::OpenAIResponses)
                envVar = std::getenv("OPENAI_API_KEY");
            else if (providerEnum == llm::Provider::Anthropic)
                envVar = std::getenv("ANTHROPIC_API_KEY");
            else if (providerEnum == llm::Provider::Gemini)
                envVar = std::getenv("GEMINI_API_KEY");
            if (envVar != nullptr)
                pc.apiKey = juce::String(envVar);
        }
    }

    if (pc.model.startsWith("gpt-5")) {
        pc.noTemperature = true;
        if (pc.reasoningEffort.isEmpty())
            pc.reasoningEffort = "low";
    }

    pc.userAgent = juce::String("MAGDA/") + MAGDA_VERSION;
    if (!agentName.empty())
        pc.userAgent += " (" + juce::String(agentName) + ")";
    pc.appUrl = "https://magda.dev";

    return pc;
}

bool supportsOpenAICFG(const Config::AgentLLMConfig& config) {
    auto pc = toLLMProviderConfig(config);
    return pc.provider == llm::Provider::OpenAIResponses && pc.model.startsWith("gpt-5");
}

void setLLMClientProvider(LLMClientProvider provider) {
    std::lock_guard<std::mutex> lock(providerMutex());
    providerSlot() = std::move(provider);
}

void setLLMProviderShutdownHandler(LLMProviderShutdownHandler handler) {
    std::lock_guard<std::mutex> lock(providerMutex());
    shutdownHandlerSlot() = std::move(handler);
}

std::unique_ptr<llm::LLMClient> createLLMClient(const Config::AgentLLMConfig& config,
                                                const std::string& agentName) {
    LLMClientProvider provider;
    {
        std::lock_guard<std::mutex> lock(providerMutex());
        provider = providerSlot();
    }

    return provider ? provider(config, agentName) : createDefaultLLMClient(config, agentName);
}

void shutdownLLMClientProvider() {
    LLMProviderShutdownHandler handler;
    {
        std::lock_guard<std::mutex> lock(providerMutex());
        handler = shutdownHandlerSlot();
    }

    if (handler)
        handler();
}

}  // namespace magda
