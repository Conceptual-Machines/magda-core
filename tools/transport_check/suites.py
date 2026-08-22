"""The checks themselves, grouped by the surface each one exercises.

Every suite takes a `Context` and reports through a `Report`. Suites never
raise past their own boundary: a transport that is entirely down should fail
its own checks and leave the others to run, because "MCP is broken and OSC is
fine" is a more useful thing to learn in one run than a traceback.

Default runs are non-mutating. The write checks are behind `--write` because
this points at whatever project the user happens to have open, and a harness
that silently added a track to real work would be a bad trade for the coverage.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import clients
import discovery
from clients import (
    LEGACY_PROTOCOL,
    Reply,
    MAGDA_META_REVISION,
    MODERN_PROTOCOL,
    McpBridgeClient,
    McpHttpClient,
    OscClient,
    WsClient,
)
from report import Check, Report, Status, timed


@dataclass
class Context:
    record: discovery.Record
    report: Report
    write: bool = False
    stream_window: float = 3.0
    osc_send_port: int = 9000
    osc_feedback_port: int = 9001
    bridge_path: str | None = None
    data_dir: Path | None = None
    timeout: float = 10.0

    def ws(self, client_name: str = clients.CLIENT_NAME, **kwargs: Any) -> WsClient:
        return WsClient(
            "127.0.0.1", self.record.port, self.record.token,
            client_name=client_name, timeout=self.timeout, **kwargs
        )

    #: One client per identity, reused across suites. The rate limit is a
    #: single bucket for the endpoint, so pacing only works if every request
    #: this harness makes is spaced against the last one — a fresh client per
    #: suite would each think it had the whole budget.
    _mcp_clients: dict = field(default_factory=dict, repr=False)

    def mcp(self, client_name: str = clients.CLIENT_NAME) -> McpHttpClient:
        if client_name not in self._mcp_clients:
            origin, path = self.record.mcp_origin_and_path()
            self._mcp_clients[client_name] = McpHttpClient(
                origin, path, self.record.mcp_token, client_name, timeout=self.timeout
            )
        return self._mcp_clients[client_name]

    def throttled(self) -> int:
        return sum(client.throttled for client in self._mcp_clients.values())


class SuiteRunner:
    """Wraps each check so one failure never takes the run down with it."""

    def __init__(self, context: Context, suite: str) -> None:
        self.context = context
        self.suite = suite
        self.report = context.report

    def check(self, name: str, fn: Callable[[], tuple[Status, str] | tuple[Status, str, dict]]) -> Check:
        with timed() as clock:
            try:
                outcome = fn()
                status, detail = outcome[0], outcome[1]
                data = outcome[2] if len(outcome) > 2 else {}
            except AssertionError as exc:
                status, detail, data = Status.FAIL, str(exc) or "assertion failed", {}
            except Exception as exc:  # noqa: BLE001 - a harness must not crash
                status, detail, data = Status.FAIL, describe_exception(exc), {}
        return self.report.add(
            Check(self.suite, name, status, detail, clock.ms, data)
        )

    def skip(self, name: str, reason: str) -> Check:
        return self.report.add(Check(self.suite, name, Status.SKIP, reason))


def describe_exception(exc: BaseException, depth: int = 0) -> str:
    """A one-line cause, digging through exception groups to find it.

    An async client raises an `ExceptionGroup` whose message is "unhandled
    errors in a TaskGroup (1 sub-exception)" — true, and useless. What the
    reader needs is the schema-validation failure three levels down, so this
    walks to it. Chained causes are followed for the same reason.
    """
    nested = getattr(exc, "exceptions", None)
    if nested and depth < 6:
        inner = [describe_exception(item, depth + 1) for item in nested]
        return inner[0] if len(inner) == 1 else " | ".join(inner)

    # A schema validation failure is the SDK suite's whole reason to exist, and
    # pydantic's default rendering buries the one useful fact — which fields
    # were wrong — under repeated URLs and truncated input dumps.
    errors = getattr(exc, "errors", None)
    if callable(errors):
        try:
            problems = []
            for item in errors():
                where = ".".join(str(part) for part in item.get("loc", ())) or "(root)"
                problems.append(f"{where}: {item.get('msg', 'invalid')}")
            if problems:
                model = str(exc).split("for", 1)[-1].split("\n", 1)[0].strip()
                head = f"{type(exc).__name__} for {model}" if model else type(exc).__name__
                return f"{head} — " + "; ".join(problems[:6])
        except Exception:  # noqa: BLE001 - fall through to the plain rendering
            pass

    text = str(exc).strip().replace("\n", " ")
    if len(text) > 400:
        text = text[:400] + "…"
    described = f"{type(exc).__name__}: {text}" if text else type(exc).__name__

    cause = exc.__cause__ or exc.__context__
    if not text and cause is not None and depth < 6:
        return describe_exception(cause, depth + 1)
    return described


def ok(detail: str = "", **data: Any) -> tuple[Status, str, dict]:
    return Status.PASS, detail, data


def fail(detail: str, **data: Any) -> tuple[Status, str, dict]:
    return Status.FAIL, detail, data


def unclear(detail: str, **data: Any) -> tuple[Status, str, dict]:
    return Status.INCONCLUSIVE, detail, data


# ---------------------------------------------------------------------------
# Discovery and packaging
# ---------------------------------------------------------------------------


def executable_for_pid(pid: int) -> Path | None:
    """The binary behind a running pid, best effort, per platform."""
    try:
        if sys.platform == "linux":
            return Path(os.readlink(f"/proc/{pid}/exe"))
        if sys.platform == "darwin":
            out = subprocess.run(
                ["ps", "-p", str(pid), "-o", "comm="],
                capture_output=True, text=True, timeout=5,
            )
            path = out.stdout.strip()
            return Path(path) if path else None
        if sys.platform == "win32":
            out = subprocess.run(
                ["wmic", "process", "where", f"processid={pid}", "get", "ExecutablePath"],
                capture_output=True, text=True, timeout=10,
            )
            lines = [l.strip() for l in out.stdout.splitlines() if l.strip()]
            return Path(lines[1]) if len(lines) > 1 else None
    except (OSError, subprocess.SubprocessError, IndexError):
        return None
    return None


def bridge_name() -> str:
    return "magda-mcp.exe" if sys.platform == "win32" else "magda-mcp"


def run_discovery(context: Context, others: list[discovery.Record]) -> None:
    runner = SuiteRunner(context, "discovery")
    record = context.record

    runner.check(
        "discovery record names a live process",
        lambda: ok(f"{record.path.name}, pid {record.pid}", pid=record.pid),
    )

    def perms() -> tuple[Status, str, dict]:
        owner_only = record.owner_only()
        if owner_only is None:
            return unclear("Windows: the file inherits the per-user directory ACL, not a mode")
        mode = oct(record.path.stat().st_mode & 0o777)
        if not owner_only:
            return fail(f"token file is {mode}, expected 0o600", mode=mode)
        return ok(f"mode {mode}", mode=mode)

    runner.check("token file is owner-only", perms)

    runner.check(
        "record carries a WebSocket URL",
        lambda: ok(record.url, port=record.port) if record.url and record.port
        else fail("record has no url/port"),
    )

    runner.check(
        "record carries an MCP URL",
        lambda: ok(record.mcp_url or "", mcpPort=record.mcp_port) if record.has_mcp
        else fail("no mcpUrl: MAGDA came up but its MCP listener did not"),
    )

    if others:
        runner.check(
            "exactly one MAGDA instance is running",
            lambda: unclear(
                f"{len(others) + 1} live instances; testing pid {record.pid}. "
                f"Others: {', '.join(str(o.pid) for o in others)}"
            ),
        )

    # -- packaging: is the bridge staged beside the executable? ------------

    def staged() -> tuple[Status, str, dict]:
        exe = executable_for_pid(record.pid)
        if exe is None:
            return unclear(f"could not resolve the executable behind pid {record.pid}")
        if "/tmp/.mount_" in str(exe) or ".mount_" in str(exe):
            return unclear(
                f"running from an AppImage ({exe}); the settings page is expected to "
                "show a placeholder here and publish magda-mcp as a separate asset"
            )
        candidate = exe.parent / bridge_name()
        if not candidate.exists():
            return fail(f"{candidate} does not exist", expected=str(candidate))
        if sys.platform != "win32" and not os.access(candidate, os.X_OK):
            return fail(f"{candidate} is not executable")
        return ok(str(candidate), path=str(candidate))

    runner.check("magda-mcp is staged beside the MAGDA executable", staged)


# ---------------------------------------------------------------------------
# WebSocket
# ---------------------------------------------------------------------------


def run_websocket(context: Context) -> None:
    runner = SuiteRunner(context, "websocket")
    # Since #2142 the two transports switch on separately, so a record with no
    # WebSocket entry is a supported configuration rather than a broken run —
    # the mirror image of the `has_mcp` guard the MCP suites have always had.
    if not context.record.has_websocket:
        runner.skip("every check", "the record carries no WebSocket url")
        return
    from wire import HandshakeError

    def bad_token() -> tuple[Status, str, dict]:
        client = WsClient("127.0.0.1", context.record.port, "not-the-token", timeout=context.timeout)
        try:
            client.connect()
        except HandshakeError as exc:
            if exc.status == 401:
                return ok("401 before the upgrade")
            return fail(f"expected 401, got {exc.status}")
        finally:
            client.close()
        return fail("a bad bearer token was accepted")

    runner.check("a bad bearer token is refused with 401", bad_token)

    def bad_origin() -> tuple[Status, str, dict]:
        client = WsClient(
            "127.0.0.1", context.record.port, context.record.token,
            timeout=context.timeout, origin="http://evil.example",
        )
        try:
            client.connect()
        except HandshakeError as exc:
            if exc.status == 403:
                return ok("403 before the upgrade")
            return fail(f"expected 403, got {exc.status}")
        finally:
            client.close()
        return fail("an unlisted Origin was accepted (DNS-rebinding defence is open)")

    runner.check("an unlisted Origin is refused with 403", bad_origin)

    try:
        with context.ws() as ws:
            runner.check(
                "connect and describe the API surface",
                lambda: _ws_describe(ws),
            )
            runner.check("project.get returns a revision", lambda: _ws_project(ws))
            runner.check("tracks.list answers", lambda: _ws_tracks(ws))
            runner.check("an unknown method is refused", lambda: _ws_unknown(ws))
            runner.check(
                "subscriptions.subscribe returns a snapshot in the reply",
                lambda: _ws_subscribe(ws),
            )
            runner.check(
                f"the connection stays up and pushes for {context.stream_window:.0f}s",
                lambda: _ws_stream(ws, context.stream_window),
            )
            if context.write:
                runner.check("a write advances the revision, and reverts", lambda: _ws_write(ws))
                runner.check("a stale expectedRevision is refused", lambda: _ws_stale(ws))
            else:
                runner.skip("a write advances the revision", "needs --write")
                runner.skip("a stale expectedRevision is refused", "needs --write")
    except Exception as exc:  # noqa: BLE001
        message = f"{type(exc).__name__}: {exc}"
        runner.check("connect to /rpc", lambda: fail(message))


def _ws_describe(ws: WsClient) -> tuple[Status, str, dict]:
    reply = ws.call("system.describe")
    assert reply.ok, f"system.describe failed: {reply.error}"
    result = reply.result or {}
    operations = result.get("operations") or result.get("ops") or []
    meta = reply.raw.get("meta") or {}
    assert meta.get("apiVersion"), "reply carried no meta.apiVersion"
    return ok(
        f"apiVersion {meta.get('apiVersion')}, {len(operations)} operations",
        apiVersion=meta.get("apiVersion"), operations=len(operations),
    )


def _ws_project(ws: WsClient) -> tuple[Status, str, dict]:
    reply = ws.call("project.get")
    assert reply.ok, f"project.get failed: {reply.error}"
    assert reply.revision is not None, "reply carried no meta.revision"
    tempo = (reply.result or {}).get("tempo")
    return ok(f"revision {reply.revision}, tempo {tempo}", revision=reply.revision)


def _ws_tracks(ws: WsClient) -> tuple[Status, str, dict]:
    reply = ws.call("tracks.list")
    assert reply.ok, f"tracks.list failed: {reply.error}"
    result = reply.result
    tracks = result.get("tracks") if isinstance(result, dict) else result
    assert isinstance(tracks, list), f"expected a list of tracks, got {type(result).__name__}"
    return ok(f"{len(tracks)} tracks", tracks=len(tracks))


def _ws_unknown(ws: WsClient) -> tuple[Status, str, dict]:
    reply = ws.call("tracks.doesNotExist")
    assert not reply.ok, "an unknown operation was accepted"
    return ok(f"code {reply.code}", code=reply.code)


def _ws_subscribe(ws: WsClient) -> tuple[Status, str, dict]:
    reply = ws.call("subscriptions.subscribe", {"topics": ["project", "transport", "tracks"]})
    assert reply.ok, f"subscribe failed: {reply.error}"
    snapshots = (reply.result or {}).get("snapshots") or []
    assert snapshots, "subscribe returned no snapshots, so a client would not know what it watches"
    kinds = [s.get("type") for s in snapshots]
    return ok(f"{len(snapshots)} snapshots {kinds}", snapshots=len(snapshots))


def _ws_stream(ws: WsClient, window: float) -> tuple[Status, str, dict]:
    ws.call("subscriptions.subscribe", {"topics": ["playhead", "meters"]})
    events = ws.collect_notifications(window)
    if not events:
        return unclear(
            "no events arrived. Expected when the transport is stopped and no audio "
            "device is open; the subscription itself was accepted."
        )
    methods = sorted({e.get("method", "?") for e in events})
    return ok(f"{len(events)} events, methods {methods}", events=len(events))


def _ws_write(ws: WsClient) -> tuple[Status, str, dict]:
    before = ws.call("project.get")
    assert before.ok, f"project.get failed: {before.error}"
    original = (before.result or {}).get("tempo")
    assert isinstance(original, (int, float)), f"no tempo to change: {before.result}"
    target = round(float(original) + 1.0, 3)
    try:
        written = ws.call("project.setTempo", {"tempo": target})
        if not written.ok and _is_permission_denial(written):
            return unclear(
                f"'{clients.CLIENT_NAME}' has not been granted 'edit'. That is the "
                "correct default — every new client is read-only — but it means this "
                "check cannot run. Tick edit and transport for it in "
                "Connections -> Clients."
            )
        assert written.ok, f"setTempo failed: {written.error}"
        assert written.revision is not None and before.revision is not None
        assert written.revision > before.revision, (
            f"revision did not advance: {before.revision} -> {written.revision}"
        )
        after = ws.call("project.get")
        assert abs(float((after.result or {}).get("tempo", 0)) - target) < 1e-3, (
            f"tempo reads back as {(after.result or {}).get('tempo')}, expected {target}"
        )
        return ok(
            f"tempo {original} -> {target}, revision {before.revision} -> {written.revision}",
            revision=written.revision,
        )
    finally:
        ws.call("project.setTempo", {"tempo": float(original)})


#: The WebSocket transport's code for "you have not been granted this".
WS_PERMISSION_DENIED = -32006


def _is_permission_denial(reply: "Reply") -> bool:
    error = reply.error or {}
    return (
        error.get("code") == WS_PERMISSION_DENIED
        or (error.get("data") or {}).get("code") == "permission_denied"
    )


def _ws_stale(ws: WsClient) -> tuple[Status, str, dict]:
    """A stale `expectedRevision` must lose — and for that reason.

    An ungranted client is refused every write, so "the write did not happen"
    is true here whether or not optimistic concurrency works at all. Checking
    only that it failed would go green on a MAGDA that had never implemented
    `expectedRevision`, which is the opposite of what this exists to establish.
    """
    current = ws.call("project.get")
    assert current.revision is not None, "no revision to be stale against"
    tempo = float((current.result or {}).get("tempo", 120.0))
    stale = 1 if current.revision != 1 else 2
    reply = ws.call("project.setTempo", {"tempo": tempo}, meta={"expectedRevision": stale})
    if reply.ok:
        return fail(
            f"a write against revision {stale} was applied while the project was at "
            f"{current.revision}, so nothing is enforcing expectedRevision"
        )
    if _is_permission_denial(reply):
        return unclear(
            f"refused, but for want of the 'edit' permission rather than the stale "
            f"revision — grant '{clients.CLIENT_NAME}' edit in Connections -> Clients "
            "for this to check what it is meant to",
            code=reply.code,
        )
    return ok(f"refused with code {reply.code}", code=reply.code)


# ---------------------------------------------------------------------------
# MCP, modern era
# ---------------------------------------------------------------------------


def run_mcp_modern(context: Context) -> None:
    runner = SuiteRunner(context, "mcp (modern, stateless)")
    if not context.record.has_mcp:
        runner.skip("every check", "the record carries no mcpUrl")
        return
    mcp = context.mcp()

    def bad_token() -> tuple[Status, str, dict]:
        origin, path = context.record.mcp_origin_and_path()
        wrong = McpHttpClient(origin, path, "not-the-token", timeout=context.timeout)
        reply = wrong.modern("tools/list")
        assert reply.status == 401, f"expected 401, got {reply.status}: {reply.body[:200]}"
        return ok("401")

    runner.check("a bad bearer token is refused with 401", bad_token)

    def bad_origin() -> tuple[Status, str, dict]:
        reply = mcp.modern(
            "tools/list", header_overrides={"Origin": "http://evil.example"}
        )
        assert reply.status == 403, (
            f"expected 403, got {reply.status}. Validating Origin is a MUST in the "
            "transport spec and is what stops a page the user merely visited from "
            "reaching this listener by DNS rebinding."
        )
        return ok("403")

    runner.check("an unlisted Origin is refused with 403", bad_origin)

    def discover() -> tuple[Status, str, dict]:
        reply = mcp.modern("server/discover")
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        result = reply.json().get("result") or {}
        versions = result.get("supportedVersions") or []
        assert MODERN_PROTOCOL in versions, f"supportedVersions {versions} lacks {MODERN_PROTOCOL}"
        return ok(f"supports {versions}", versions=versions)

    runner.check("server/discover advertises the supported versions", discover)

    tool_names: list[str] = []

    def tools_list() -> tuple[Status, str, dict]:
        reply = mcp.modern("tools/list")
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        tools = (reply.json().get("result") or {}).get("tools") or []
        assert tools, "tools/list returned nothing"
        tool_names.extend(t["name"] for t in tools)
        assert "tracks.list" in tool_names, f"tracks.list missing from {tool_names[:10]}"
        missing_schema = [t["name"] for t in tools if not t.get("inputSchema")]
        assert not missing_schema, f"tools without an inputSchema: {missing_schema[:5]}"
        return ok(f"{len(tools)} tools, all with an inputSchema", tools=len(tools))

    runner.check("tools/list projects the operation registry", tools_list)

    call_text: list[str] = []

    def tools_call() -> tuple[Status, str, dict]:
        reply = mcp.modern("tools/call", {"name": "tracks.list", "arguments": {}})
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        result = reply.json().get("result") or {}
        assert result.get("isError") is False, f"tools/call reported an error: {result}"
        assert "structuredContent" in result, "no structuredContent in the result"
        content = result.get("content") or []
        assert content and content[0].get("type") == "text", "no text content block"
        call_text.append(content[0]["text"])
        revision = (result.get("_meta") or {}).get(MAGDA_META_REVISION)
        assert revision is not None, f"_meta carried no {MAGDA_META_REVISION}"
        return ok(f"revision {revision}", revision=revision)

    runner.check("tools/call returns structured content and a revision", tools_call)

    def _text_pair(tool: str, uri: str) -> tuple[str, str, dict]:
        called = mcp.modern("tools/call", {"name": tool, "arguments": {}})
        assert called.status == 200, f"tools/call {tool}: HTTP {called.status}"
        call_result = called.json().get("result") or {}
        assert call_result.get("isError") is False, f"tools/call {tool} failed: {call_result}"
        read = mcp.modern("resources/read", {"uri": uri})
        assert read.status == 200, f"resources/read {uri}: HTTP {read.status}"
        contents = (read.json().get("result") or {}).get("contents") or []
        assert contents, f"resources/read {uri} returned no contents"
        assert contents[0].get("mimeType") == "application/json", contents[0].get("mimeType")
        return (call_result.get("content") or [{}])[0].get("text", ""), contents[0].get("text", ""), call_result

    def object_parity() -> tuple[Status, str, dict]:
        call_text, read_text, result = _text_pair("project.get", "magda://project/current")
        if call_text == read_text:
            return ok("byte for byte identical, as documented")
        if json.loads(call_text) == json.loads(read_text):
            return unclear("equal as JSON but not byte for byte, which the docs promise")
        return fail(
            "resources/read and tools/call disagree for the same operation",
            call=call_text[:200], read=read_text[:200],
        )

    runner.check(
        "resources/read matches tools/call for an object-valued operation", object_parity
    )

    def array_parity() -> tuple[Status, str, dict]:
        """The case the in-tree parity test does not reach.

        `structuredContent` is typed as an object, so an array-valued
        operation is wrapped as `{"items": [...]}` on the tool surface.
        `resources/read` has no such constraint and returns the bare array, so
        the two surfaces carry the same data in two shapes — which is not the
        unqualified byte-for-byte equivalence docs/remote-api-mcp.md promises.
        """
        call_text, read_text, result = _text_pair("tracks.list", "magda://tracks")
        if call_text == read_text:
            return ok("identical — the tool surface is not wrapping this one")

        structured = result.get("structuredContent")
        wrapped = isinstance(structured, dict) and set(structured) == {"items"}
        if wrapped and structured["items"] == json.loads(read_text):
            return ok(
                "tools/call wraps the array as {\"items\": [...]} and resources/read "
                "returns it bare — same data, two shapes. Deliberate (remote_mcp.cpp: "
                "structuredContent must be an object), but docs/remote-api-mcp.md "
                "promises byte-for-byte equality with no carve-out for this.",
                call=call_text[:120], read=read_text[:120],
            )
        return fail(
            "an array-valued operation disagrees across the two surfaces, and not "
            "by the documented {items} wrapper either",
            call=call_text[:200], read=read_text[:200],
        )

    runner.check(
        "an array-valued operation differs only by the documented wrapper", array_parity
    )

    def templates() -> tuple[Status, str, dict]:
        reply = mcp.modern("resources/templates/list")
        assert reply.status == 200, f"HTTP {reply.status}"
        items = (reply.json().get("result") or {}).get("resourceTemplates") or []
        assert items, "no resource templates advertised"
        return ok(f"{len(items)} templates", templates=len(items))

    runner.check("resources/templates/list answers", templates)

    # -- the rules a dual-era client depends on ---------------------------

    def no_meta() -> tuple[Status, str, dict]:
        reply = mcp.modern("tools/list", omit_meta=True)
        assert reply.status == 400, f"expected 400, got {reply.status}"
        error = reply.json().get("error") or {}
        assert error.get("code") == -32602, f"expected -32602, got {error.get('code')}"
        return ok("400 with -32602, which is what tells a dual-era client where it is")

    runner.check("a request with no _meta protocol version is refused", no_meta)

    def no_capabilities() -> tuple[Status, str, dict]:
        # Built by hand: `modern()` always supplies clientCapabilities, and
        # leaving it out is the whole of what this asserts.
        params = {"_meta": {
            clients.META_PROTOCOL_VERSION: MODERN_PROTOCOL,
            clients.META_CLIENT_INFO: {"name": mcp.client_name, "version": "1.0"},
        }}
        body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": params})
        headers = mcp.base_headers()
        headers["MCP-Protocol-Version"] = MODERN_PROTOCOL
        headers["Mcp-Method"] = "tools/list"
        reply = mcp.request("tools/list", body, headers)
        assert reply.status == 400, f"expected 400, got {reply.status}"
        assert (reply.json().get("error") or {}).get("code") == -32602
        return ok("400 with -32602")

    runner.check("a request with no clientCapabilities is refused", no_capabilities)

    def header_mismatch() -> tuple[Status, str, dict]:
        reply = mcp.modern("tools/list", header_overrides={"Mcp-Method": "tools/call"})
        assert reply.status == 400, f"expected 400, got {reply.status}"
        code = (reply.json().get("error") or {}).get("code")
        assert code == -32020, f"expected -32020, got {code}"
        return ok("400 with -32020")

    runner.check("a Mcp-Method header disagreeing with the body is refused", header_mismatch)

    def missing_name_header() -> tuple[Status, str, dict]:
        reply = mcp.modern(
            "tools/call", {"name": "tracks.list", "arguments": {}},
            header_overrides={"Mcp-Name": None},
        )
        assert reply.status == 400, f"expected 400, got {reply.status}"
        code = (reply.json().get("error") or {}).get("code")
        assert code == -32020, f"expected -32020, got {code}"
        return ok("400 with -32020")

    runner.check("tools/call without a Mcp-Name header is refused", missing_name_header)

    def wrong_name_header() -> tuple[Status, str, dict]:
        reply = mcp.modern(
            "tools/call", {"name": "tracks.list", "arguments": {}},
            header_overrides={"Mcp-Name": "tracks.create"},
        )
        assert reply.status == 400, (
            f"expected 400, got {reply.status}. A proxy is allowed to route on this "
            "header without parsing the body, so a server that executed the body "
            "while an intermediary had decided on the header would be two components "
            "acting on two different requests."
        )
        code = (reply.json().get("error") or {}).get("code")
        assert code == -32020, f"expected -32020, got {code}"
        return ok("400 with -32020")

    runner.check("a Mcp-Name header naming a different tool is refused", wrong_name_header)

    def base64_name() -> tuple[Status, str, dict]:
        import base64 as b64
        encoded = "=?base64?" + b64.b64encode(b"tracks.list").decode() + "?="
        reply = mcp.modern(
            "tools/call", {"name": "tracks.list", "arguments": {}},
            header_overrides={"Mcp-Name": encoded},
        )
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        return ok("a base64-wrapped Mcp-Name decodes and matches")

    runner.check("a base64-wrapped Mcp-Name is decoded before comparison", base64_name)

    def unknown_method() -> tuple[Status, str, dict]:
        reply = mcp.modern("tools/nope")
        assert reply.status == 404, f"expected 404, got {reply.status}"
        return ok("404, which separates 'no such method' from 'no MCP here'")

    runner.check("an unknown method answers 404", unknown_method)

    def unknown_tool() -> tuple[Status, str, dict]:
        reply = mcp.modern("tools/call", {"name": "tracks.nope", "arguments": {}})
        payload = reply.json()
        assert "error" in payload, (
            "an unknown tool came back as a tool execution error; nothing the model "
            "does could fix it, so it belongs in the JSON-RPC channel"
        )
        return ok(f"JSON-RPC error {payload['error'].get('code')}")

    runner.check("an unknown tool is a JSON-RPC error, not a tool error", unknown_tool)

    def bad_arguments() -> tuple[Status, str, dict]:
        reply = mcp.modern(
            "tools/call", {"name": "tracks.get", "arguments": {"trackId": -999999}}
        )
        payload = reply.json()
        result = payload.get("result") or {}
        if result.get("isError") is True:
            assert "structuredContent" not in result, (
                "a tool error carried structuredContent, which cannot satisfy the outputSchema"
            )
            return ok("isError with the envelope in the text block, and no structuredContent")
        if "error" in payload:
            return unclear(f"answered in the JSON-RPC channel with {payload['error'].get('code')}")
        return fail("an unknown track id was accepted")

    runner.check("an actionable failure is a tool execution error", bad_arguments)

    def listen() -> tuple[Status, str, dict]:
        status, messages, comments = mcp.listen_modern(
            ["magda://tracks", "magda://transport", "magda://nothing/here"],
            context.stream_window,
        )
        assert status == 200, f"expected 200 for the SSE stream, got {status}"
        assert messages, "the stream opened but sent nothing, not even an acknowledgement"
        first = messages[0]
        method = first.get("method") if isinstance(first, dict) else None
        assert method == "notifications/subscriptions/acknowledged", (
            f"first frame was {method!r}, expected the acknowledgement"
        )
        agreed = ((first.get("params") or {}).get("notifications.resourceSubscriptions")) or []
        assert "magda://nothing/here" not in agreed, (
            "the acknowledgement echoed a URI that names nothing, so it is an echo "
            "rather than the filter actually agreed to"
        )
        return ok(
            f"acknowledged {agreed}; {len(messages)} messages, {comments} keep-alives "
            f"over {context.stream_window:.0f}s",
            agreed=agreed, comments=comments,
        )

    runner.check("subscriptions/listen acknowledges, then stays up", listen)


# ---------------------------------------------------------------------------
# MCP, legacy era
# ---------------------------------------------------------------------------


def run_mcp_legacy(context: Context) -> None:
    runner = SuiteRunner(context, "mcp (legacy, session)")
    if not context.record.has_mcp:
        runner.skip("every check", "the record carries no mcpUrl")
        return
    mcp = context.mcp()
    session: list[str] = []

    def initialize() -> tuple[Status, str, dict]:
        reply = mcp.initialize(LEGACY_PROTOCOL)
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        result = reply.json().get("result") or {}
        assert result.get("protocolVersion") == LEGACY_PROTOCOL, (
            f"negotiated {result.get('protocolVersion')}, asked for {LEGACY_PROTOCOL}"
        )
        assert reply.session_id, "initialize minted no Mcp-Session-Id"
        session.append(reply.session_id)
        server = result.get("serverInfo") or {}
        return ok(
            f"session {reply.session_id[:8]}…, server {server.get('name')} {server.get('version')}",
            protocolVersion=result.get("protocolVersion"),
        )

    runner.check("initialize mints a session and echoes the version", initialize)

    if not session:
        runner.skip("the remaining legacy checks", "no session to use")
        return

    def with_session() -> tuple[Status, str, dict]:
        reply = mcp.legacy("tools/list", session=session[0])
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        tools = (reply.json().get("result") or {}).get("tools") or []
        assert tools, "tools/list under a session returned nothing"
        return ok(f"{len(tools)} tools, no _meta and no mirrored headers needed")

    runner.check("tools/list works against the session alone", with_session)

    def call_with_session() -> tuple[Status, str, dict]:
        reply = mcp.legacy(
            "tools/call", {"name": "tracks.list", "arguments": {}}, session=session[0]
        )
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        result = reply.json().get("result") or {}
        assert result.get("isError") is False, f"tools/call failed: {result}"
        return ok("structured content returned")

    runner.check("tools/call works against the session", call_with_session)

    def modern_ignores_session() -> tuple[Status, str, dict]:
        params = {"_meta": mcp.modern_meta()}
        body = json.dumps(
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": params}
        )
        headers = mcp.base_headers()
        headers["MCP-Protocol-Version"] = MODERN_PROTOCOL
        headers["Mcp-Method"] = "tools/list"
        headers["Mcp-Session-Id"] = session[0]
        reply = mcp.request("tools/list", body, headers)
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        assert reply.session_id is None, (
            f"a modern request echoed Mcp-Session-Id {reply.session_id}, which 2026-07-28 forbids"
        )
        return ok("served statelessly, no session echoed")

    runner.check("a modern request carrying a session id is served statelessly", modern_ignores_session)

    def discover_is_modern_only() -> tuple[Status, str, dict]:
        reply = mcp.legacy("server/discover", session=session[0])
        payload = reply.json()
        assert "error" in payload, "server/discover answered in a legacy session"
        return ok(f"refused with {payload['error'].get('code')}")

    runner.check("server/discover is refused in a legacy session", discover_is_modern_only)

    def get_stream() -> tuple[Status, str, dict]:
        reply = mcp.legacy("resources/subscribe", {"uri": "magda://tracks"}, session=session[0])
        assert reply.status == 200, f"resources/subscribe: HTTP {reply.status}"
        status, messages, comments = mcp.listen_legacy(session[0], context.stream_window)
        assert status == 200, f"expected 200 for the GET stream, got {status}"
        return ok(
            f"open for {context.stream_window:.0f}s, {len(messages)} messages, "
            f"{comments} keep-alives",
            comments=comments,
        )

    runner.check("resources/subscribe, then the GET stream stays up", get_stream)

    def delete_session() -> tuple[Status, str, dict]:
        reply = mcp.delete_session(session[0])
        assert reply.status in (200, 202, 204), f"DELETE answered {reply.status}"
        after = mcp.legacy("tools/list", session=session[0])
        assert after.status != 200 or "error" in after.json(), (
            "the session still worked after being deleted"
        )
        return ok(f"DELETE {reply.status}, then the session no longer resolves")

    runner.check("DELETE ends the session", delete_session)


# ---------------------------------------------------------------------------
# The stdio bridge — the surface the settings page actually tells users about
# ---------------------------------------------------------------------------


def resolve_bridge(context: Context) -> Path | None:
    if context.bridge_path:
        return Path(context.bridge_path)
    exe = executable_for_pid(context.record.pid)
    if exe is not None:
        candidate = exe.parent / bridge_name()
        if candidate.exists():
            return candidate
    found = shutil.which(bridge_name())
    return Path(found) if found else None


def run_bridge(context: Context) -> None:
    runner = SuiteRunner(context, "mcp bridge (magda-mcp, stdio)")
    bridge = resolve_bridge(context)
    if bridge is None:
        runner.skip(
            "every check",
            f"no {bridge_name()} found beside the running MAGDA or on PATH; "
            "pass --bridge to point at one",
        )
        return

    runner.report.note(f"using {bridge}")

    try:
        with McpBridgeClient(str(bridge), data_dir=context.data_dir) as client:
            def modern_call() -> tuple[Status, str, dict]:
                reply = client.call("tools/call", {"name": "tracks.list", "arguments": {}})
                assert "result" in reply, f"bridge returned {reply}"
                assert reply["result"].get("isError") is False, reply["result"]
                return ok("a stateless call reached MAGDA and came back")

            runner.check("a modern tools/call proxies through the bridge", modern_call)

            def tools_match() -> tuple[Status, str, dict]:
                through_bridge = client.call("tools/list")
                names = sorted(t["name"] for t in through_bridge["result"]["tools"])
                mcp = context.mcp()
                direct = mcp.modern("tools/list")
                direct_names = sorted(
                    t["name"] for t in (direct.json()["result"]["tools"])
                )
                assert names == direct_names, (
                    f"bridge and HTTP disagree: {set(names) ^ set(direct_names)}"
                )
                return ok(f"{len(names)} tools, identical to the HTTP endpoint")

            runner.check("the bridge exposes the same tools as the endpoint", tools_match)

            def legacy_session() -> tuple[Status, str, dict]:
                init = client.call(
                    "initialize",
                    {
                        "protocolVersion": LEGACY_PROTOCOL,
                        "capabilities": {},
                        "clientInfo": {"name": clients.CLIENT_NAME, "version": "1.0"},
                    },
                    era="legacy",
                )
                assert "result" in init, f"initialize failed: {init}"
                version = init["result"].get("protocolVersion")
                assert version == LEGACY_PROTOCOL, f"negotiated {version}"
                # The session id never crosses stdio; the bridge holds it. A
                # second call succeeding is the evidence that it did.
                follow_up = client.call("tools/list", era="legacy")
                assert "result" in follow_up, f"the session did not carry: {follow_up}"
                return ok("initialize, then a second call on the same session the bridge holds")

            runner.check("a legacy session is carried across requests", legacy_session)

            if client.stderr:
                runner.report.note(f"bridge stderr: {client.stderr[-1][:120]}")
    except Exception as exc:  # noqa: BLE001
        message = f"{type(exc).__name__}: {exc}"
        runner.check("run the bridge", lambda: fail(message))


def run_bridge_without_magda(context: Context) -> None:
    """The bridge with nothing to reach must error, not hang.

    A host that launches `magda-mcp` before MAGDA is up gets this path, and a
    hang there is indistinguishable to the user from a broken host.
    """
    runner = SuiteRunner(context, "mcp bridge (no MAGDA)")
    bridge = resolve_bridge(context)
    if bridge is None:
        runner.skip("every check", "no magda-mcp found")
        return

    def errors_promptly() -> tuple[Status, str, dict]:
        empty = Path(tempfile.gettempdir()) / "magda-transport-check-empty"
        empty.mkdir(parents=True, exist_ok=True)
        with McpBridgeClient(str(bridge), data_dir=empty, timeout=15.0) as client:
            reply = client.call("tools/list")
        assert "error" in reply, f"expected an error with no MAGDA running, got {reply}"
        return ok(f"answered {reply['error'].get('code')} rather than hanging")

    runner.check("pointed at an empty data dir, the bridge errors rather than hangs", errors_promptly)


# ---------------------------------------------------------------------------
# Permissions — the first thing a new user hits
# ---------------------------------------------------------------------------


def run_readonly(context: Context) -> None:
    runner = SuiteRunner(context, "read-only client")
    if not context.record.has_websocket:
        runner.skip("every check", "the record carries no WebSocket url")
        return
    runner.report.note(
        f"as '{clients.READONLY_CLIENT_NAME}' — do not grant this one anything in Connections"
    )

    def ws_read() -> tuple[Status, str, dict]:
        with context.ws(client_name=clients.READONLY_CLIENT_NAME) as ws:
            reply = ws.call("tracks.list")
            assert reply.ok, f"an ungranted client could not read: {reply.error}"
            return ok("read is always on, as documented")

    runner.check("an ungranted client may read over WebSocket", ws_read)

    def ws_write() -> tuple[Status, str, dict]:
        with context.ws(client_name=clients.READONLY_CLIENT_NAME) as ws:
            reply = ws.call("tracks.create", {"name": "transport-check", "type": "audio"})
            if reply.ok:
                return fail(
                    "an ungranted client created a track. Either this name has been "
                    "granted 'edit' in Connections -> Clients, or nothing is being enforced."
                )
            message = (reply.error or {}).get("message", "")
            return ok(f"code {reply.code}: {message[:80]}", code=reply.code, message=message)

    runner.check("an ungranted client is refused a write over WebSocket", ws_write)

    if not context.record.has_mcp:
        runner.skip("the MCP checks", "the record carries no mcpUrl")
        return

    mcp = context.mcp(client_name=clients.READONLY_CLIENT_NAME)

    def mcp_read() -> tuple[Status, str, dict]:
        reply = mcp.modern("resources/read", {"uri": "magda://tracks"})
        assert reply.status == 200, f"HTTP {reply.status}: {reply.body[:200]}"
        return ok("resources/read allowed")

    runner.check("an ungranted client may read over MCP", mcp_read)

    def mcp_write() -> tuple[Status, str, dict]:
        reply = mcp.modern(
            "tools/call",
            {"name": "tracks.create", "arguments": {"name": "transport-check", "type": "audio"}},
        )
        payload = reply.json()
        result = payload.get("result") or {}
        if result.get("isError") is True:
            text = (result.get("content") or [{}])[0].get("text", "")
            return ok(f"tool execution error, surfaced to the model: {text[:90]}", text=text)
        if "error" in payload:
            code = payload["error"].get("code")
            return ok(f"JSON-RPC error {code}: {payload['error'].get('message', '')[:80]}", code=code)
        return fail(
            "an ungranted client created a track over MCP. Either this name has been "
            "granted 'edit', or nothing is being enforced."
        )

    runner.check("an ungranted client is refused a write over MCP", mcp_write)


# ---------------------------------------------------------------------------
# OSC
# ---------------------------------------------------------------------------


def run_osc(context: Context) -> None:
    runner = SuiteRunner(context, "osc")

    client = OscClient("127.0.0.1", context.osc_send_port, context.osc_feedback_port)
    try:
        client.open(listen=True)
    except OSError as exc:
        runner.report.note(f"could not bind the feedback port {context.osc_feedback_port}: {exc}")
        try:
            client.open(listen=False)
        except OSError as inner:
            message = str(inner)
            runner.check("open a socket", lambda: fail(message))
            return

    try:
        def reachable() -> tuple[Status, str, dict]:
            reason = client.check_reachable()
            if reason:
                return fail(
                    f"{reason}. OSC is off by default — switch it on in the "
                    "Controllers settings, or pass --osc-port."
                )
            return ok(f"127.0.0.1:{context.osc_send_port} accepted a datagram")

        listening = runner.check("MAGDA is listening on the OSC receive port", reachable)
        if listening.status is Status.FAIL:
            runner.skip("the remaining OSC checks", "nothing is listening")
            return

        def snapshot() -> tuple[Status, str, dict]:
            # Understood, therefore answerable — but track 128 resolves to no
            # track, so nothing in the project moves. A surface MAGDA has not
            # heard from before is sent a full snapshot; one it already knows
            # is not, which is why a second run in one session sees nothing.
            client.send("/magda/track/128/mute", 0)
            messages = client.collect(context.stream_window)
            if not messages:
                return unclear(
                    "no feedback arrived. Expected if this host is already a known peer "
                    "in this MAGDA session (restart MAGDA to re-arm), if feedback is "
                    f"aimed at a port other than {context.osc_feedback_port}, or if OSC "
                    "feedback is off."
                )
            addresses = sorted({m.address for m in messages})
            return ok(
                f"{len(messages)} messages over {len(addresses)} addresses, "
                f"e.g. {addresses[:3]}",
                messages=len(messages), addresses=len(addresses),
            )

        runner.check("a new surface is sent a feedback snapshot", snapshot)

        if not context.write:
            runner.skip("an OSC command changes the project", "needs --write")
            return

        def round_trip() -> tuple[Status, str, dict]:
            with context.ws() as ws:
                before = ws.call("project.get")
                assert before.ok, f"project.get failed: {before.error}"
                original = float((before.result or {}).get("tempo", 120.0))
                target = round(original + 2.0, 3)
                try:
                    client.send("/magda/transport/tempo", target)
                    deadline_reads = 20
                    for _ in range(deadline_reads):
                        after = ws.call("project.get")
                        if abs(float((after.result or {}).get("tempo", 0)) - target) < 1e-2:
                            break
                        time.sleep(0.1)
                    else:
                        return fail(
                            f"tempo is still {(after.result or {}).get('tempo')} after "
                            f"an OSC message asking for {target}"
                        )
                    echoes = client.collect(1.0)
                    echoed = [m.address for m in echoes if "tempo" in m.address]
                    detail = f"tempo {original} -> {target} via OSC, confirmed over WebSocket"
                    if echoed:
                        detail += f"; feedback echoed {echoed[0]}"
                    return ok(detail)
                finally:
                    ws.call("project.setTempo", {"tempo": original})

        runner.check("an OSC command changes the project, and reverts", round_trip)
    finally:
        client.close()
