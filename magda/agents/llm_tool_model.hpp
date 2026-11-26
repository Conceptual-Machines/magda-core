// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_llm/juce_llm.h>

#include <functional>

#include "agent_runtime.hpp"

namespace magda::agent {

/**
 * @brief `agent::Model` over an `llm::LLMClient`, via native tool calling.
 *
 * The provider-neutral surface juce_llm already carries — `Request::tools`,
 * `Response::toolCalls`, tool-result conversation turns — mapped onto the
 * runtime's types, so one adapter serves every configured provider (#2295).
 * No DSL, no grammar: the model calls the same named operations an MCP client
 * calls, and malformed arguments come back to it as the dispatcher's
 * validation errors rather than being repaired here.
 */
class LlmToolModel : public Model {
  public:
    /// Receives the model's text as it streams; false stops the request.
    using TextStream = std::function<bool(const juce::String&)>;
    /// Told the operation name when the model starts writing a call to it.
    using ToolCallStream = std::function<void(const juce::String&)>;

    explicit LlmToolModel(llm::LLMClient& client, float temperature = 0.1f, TextStream onText = {},
                          ToolCallStream onToolCall = {});

    ModelResponse generate(const ModelRequest& request,
                           const CancellationToken& cancellation) override;

  private:
    llm::LLMClient& client_;
    float temperature_;
    TextStream onText_;
    ToolCallStream onToolCall_;
};

}  // namespace magda::agent
