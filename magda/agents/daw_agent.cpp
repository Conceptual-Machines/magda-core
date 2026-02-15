#include "daw_agent.hpp"

#include "dsl_grammar.hpp"

namespace magda {

DAWAgent::DAWAgent() = default;
DAWAgent::~DAWAgent() = default;

std::map<std::string, std::string> DAWAgent::getCapabilities() const {
    return {
        {"track_management", "create, delete, modify tracks"},
        {"clip_management", "create, delete clips"},
        {"llm_backend", "OpenAI GPT-5.2 with CFG grammar"},
    };
}

bool DAWAgent::start() {
    running_ = true;
    return true;
}

void DAWAgent::stop() {
    running_ = false;
}

void DAWAgent::setMessageCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    messageCallback_ = std::move(callback);
}

std::string DAWAgent::processMessage(const std::string& message) {
    if (!running_)
        return "Agent is not running.";

    // Reload config in case the user changed settings
    openai_.loadFromConfig();

    if (!openai_.hasApiKey())
        return "OpenAI API key not configured. Set it in Preferences > AI Assistant.";

    // 1. Build state snapshot
    auto stateJson = dsl::Interpreter::buildStateSnapshot();

    // 2. Call OpenAI with CFG grammar
    auto dslResult =
        openai_.generateDSL(juce::String(message), stateJson, juce::String(dsl::getGrammar()),
                            juce::String(dsl::getToolDescription()));

    if (dslResult.isEmpty()) {
        return "Error: " + openai_.getLastError().toStdString();
    }

    DBG("MAGDA DAWAgent: DSL received: " + dslResult);

    // 3. Execute DSL
    if (!interpreter_.execute(dslResult.toRawUTF8())) {
        return "DSL execution error: " + std::string(interpreter_.getError()) +
               "\nDSL was: " + dslResult.toStdString();
    }

    // 4. Build result
    auto results = interpreter_.getResults();
    if (results.isEmpty())
        return "Done.";

    return results.toStdString();
}

}  // namespace magda
