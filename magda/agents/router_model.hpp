#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace magda {

/**
 * @brief Offline, on-device console router — natural language → ConsoleIntent.
 *
 * A tiny classifier (embedding → 3 dilated conv1d + ReLU → masked mean-pool →
 * 7-way head) ported to hand-written float32 inference, with the weights baked
 * into router_model_data.cpp. Sub-millisecond, offline, free: with the command
 * model (#1827) behind role::COMMAND and this behind role::ROUTER, the whole
 * command path runs with no network at all.
 *
 * Deliberately a *separate* model from CommandModel rather than an extra head
 * on it. The command model classifies 38 DSL command intents; this answers the
 * coarser question of which agent handles the turn. Keeping the label spaces
 * apart is what stops a music request from being fed to the command model and
 * silently mangled into DSL.
 *
 * It classifies and commits — no confidence threshold, no LLM fallback. Fast
 * inference is deterministic classification over a fixed label set; open-ended
 * requests are the LLM's job by design, not a router failure to hedge against.
 *
 * Unlike CommandModel this tokenizer is multilingual (UTF-8, per-codepoint for
 * CJK), because the router runs on every console turn: an ASCII-only tokenizer
 * would send all non-Latin input to whatever the empty-input default is.
 *
 * Ported from the Python POC in prototypes/router-model-poc/ (issue #1843).
 */
class RouterModel {
  public:
    RouterModel();

    /** Natural language → ConsoleIntent token ("COMMAND", "MUSIC", ...).
        Returns "" for empty/untokenizable input, which callers treat as
        "no opinion" and resolve through the view's default. */
    std::string classify(const std::string& text) const;

    /** Surface tokens, exposed for tests/inspection. */
    std::vector<std::string> tokenize(const std::string& text) const;

  private:
    std::unordered_map<std::string, int> vocab_;
    int unkId_ = 1;
};

}  // namespace magda
