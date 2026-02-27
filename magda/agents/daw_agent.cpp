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
    shouldStop_ = true;
    running_ = false;
}

void DAWAgent::setMessageCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    messageCallback_ = std::move(callback);
}

std::string DAWAgent::processMessage(const std::string& message) {
    auto dsl = generateDSL(message);
    if (dsl.hasError)
        return dsl.error;
    return executeDSL(dsl);
}

DAWAgent::DSLResult DAWAgent::generateDSL(const std::string& message) {
    DSLResult result;

    if (!running_) {
        result.error = "Agent is not running.";
        result.hasError = true;
        return result;
    }

    // Reload config in case the user changed settings
    openai_.loadFromConfig();

    if (!openai_.hasApiKey()) {
        result.error = "OpenAI API key not configured. Set it in Preferences > AI Assistant.";
        result.hasError = true;
        return result;
    }

    // 1. Build state snapshot
    auto stateJson = dsl::Interpreter::buildStateSnapshot();

    // 2. Call OpenAI with CFG grammar
    auto dslString =
        openai_.generateDSL(juce::String(message), stateJson, juce::String(dsl::getGrammar()),
                            juce::String(dsl::getToolDescription()), &shouldStop_);

    if (dslString.isEmpty()) {
        result.error = "Error: " + openai_.getLastError().toStdString();
        result.hasError = true;
        return result;
    }

    DBG("MAGDA DAWAgent: DSL received: " + dslString);
    result.dsl = dslString.toStdString();
    return result;
}

std::string DAWAgent::executeDSL(const DSLResult& result) {
    if (result.hasError)
        return result.error;

    if (!interpreter_.execute(result.dsl.c_str())) {
        return "DSL execution error: " + std::string(interpreter_.getError()) +
               "\nDSL was: " + result.dsl;
    }

    auto results = interpreter_.getResults();
    if (results.isEmpty())
        return "Done.";

    return results.toStdString();
}

}  // namespace magda
