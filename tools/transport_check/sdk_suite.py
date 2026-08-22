"""The endpoint driven by the official MCP SDK, as a second, independent host.

#2059 asks for verification against real MCP hosts, on the grounds that MAGDA's
own tests can prove it answers correctly but not that a shipped host asks the
way it expects. The rest of this harness is a client written from
`docs/remote-api-mcp.md`, which shares that blind spot: it asks the way the
documentation says to.

`mcp` — the official Python SDK — does not. It validates every response against
the protocol schema for the version it negotiated, so it fails on shapes a
hand-written client would accept, which is the whole reason to run it.

Optional and skipped when absent. The SDK needs Python 3.10+ while the rest of
this harness runs on 3.9, so it cannot be a hard dependency:

    python3 -m venv .venv && .venv/bin/pip install -r tools/transport_check/requirements.txt
    .venv/bin/python tools/transport_check --mcp-sdk
"""

from __future__ import annotations

import asyncio
import importlib.util
from typing import Any

from report import Status

#: What the SDK calls itself to MAGDA. A separate identity from the
#: hand-written client's, so AI Settings -> Clients shows which host did what.
SDK_CLIENT_NAME = "magda-transport-check-sdk"


def available() -> tuple[bool, str]:
    """Whether the SDK can be used, and if not, what to do about it.

    `find_spec` rather than an import: the SDK pulls in pydantic and anyio, and
    a suite that is about to be skipped should not pay for that.
    """
    for module in ("mcp", "httpx2"):
        if importlib.util.find_spec(module) is None:
            return False, (
                f"the official MCP SDK is not installed ({module} is missing). "
                "It needs Python 3.10+; install it with "
                "`pip install -r tools/transport_check/requirements.txt` and run "
                "this harness with that interpreter."
            )
    return True, ""


class SdkSession:
    """One `mcp.Client` session, driven synchronously.

    The SDK is async and everything else here is not, so each check runs its
    own event loop. That is slower than sharing one, and it also means a check
    that wedges cannot wedge the others.
    """

    def __init__(self, url: str, token: str, mode: str = "auto") -> None:
        self.url = url
        self.token = token
        self.mode = mode

    async def _run(self, body) -> Any:
        import httpx2
        from mcp import Client, Implementation
        from mcp.client.streamable_http import streamable_http_client

        http_client = httpx2.AsyncClient(
            headers={"Authorization": f"Bearer {self.token}"}
        )
        transport = streamable_http_client(self.url, http_client=http_client)
        async with Client(
            transport,
            client_info=Implementation(name=SDK_CLIENT_NAME, version="1.0"),
            mode=self.mode,
        ) as client:
            return await body(client)

    def run(self, body) -> Any:
        return asyncio.run(self._run(body))


def run_sdk(context) -> None:
    """Checks driven through the official SDK rather than this harness's client."""
    from suites import SuiteRunner, ok, unclear

    runner = SuiteRunner(context, "mcp (official SDK)")

    ready, reason = available()
    if not ready:
        runner.skip("every check", reason)
        return
    if not context.record.has_mcp:
        runner.skip("every check", "the record carries no mcpUrl")
        return

    import mcp.types as types

    runner.report.note(f"SDK latest protocol: {types.LATEST_PROTOCOL_VERSION}")

    url = context.record.mcp_url or ""
    auto = SdkSession(url, context.record.token, mode="auto")
    legacy = SdkSession(url, context.record.token, mode="legacy")

    negotiated: list[str] = []

    def connect() -> tuple[Status, str, dict]:
        async def body(client):
            return client.protocol_version, client.server_info

        version, server = auto.run(body)
        negotiated.append(version or "")
        name = getattr(server, "name", None)
        detail = f"negotiated {version}, server {name}"
        if version != types.LATEST_PROTOCOL_VERSION:
            return unclear(
                detail + f" — the SDK supports {types.LATEST_PROTOCOL_VERSION} and "
                "settled for less, which means its modern probe did not take. "
                "server/discover is what that probe reads.",
                negotiated=version,
            )
        return ok(detail, negotiated=version)

    runner.check("the SDK connects and negotiates a version", connect)

    # -- the catalogue, in whatever era `auto` settled on -------------------

    def list_tools() -> tuple[Status, str, dict]:
        async def body(client):
            return await client.list_tools()

        result = auto.run(body)
        names = [tool.name for tool in result.tools]
        assert "tracks.list" in names, f"tracks.list missing from {names[:8]}"
        return ok(f"{len(names)} tools accepted by the SDK's schema validation")

    runner.check("tools/list validates against the negotiated schema", list_tools)

    def call_tool() -> tuple[Status, str, dict]:
        async def body(client):
            return await client.call_tool("tracks.list", {})

        result = auto.run(body)
        assert result.is_error is False, f"tools/call reported an error: {result}"
        assert result.content, "no content in the result"
        return ok("a tool call round-tripped through the SDK")

    runner.check("tools/call validates against the negotiated schema", call_tool)

    def read_resource() -> tuple[Status, str, dict]:
        async def body(client):
            return await client.read_resource("magda://tracks")

        result = auto.run(body)
        assert result.contents, "no contents returned"
        return ok("a resource read round-tripped through the SDK")

    runner.check("resources/read validates against the negotiated schema", read_resource)

    def list_resources() -> tuple[Status, str, dict]:
        async def body(client):
            listed = await client.list_resources()
            templates = await client.list_resource_templates()
            return listed, templates

        listed, templates = auto.run(body)
        return ok(
            f"{len(listed.resources)} resources, "
            f"{len(templates.resource_templates)} templates"
        )

    runner.check("the resource catalogue validates", list_resources)

    # -- the legacy era on its own, which has no cache directives ----------

    def legacy_session() -> tuple[Status, str, dict]:
        async def body(client):
            result = await client.list_tools()
            return client.protocol_version, len(result.tools)

        version, count = legacy.run(body)
        return ok(f"negotiated {version}, {count} tools", negotiated=version)

    runner.check("the SDK works when forced to the legacy era", legacy_session)
