#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace magda {

/**
 * @brief Offline, on-device command model — natural language → MAGDA DSL.
 *
 * A tiny non-autoregressive intent+slots network (embedding → 3 dilated
 * conv1d + ReLU → masked mean-pool intent head + per-token BIO slot head),
 * ported to hand-written float32 inference (weights baked into
 * command_model_data.cpp). Runs in well under a millisecond with zero new
 * dependencies — the instant, free, private path alongside the LLM
 * CommandAgent. Emits the same DSL grammar (dsl_grammar.hpp) so its output
 * flows straight into the DSL interpreter → InstructionExecutor → UndoManager.
 *
 * Ported from the Python POC in prototypes/command-model-poc/ (issue #1827).
 */
class CommandModel {
  public:
    CommandModel();

    /** Natural language → canonical DSL. Returns "" on empty/unrecognised input. */
    std::string generate(const std::string& text) const;

    /** Perception output, exposed for tests/inspection. */
    struct Prediction {
        std::string intent;
        std::vector<std::string> tokens;  // surface tokens (identity-preserving)
        std::vector<std::string> tags;    // BIO tag per token
    };

    Prediction predict(const std::string& text) const;

  private:
    std::unordered_map<std::string, int> vocab_;
    int unkId_ = 1;
};

}  // namespace magda
