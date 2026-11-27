// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/LLMClientProvider.hpp"

using magda::Config;

namespace {

Config::AgentLLMConfig anthropic(const std::string& model) {
    Config::AgentLLMConfig config;
    config.provider = magda::provider::ANTHROPIC;
    config.model = model;
    config.apiKey = "test";
    return config;
}

}  // namespace

TEST_CASE("The tool loop asks a model that always thinks for its thinking summary",
          "[agents][llm-provider]") {
    CHECK(magda::toLLMProviderConfig(anthropic("claude-fable-5"), "tool-loop").thinkingDisplay ==
          "summarized");
    CHECK(magda::toLLMProviderConfig(anthropic("claude-opus-5-5"), "tool-loop").thinkingDisplay ==
          "summarized");
    CHECK(magda::toLLMProviderConfig(anthropic("claude-opus-4-8"), "tool-loop")
              .thinkingDisplay.isEmpty());
    CHECK(
        magda::toLLMProviderConfig(anthropic("claude-fable-5"), "chord").thinkingDisplay.isEmpty());
}
