#pragma once

#include <vector>

#include "../daw/api/remote_scopes.hpp"
#include "../daw/core/ConsoleRouting.hpp"
#include "agent_runtime.hpp"

namespace magda::remote {
class RemoteApiService;
}

namespace magda::agent {

/**
 * @brief The bridge `agent_runtime.hpp` demands: agents act through the same
 *        operation contract as every remote client (#2295).
 *
 * `ToolExecutor`'s contract says production adapters must delegate to
 * `RemoteApiService`, and until this existed the only implementation was a test
 * fake — in-app agents held a raw `MagdaApi&` and bypassed validation, scopes,
 * and the audit log entirely. This file is the missing production half:
 *
 * - `agentToolsForSurface` turns a `ConsoleRouting` surface allowlist into the
 *   tool set an `AgentDefinition` carries, resolved against
 *   `remote::OperationRegistry`. The allowlist stops being prompt metadata and
 *   becomes the agent's actual capability boundary: the runtime refuses calls
 *   outside its definition, so a surface's agent structurally cannot reach an
 *   operation the surface does not declare.
 * - `RemoteAgentToolExecutor` executes each call through the service, which is
 *   where schema validation, scope enforcement, revision tracking, undo
 *   grouping, and the audit record already live — once, for every caller.
 */

/**
 * @brief The surface's allowlist as executable tool definitions.
 *
 * One `ToolDefinition` per allowlisted operation, carrying the registry's
 * summary and input schema so the model sees the same contract an MCP client
 * does. Transport-scoped operations and names the registry does not know are
 * skipped and logged — `invalidSurfaceTools` exists to keep the lists in sync,
 * and a test pins every registered surface to resolve completely.
 */
std::vector<ToolDefinition> agentToolsForSurface(const AgentSurface& surface);

/**
 * @brief The scopes the surface's allowlist requires, plus `read`.
 *
 * Computed from the resolved operations' `requiredScope` rather than granted
 * wholesale, so a surface whose allowlist holds no session operation cannot
 * launch clips even if a handler is misrouted.
 */
remote::ScopeSet agentScopesForSurface(const AgentSurface& surface);

/**
 * @brief `ToolExecutor` over `RemoteApiService`.
 *
 * Threading follows the service's rules: the service runs the completion
 * inline when called on the message thread (or when there is none), and posts
 * it back to the message thread when called from an agent worker thread — so
 * this dispatches and waits in every calling context, honouring the request
 * deadline and the cancellation token.
 *
 * Enforces the surface allowlist a second time at dispatch, independently of
 * the definition the runtime checked — a definition built by hand with an
 * extra tool still cannot reach past the surface.
 */
class RemoteAgentToolExecutor : public ToolExecutor {
  public:
    RemoteAgentToolExecutor(remote::RemoteApiService& service, const AgentSurface& surface);

    ToolResult execute(const ToolExecutionRequest& request,
                       const CancellationToken& cancellation) override;

  private:
    remote::RemoteApiService& service_;
    /// Points into `registeredAgentSurfaces()`, whose storage lives for the
    /// process, so holding a reference is safe.
    const AgentSurface& surface_;
    remote::ScopeSet scopes_;
};

}  // namespace magda::agent
