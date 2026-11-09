# Remote API Operations

Every operation the [Remote API](remote-api.md) offers, generated from MAGDA's operation
registry. The same catalogue is published as an [OpenRPC 1.3](https://spec.open-rpc.org/)
document, [`magda-openrpc.json`](magda-openrpc.json), for client generators and API tools. A
running MAGDA returns the same operations and schemas from `system.describe`.

Parameters go by name in `params`. The types below are a summary: the OpenRPC document carries
each parameter's and result's full JSON Schema, including ranges, enums and size limits. **Scope**
is the permission the named client needs. A *transport-scoped* method is carried out by the
WebSocket connection itself rather than the project, and has no MCP tool.

Any method can also fail with the errors listed at the end of this page.

<!-- openrpc:operations -->
