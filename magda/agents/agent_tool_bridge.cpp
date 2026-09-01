#include "agent_tool_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

#include "../daw/api/remote_service.hpp"

namespace magda::agent {
namespace {

const remote::OperationDescriptor* resolvableOperation(const std::string& name) {
    const auto* operation = remote::OperationRegistry::instance().find(juce::String(name));
    if (operation == nullptr) {
        juce::Logger::writeToLog("Agent surface allowlist names an unknown operation: " + name);
        jassertfalse;
        return nullptr;
    }
    // Transport-scoped operations (subscriptions) are per-connection state and
    // an in-process agent has no connection; a surface listing one would hand
    // the model a tool nothing can execute.
    if (operation->transportScoped) {
        juce::Logger::writeToLog("Agent surface allowlist names a transport-scoped operation: " +
                                 name);
        jassertfalse;
        return nullptr;
    }
    return operation;
}

}  // namespace

std::vector<ToolDefinition> agentToolsForSurface(const AgentSurface& surface) {
    std::vector<ToolDefinition> tools;
    tools.reserve(surface.toolAllowlist.size());
    for (const auto& name : surface.toolAllowlist) {
        const auto* operation = resolvableOperation(name);
        if (operation == nullptr)
            continue;
        tools.push_back({.name = operation->name,
                         .description = operation->summary,
                         .inputSchema = operation->inputSchema.clone(),
                         .access = operation->access == remote::OperationAccess::Write
                                       ? ToolAccess::Mutation
                                       : ToolAccess::Read});
    }
    return tools;
}

remote::ScopeSet agentScopesForSurface(const AgentSurface& surface) {
    remote::ScopeSet scopes{remote::Scope::Read};
    for (const auto& name : surface.toolAllowlist) {
        const auto* operation = resolvableOperation(name);
        if (operation != nullptr)
            scopes.add(operation->requiredScope);
    }
    return scopes;
}

RemoteAgentToolExecutor::RemoteAgentToolExecutor(remote::RemoteApiService& service,
                                                 const AgentSurface& surface)
    : service_(service), surface_(surface), scopes_(agentScopesForSurface(surface)) {}

ToolResult RemoteAgentToolExecutor::execute(const ToolExecutionRequest& request,
                                            const CancellationToken& cancellation) {
    const auto failure = [&](juce::String code, juce::String message,
                             juce::var details = {}) -> ToolResult {
        return {.callId = request.call.id,
                .toolName = request.call.name,
                .success = false,
                .content = {},
                .error = ToolError{.code = std::move(code),
                                   .message = std::move(message),
                                   .details = std::move(details)},
                // The current revision, never something older: the runtime
                // treats a regressed revision as a broken executor.
                .projectRevision = service_.currentRevision(),
                .mutated = false};
    };

    const bool allowed = std::find(surface_.toolAllowlist.begin(), surface_.toolAllowlist.end(),
                                   request.call.name.toStdString()) != surface_.toolAllowlist.end();
    if (!allowed)
        return failure("tool_not_allowed", "Operation '" + request.call.name + "' is not in the " +
                                               surface_.name + " surface's allowlist");

    remote::RequestContext context;
    context.clientId = "agent:" + juce::String(surface_.name);
    context.clientName = "magda-agent";
    context.transport = "in-app";
    context.requestId = request.call.id;
    // The runtime threads the revision it last observed; passing it through
    // gives agent writes the same optimistic concurrency a remote client gets.
    // Zero means the run has observed nothing yet and expects nothing.
    if (request.expectedRevision != 0)
        context.expectedRevision = request.expectedRevision;
    context.scopes = scopes_;
    context.deadline = request.deadline;

    const auto revisionBefore = service_.currentRevision();

    // The service runs the completion inline when this is already the message
    // thread (or there is none), and posts it to the message thread otherwise —
    // so one dispatch-and-wait covers every calling context, and the inline
    // cases signal before the first wait. Shared state keeps the completion
    // valid even if this thread abandons the wait at the deadline.
    struct Pending {
        juce::WaitableEvent done;
        remote::Response response;
    };
    auto pending = std::make_shared<Pending>();
    service_.dispatch(request.call.name, request.call.arguments, context,
                      [pending](remote::Response completed) {
                          pending->response = std::move(completed);
                          pending->done.signal();
                      });

    constexpr int pollMs = 50;
    while (!pending->done.wait(pollMs)) {
        if (cancellation.isCancellationRequested())
            return failure("cancelled", "Run cancelled while the operation was in flight");
        if (std::chrono::steady_clock::now() >= request.deadline)
            return failure("time_limit", "Operation did not complete before the deadline");
    }
    const remote::Response response = std::move(pending->response);

    if (!response.ok) {
        juce::var details;
        if (!response.error.issues.empty()) {
            juce::Array<juce::var> issues;
            for (const auto& issue : response.error.issues) {
                auto* entry = new juce::DynamicObject();
                entry->setProperty("path", issue.path);
                entry->setProperty("code", issue.code);
                entry->setProperty("message", issue.message);
                issues.add(entry);
            }
            details = issues;
        }
        return failure(remote::toString(response.error.code), response.error.message, details);
    }

    return {.callId = request.call.id,
            .toolName = request.call.name,
            .success = true,
            .content = response.result,
            .error = {},
            .projectRevision = response.revision,
            .mutated = response.revision != revisionBefore};
}

}  // namespace magda::agent
