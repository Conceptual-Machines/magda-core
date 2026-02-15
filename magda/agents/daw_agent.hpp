#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

#include "agent_interface.hpp"
#include "dsl_interpreter.hpp"
#include "openai_client.hpp"

namespace magda {

/**
 * @brief Concrete DAW agent that wires OpenAI → DSL → TrackManager/ClipManager execution.
 *
 * processMessage() flow:
 * 1. Build state snapshot of current tracks/clips
 * 2. Call OpenAI with CFG grammar constraint
 * 3. Parse and execute returned DSL
 * 4. Return human-readable result
 */
class DAWAgent : public AgentInterface {
  public:
    DAWAgent();
    ~DAWAgent() override;

    // AgentInterface
    std::string getId() const override {
        return "daw-agent";
    }
    std::string getName() const override {
        return "DAW Agent";
    }
    std::string getType() const override {
        return "daw";
    }
    std::map<std::string, std::string> getCapabilities() const override;

    bool start() override;
    void stop() override;
    bool isRunning() const override {
        return running_.load();
    }

    std::string processMessage(const std::string& message) override;
    void setMessageCallback(
        std::function<void(const std::string&, const std::string&)> callback) override;

  private:
    OpenAIClient openai_;
    dsl::Interpreter interpreter_;
    std::atomic<bool> running_{false};
    std::function<void(const std::string&, const std::string&)> messageCallback_;
};

}  // namespace magda
