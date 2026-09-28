#include "llm_tool_model.hpp"

namespace magda::agent {
namespace {

llm::Message toLlmMessage(const ConversationTurn& turn) {
    llm::Message message;
    switch (turn.role) {
        case ConversationRole::User:
            message.role = "user";
            message.content = turn.text;
            break;
        case ConversationRole::Assistant:
            message.role = "assistant";
            message.content = turn.text;
            for (const auto& call : turn.toolCalls) {
                llm::ToolCall llmCall;
                llmCall.id = call.id;
                llmCall.name = call.name;
                llmCall.arguments = call.arguments;
                message.toolCalls.push_back(std::move(llmCall));
            }
            break;
        case ConversationRole::Tool: {
            message.role = "tool";
            if (turn.toolResult.has_value()) {
                const auto& result = *turn.toolResult;
                const bool isError = result.error.has_value();
                message.toolResults.emplace_back(
                    result.callId, result.toolName,
                    isError ? juce::var(result.error->code + ": " + result.error->message)
                            : result.content,
                    isError, isError ? result.error->message : juce::String());
            }
            break;
        }
    }
    return message;
}

}  // namespace

LlmToolModel::LlmToolModel(llm::LLMClient& client, float temperature)
    : client_(client), temperature_(temperature) {}

ModelResponse LlmToolModel::generate(const ModelRequest& request,
                                     const CancellationToken& cancellation) {
    if (cancellation.isCancellationRequested())
        return {.success = false, .error = "cancelled"};

    llm::Request llmRequest;
    llmRequest.systemPrompt = request.systemPrompt;
    if (!request.baselineContext.isVoid()) {
        llmRequest.systemPrompt +=
            "\n\n[Context]\n" + juce::JSON::toString(request.baselineContext, true);
    }
    llmRequest.temperature = temperature_;

    llmRequest.tools.reserve(request.tools.size());
    for (const auto& tool : request.tools) {
        auto* annotations = new juce::DynamicObject();
        annotations->setProperty("readOnlyHint", tool.access == ToolAccess::Read);
        llmRequest.tools.emplace_back(tool.name, tool.description, tool.inputSchema.clone(),
                                      juce::var(annotations));
    }

    // The runtime appends the user turn to the conversation; providers want the
    // current user turn separated out, so a trailing user turn becomes
    // `userMessage` and everything before it becomes history. Mid-run
    // iterations end on tool results instead, which stay in `messages` as the
    // pending turns the provider reads.
    auto conversation = request.conversation;
    if (!conversation.empty() && conversation.back().role == ConversationRole::User) {
        llmRequest.userMessage = conversation.back().text;
        conversation.pop_back();
    }
    llmRequest.messages.reserve(conversation.size());
    for (const auto& turn : conversation)
        llmRequest.messages.push_back(toLlmMessage(turn));

    const auto response = client_.sendRequest(llmRequest);
    if (cancellation.isCancellationRequested())
        return {.success = false, .error = "cancelled"};

    ModelResponse mapped;
    mapped.success = response.success;
    mapped.error = response.error;
    mapped.text = response.text;
    for (const auto& call : response.toolCalls) {
        // A call whose arguments failed to parse keeps its name and a void
        // payload: dispatch-side schema validation turns that into an error
        // the model can read and correct, which beats repairing JSON here.
        mapped.toolCalls.push_back({.id = call.id, .name = call.name, .arguments = call.arguments});
    }
    if (response.totalTokens >= 0)
        mapped.tokensUsed = static_cast<std::size_t>(response.totalTokens);
    else if (response.outputTokens >= 0)
        mapped.tokensUsed = static_cast<std::size_t>(response.outputTokens);
    return mapped;
}

}  // namespace magda::agent
