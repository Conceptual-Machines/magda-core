#pragma once

#include <juce_core/juce_core.h>
#include <juce_llm/juce_llm.h>

#include <atomic>

#include "agent_interface.hpp"
#include "compact_executor.hpp"
#include "compact_parser.hpp"
#include "dsl_interpreter.hpp"

namespace magda {

/**
 * @brief Concrete DAW agent that wires LLM → compact IR → execution.
 *
 * processMessage() flow:
 * 1. Build state snapshot of current tracks/clips
 * 2. Call LLM with compact system prompt
 * 3. Parse compact output into IR instructions
 * 4. Execute IR directly against TrackManager/ClipManager
 *
 * The DSL interpreter is kept for the interactive REPL; this agent
 * uses the compact IR path for lower latency and fewer tokens.
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

    /** Signal any in-flight HTTP request to cancel */
    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

    std::string processMessage(const std::string& message) override;
    void setMessageCallback(
        std::function<void(const std::string&, const std::string&)> callback) override;

    /** Result of the LLM call (HTTP), before execution */
    struct GenerateResult {
        std::string compactOutput;
        std::vector<Instruction> instructions;
        std::string error;
        bool hasError = false;
    };

    /** Step 1: Call LLM to generate compact instructions (background thread safe) */
    GenerateResult generate(const std::string& message);

    /** Step 2: Execute IR instructions (MUST be called on the message thread) */
    std::string execute(const GenerateResult& result);

    // --- Legacy DSL path (kept for REPL) ---

    struct DSLResult {
        std::string dsl;
        std::string error;
        bool hasError = false;
    };

    /** Generate DSL via the old grammar-constrained path */
    DSLResult generateDSL(const std::string& message);

    /** Execute DSL code via the interpreter */
    std::string executeDSL(const DSLResult& result);

  private:
    /** Get the compact system prompt with instruction set. */
    static const char* getCompactSystemPrompt();

    CompactParser parser_;
    CompactExecutor executor_;
    dsl::Interpreter interpreter_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shouldStop_{false};
    std::function<void(const std::string&, const std::string&)> messageCallback_;
};

}  // namespace magda
