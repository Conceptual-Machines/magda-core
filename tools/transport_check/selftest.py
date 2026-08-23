"""A stub MAGDA, so the harness itself can be checked without one running.

This is not a test of MAGDA. It is a test of the code in this directory: that
the WebSocket framing is right, that each check reads the JSON path it means
to, that a suite whose transport is refusing everything reports that instead of
crashing, and that the exit status follows the findings.

Point the harness at a real build to learn anything about MAGDA. Run this to
learn whether the harness is still working after someone edits it:

    python3 tools/transport_check/selftest.py

Dependency-free on purpose, including the WebSocket server, so it runs wherever
the harness does.
"""

from __future__ import annotations

import base64
import hashlib
import http.server
import json
import os
import socket
import struct
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import clients  # noqa: E402
from report import Status  # noqa: E402

TOKEN = "stub-token-0123456789abcdef"
WS_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MODERN = clients.MODERN_PROTOCOL
LEGACY = clients.LEGACY_PROTOCOL

TOOLS = [
    {"name": "tracks.list", "description": "List tracks", "inputSchema": {"type": "object"}},
    {"name": "tracks.get", "description": "Get a track", "inputSchema": {"type": "object"}},
    {"name": "tracks.create", "description": "Create a track", "inputSchema": {"type": "object"}},
    {"name": "project.get", "description": "Get the project", "inputSchema": {"type": "object"}},
]
#: Modern-era results the schema treats as cacheable, and which therefore
#: must carry `ttlMs` and `cacheScope` beside `resultType`.
CACHEABLE_RESULTS = {"tools/list", "resources/list", "resources/templates/list",
                     "resources/read", "server/discover"}

#: `tracks.list` returns an *array*, and the two MCP surfaces carry it
#: differently — `structuredContent` is typed as an object, so the tool surface
#: wraps it while `resources/read` returns it bare. Modelled here because the
#: real endpoint does it, and a stub that returned an object from both would
#: let the parity checks pass without ever meeting the case that matters.
TRACKS_ARRAY = [{"id": 1, "name": "Drums"}]
TRACKS_BARE_JSON = json.dumps(TRACKS_ARRAY)
TRACKS_WRAPPED_JSON = json.dumps({"items": TRACKS_ARRAY})


def project_json() -> str:
    return json.dumps({"name": "Stub", "tempo": STATE["tempo"]})

#: The stub project. Shared by every transport, because "an OSC message
#: changed something a WebSocket read can see" is precisely what the
#: cross-transport check asserts, and two copies of the tempo would make
#: that check pass or fail for reasons that have nothing to do with it.
STATE = {"tempo": 120.0, "revision": 41}


# ---------------------------------------------------------------------------
# A WebSocket server, server side of the same framing wire.py implements
# ---------------------------------------------------------------------------


def _read_http_request(conn: socket.socket) -> tuple[str, dict[str, str]]:
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = conn.recv(4096)
        if not chunk:
            raise ConnectionError("closed during handshake")
        buf += chunk
    head = buf.split(b"\r\n\r\n")[0].decode("latin-1")
    lines = head.split("\r\n")
    target = lines[0].split(" ")[1] if len(lines[0].split(" ")) > 1 else "/"
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        if name:
            headers[name.strip().lower()] = value.strip()
    return target, headers


def _ws_send(conn: socket.socket, text: str, opcode: int = 0x1) -> None:
    payload = text.encode("utf-8")
    header = bytearray([0x80 | opcode])
    length = len(payload)
    if length < 126:
        header.append(length)
    elif length < 65536:
        header.append(126)
        header += struct.pack(">H", length)
    else:
        header.append(127)
        header += struct.pack(">Q", length)
    conn.sendall(bytes(header) + payload)


def _recv_exact(conn: socket.socket, count: int) -> bytes:
    out = b""
    while len(out) < count:
        chunk = conn.recv(count - len(out))
        if not chunk:
            raise ConnectionError("closed")
        out += chunk
    return out


def _ws_recv(conn: socket.socket) -> str | None:
    first, second = _recv_exact(conn, 2)
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    if length == 126:
        (length,) = struct.unpack(">H", _recv_exact(conn, 2))
    elif length == 127:
        (length,) = struct.unpack(">Q", _recv_exact(conn, 8))
    mask = _recv_exact(conn, 4) if masked else b""
    payload = _recv_exact(conn, length) if length else b""
    if masked:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    if opcode == 0x8:
        return None
    return payload.decode("utf-8")


class StubWebSocket(threading.Thread):
    """`/rpc`: bearer token, Origin rule, a handful of operations, pushed events."""

    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        self.running = True

    def run(self) -> None:
        while self.running:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def stop(self) -> None:
        self.running = False
        self.sock.close()

    def _serve(self, conn: socket.socket) -> None:
        try:
            target, headers = _read_http_request(conn)
            if headers.get("authorization") != f"Bearer {TOKEN}":
                conn.sendall(b"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n")
                return
            if "origin" in headers:
                conn.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
                return
            key = headers.get("sec-websocket-key", "").encode()
            accept = base64.b64encode(hashlib.sha1(key + WS_GUID).digest()).decode()
            conn.sendall(
                (
                    "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                    f"Connection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n"
                ).encode()
            )
            client_name = "unknown"
            if "client=" in target:
                client_name = target.split("client=", 1)[1].split("&")[0]

            while True:
                raw = _ws_recv(conn)
                if raw is None:
                    return
                request = json.loads(raw)
                self._dispatch(conn, request, client_name)
        except (ConnectionError, OSError, ValueError):
            return
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def _dispatch(self, conn: socket.socket, request: dict, client_name: str) -> None:
        method = request.get("method")
        params = request.get("params") or {}
        meta = request.get("meta") or {}
        rid = request.get("id")

        def result(value: object) -> None:
            _ws_send(conn, json.dumps({
                "jsonrpc": "2.0", "id": rid, "result": value,
                "meta": {"revision": STATE['revision'], "apiVersion": "1.0.0"},
            }))

        def error(code: int, message: str) -> None:
            _ws_send(conn, json.dumps({
                "jsonrpc": "2.0", "id": rid,
                "error": {"code": code, "message": message,
                          "data": {"revision": STATE['revision']}},
            }))

        writes = {"tracks.create", "project.setTempo"}
        if method in writes and client_name == clients.READONLY_CLIENT_NAME:
            error(-32006, "this client has not been granted 'edit'")
            return
        if method in writes and "expectedRevision" in meta and meta["expectedRevision"] != STATE['revision']:
            error(-32004, "the project has moved on")
            return

        if method == "system.describe":
            result({"operations": [{"name": t["name"]} for t in TOOLS]})
        elif method == "project.get":
            result({"tempo": STATE['tempo'], "name": "Stub"})
        elif method == "tracks.list":
            result(TRACKS_ARRAY)
        elif method == "project.setTempo":
            STATE['tempo'] = float(params.get("tempo", STATE['tempo']))
            STATE['revision'] += 1
            result({"tempo": STATE['tempo']})
        elif method == "tracks.create":
            STATE['revision'] += 1
            result({"id": 2, "name": params.get("name")})
        elif method == "subscriptions.subscribe":
            topics = params.get("topics") or []
            result({"snapshots": [
                {"type": "snapshot", "topic": t, "payload": {"tempo": STATE['tempo']}}
                for t in topics if t in ("project", "transport", "tracks")
            ]})
            if any(t in ("playhead", "meters") for t in topics):
                def push() -> None:
                    for _ in range(4):
                        try:
                            _ws_send(conn, json.dumps({
                                "jsonrpc": "2.0", "method": "subscriptions.event",
                                "params": {"topic": "playhead", "payload": {"beats": 4.0}},
                            }))
                        except OSError:
                            return
                        __import__("time").sleep(0.3)
                threading.Thread(target=push, daemon=True).start()
        else:
            error(-32601, f"unknown operation {method}")


# ---------------------------------------------------------------------------
# The MCP endpoint, both eras
# ---------------------------------------------------------------------------


class QuietHttpServer(http.server.ThreadingHTTPServer):
    """Closing a stream is how a client cancels it, so a reset is not an error."""

    def handle_error(self, request: object, client_address: object) -> None:
        if not isinstance(sys.exc_info()[1], (ConnectionResetError, BrokenPipeError)):
            super().handle_error(request, client_address)


class StubMcpHandler(http.server.BaseHTTPRequestHandler):
    sessions: dict[str, str] = {}
    protocol_version = "HTTP/1.1"

    def log_message(self, *args: object) -> None:  # quiet
        pass

    # -- helpers ----------------------------------------------------------

    def _send_json(self, status: int, payload: dict, headers: dict | None = None) -> None:
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    @staticmethod
    def _error(rid: object, code: int, message: str) -> dict:
        return {"jsonrpc": "2.0", "id": rid, "error": {"code": code, "message": message}}

    def _authorised(self) -> bool:
        return self.headers.get("Authorization") == f"Bearer {TOKEN}"

    def _refused(self) -> bool:
        """401 for a bad token, 403 for an Origin nobody allowed. Both before anything else."""
        if not self._authorised():
            self._send_json(401, {"error": "unauthorised"})
            return True
        if self.headers.get("Origin"):
            self._send_json(403, {"error": "origin not allowed"})
            return True
        return False

    # -- verbs ------------------------------------------------------------

    def do_DELETE(self) -> None:
        session = self.headers.get("Mcp-Session-Id")
        self.sessions.pop(session, None)
        self.send_response(204)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:
        if self._refused():
            return
        session = self.headers.get("Mcp-Session-Id")
        if not session or session not in self.sessions:
            self._send_json(400, self._error(None, -32600, "no session"))
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        import time as _t
        for _ in range(20):
            try:
                self.wfile.write(b": keep-alive\n\n")
                self.wfile.flush()
            except OSError:
                return
            _t.sleep(0.4)

    def do_POST(self) -> None:
        if self._refused():
            return
        length = int(self.headers.get("Content-Length", "0"))
        try:
            request = json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            self._send_json(400, self._error(None, -32700, "parse error"))
            return

        rid = request.get("id")
        method = request.get("method")
        params = request.get("params") or {}
        meta = params.get("_meta") or {}
        session = self.headers.get("Mcp-Session-Id")

        if rid is None:
            self.send_response(202)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        modern = bool(meta.get(clients.META_PROTOCOL_VERSION))
        if not modern and method == "initialize":
            return self._initialize(rid, params)
        if not modern and session in self.sessions:
            return self._dispatch(rid, method, params, era="legacy",
                                  client_name=self.sessions[session])
        if not modern:
            self._send_json(400, self._error(
                rid, -32602,
                "A request must carry io.modelcontextprotocol/protocolVersion in params._meta, "
                "or an Mcp-Session-Id from a prior initialize"))
            return

        # Modern: the mirrored headers and the required _meta fields.
        if self.headers.get("MCP-Protocol-Version") != meta.get(clients.META_PROTOCOL_VERSION):
            self._send_json(400, self._error(rid, -32020, "MCP-Protocol-Version mismatch"))
            return
        if self.headers.get("Mcp-Method") != method:
            self._send_json(400, self._error(rid, -32020, "Mcp-Method mismatch"))
            return
        name_field = {"tools/call": "name", "prompts/get": "name", "resources/read": "uri"}.get(method)
        if name_field:
            raw = self.headers.get("Mcp-Name")
            if raw is None:
                self._send_json(400, self._error(rid, -32020, "Mcp-Name is required"))
                return
            if raw.startswith("=?base64?") and raw.endswith("?="):
                raw = base64.b64decode(raw[len("=?base64?"):-2]).decode()
            if raw != params.get(name_field):
                self._send_json(400, self._error(rid, -32020, "Mcp-Name mismatch"))
                return
        if not isinstance(meta.get(clients.META_CLIENT_CAPABILITIES), dict):
            self._send_json(400, self._error(
                rid, -32602,
                "io.modelcontextprotocol/clientCapabilities is required in params._meta"))
            return

        client_name = (meta.get(clients.META_CLIENT_INFO) or {}).get("name", "unknown")
        self._dispatch(rid, method, params, era="modern", client_name=client_name)

    # -- protocol ---------------------------------------------------------

    def _initialize(self, rid: object, params: dict) -> None:
        version = params.get("protocolVersion", LEGACY)
        session = base64.urlsafe_b64encode(os.urandom(12)).decode().rstrip("=")
        self.sessions[session] = (params.get("clientInfo") or {}).get("name", "unknown")
        self._send_json(200, {
            "jsonrpc": "2.0", "id": rid,
            "result": {
                "protocolVersion": version,
                "capabilities": {"tools": {}, "resources": {"subscribe": True}},
                "serverInfo": {"name": "MAGDA-stub", "version": "1.0"},
            },
        }, headers={"Mcp-Session-Id": session})

    def _dispatch(self, rid: object, method: str, params: dict, era: str, client_name: str) -> None:
        def result(value: object) -> None:
            # What McpEndpoint::handle's `complete` lambda does: a modern reply
            # carries `resultType` and a serverInfo `_meta`, and a legacy one
            # carries neither, since that revision has never heard of them.
            if era == "modern" and isinstance(value, dict):
                value = dict(value)
                value.setdefault("resultType", "complete")
                # The 2026-07-28 schema makes a cacheable result carry its own
                # cache directives: `resultType` alone does not satisfy it, and
                # a client validating against the schema — the official SDK
                # does — rejects the response without them.
                if method in CACHEABLE_RESULTS:
                    value.setdefault("ttlMs", 0)
                    value.setdefault("cacheScope", "private")
                meta = dict(value.get("_meta") or {})
                meta.setdefault("io.modelcontextprotocol/serverInfo",
                                {"name": "MAGDA-stub", "version": "1.0"})
                value["_meta"] = meta
            self._send_json(200, {"jsonrpc": "2.0", "id": rid, "result": value})

        if method == "server/discover":
            if era != "modern":
                self._send_json(400, self._error(
                    rid, -32022, "server/discover requires protocol version 2026-07-28 or later"))
                return
            # The same shape McpEndpoint::discoverResult() builds. `capabilities`
            # is required by the specification's DiscoverResult, and a client
            # that cannot parse this falls back to the legacy handshake — which
            # is exactly what the SDK suite exists to notice.
            result({
                "resultType": "complete",
                "ttlMs": 0,
                "cacheScope": "private",
                "supportedVersions": [MODERN, LEGACY, "2025-06-18"],
                "capabilities": {"tools": {"listChanged": False},
                                 "resources": {"subscribe": True, "listChanged": False}},
                "instructions": "Inspect and control the MAGDA session that is running now.",
                "_meta": {"io.modelcontextprotocol/serverInfo":
                          {"name": "MAGDA-stub", "version": "1.0"}},
            })
        elif method == "tools/list":
            result({"tools": TOOLS})
        elif method == "resources/list":
            result({"resources": [
                {"uri": "magda://tracks", "name": "tracks",
                 "mimeType": "application/json"},
                {"uri": "magda://project/current", "name": "project",
                 "mimeType": "application/json"},
            ]})
        elif method == "resources/templates/list":
            result({"resourceTemplates": [{"uriTemplate": "magda://tracks/{track_id}",
                                           "name": "track"}]})
        elif method == "resources/read":
            uri = params.get("uri")
            bodies = {
                "magda://tracks": TRACKS_BARE_JSON,
                "magda://project/current": project_json(),
            }
            if uri not in bodies:
                self._send_json(200, self._error(rid, -32602, f"no resource at {uri}"))
                return
            result({"contents": [{"uri": uri, "mimeType": "application/json",
                                  "text": bodies[uri]}]})
        elif method == "tools/call":
            self._tools_call(rid, params, client_name, result)
        elif method == "resources/subscribe":
            result({})
        elif method == "subscriptions/listen":
            self._listen(params)
        else:
            self._send_json(404, self._error(rid, -32601, f"no such method {method}"))

    def _tools_call(self, rid: object, params: dict, client_name: str, result) -> None:
        name = params.get("name")
        arguments = params.get("arguments") or {}
        known = {t["name"] for t in TOOLS}
        if name not in known:
            self._send_json(200, self._error(rid, -32602, f"no tool named {name}"))
            return
        if name == "tracks.create" and client_name == clients.READONLY_CLIENT_NAME:
            result({"isError": True, "content": [{"type": "text", "text": json.dumps(
                {"code": "PERMISSION_DENIED",
                 "message": "this client has not been granted 'edit'"})}]})
            return
        if name == "tracks.get" and arguments.get("trackId", 0) < 0:
            result({"isError": True, "content": [{"type": "text", "text": json.dumps(
                {"code": "NOT_FOUND", "message": "no track with that id",
                 "issues": [{"field": "trackId", "problem": "unknown"}]})}]})
            return
        if name == "tracks.list":
            text = TRACKS_WRAPPED_JSON
        elif name == "project.get":
            text = project_json()
        else:
            text = json.dumps({"ok": True})
        result({
            "isError": False,
            "content": [{"type": "text", "text": text}],
            "structuredContent": json.loads(text),
            "_meta": {clients.MAGDA_META_REVISION: STATE["revision"]},
        })

    def _listen(self, params: dict) -> None:
        import time as _t
        asked = params.get("notifications.resourceSubscriptions") or []
        # Only URIs that name something are agreed to — the honest filter.
        agreed = [u for u in asked if u == "magda://tracks" or u == "magda://transport"]
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        ack = {"jsonrpc": "2.0", "method": "notifications/subscriptions/acknowledged",
               "params": {"notifications.resourceSubscriptions": agreed}}
        try:
            self.wfile.write(f"data: {json.dumps(ack)}\n\n".encode())
            self.wfile.flush()
            for _ in range(20):
                self.wfile.write(b": keep-alive\n\n")
                self.wfile.flush()
                _t.sleep(0.4)
        except OSError:
            return


# ---------------------------------------------------------------------------
# OSC
# ---------------------------------------------------------------------------


class StubOsc(threading.Thread):
    """Answers a host it has not heard from with a snapshot, once."""

    def __init__(self, receive_port: int, feedback_port: int) -> None:
        super().__init__(daemon=True)
        from wire import osc_decode, osc_encode

        self._decode, self._encode = osc_decode, osc_encode
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", receive_port))
        self.receive_port = self.sock.getsockname()[1]
        self.feedback_port = feedback_port
        self.known: set[str] = set()
        self.running = True

    def run(self) -> None:
        while self.running:
            try:
                data, peer = self.sock.recvfrom(65536)
            except OSError:
                return
            messages = self._decode(data)
            if not messages:
                continue  # heard from, but nothing understood: not answerable
            for message in messages:
                if message.address == "/magda/transport/tempo" and message.args:
                    STATE['tempo'] = float(message.args[0])
            if peer[0] not in self.known:
                self.known.add(peer[0])
                self._snapshot(peer[0])

    def _snapshot(self, host: str) -> None:
        out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            for address, value in (
                ("/magda/transport/tempo", STATE['tempo']),
                ("/magda/track/1/volume", 0.8),
                ("/magda/track/1/pan", 0.5),
                ("/magda/master/volume", 1.0),
            ):
                out.sendto(self._encode(address, value), (host, self.feedback_port))
        finally:
            out.close()

    def stop(self) -> None:
        self.running = False
        self.sock.close()


# ---------------------------------------------------------------------------
# A fake magda-mcp
# ---------------------------------------------------------------------------

FAKE_BRIDGE = '''#!{python}
"""A stand-in for magda-mcp: newline JSON-RPC on stdio, proxied over HTTP."""
import http.client, json, os, sys, glob

def endpoint():
    directory = os.environ.get("MAGDA_DATA_DIR", "")
    best = None
    for path in glob.glob(os.path.join(directory, "remote-api-*.json")):
        with open(path) as handle:
            record = json.load(handle)
        if record.get("mcpUrl") and record.get("token"):
            best = record
    return best

SESSION = {{}}
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = json.loads(line)
    record = endpoint()
    if record is None:
        print(json.dumps({{"jsonrpc": "2.0", "id": request.get("id"),
              "error": {{"code": -32603, "message": "no running MAGDA found"}}}}), flush=True)
        continue
    url = record["mcpUrl"]
    host = url.split("://", 1)[1].split("/", 1)[0]
    path = "/" + url.split("://", 1)[1].split("/", 1)[1]
    headers = {{"Authorization": "Bearer " + record["token"],
               "Content-Type": "application/json",
               "Accept": "application/json, text/event-stream"}}
    meta = (request.get("params") or {{}}).get("_meta") or {{}}
    if meta.get("io.modelcontextprotocol/protocolVersion"):
        headers["MCP-Protocol-Version"] = meta["io.modelcontextprotocol/protocolVersion"]
        headers["Mcp-Method"] = request["method"]
        params = request.get("params") or {{}}
        if request["method"] == "tools/call":
            headers["Mcp-Name"] = params.get("name", "")
        elif request["method"] == "resources/read":
            headers["Mcp-Name"] = params.get("uri", "")
    elif SESSION.get("id"):
        headers["Mcp-Session-Id"] = SESSION["id"]
    conn = http.client.HTTPConnection(host, timeout=15)
    conn.request("POST", path, json.dumps(request), headers)
    response = conn.getresponse()
    body = response.read().decode()
    if response.getheader("Mcp-Session-Id"):
        SESSION["id"] = response.getheader("Mcp-Session-Id")
    conn.close()
    print(body, flush=True)
'''


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main() -> int:
    from cli import run as run_harness

    ws = StubWebSocket()
    ws.start()

    mcp_server = QuietHttpServer(("127.0.0.1", 0), StubMcpHandler)
    mcp_port = mcp_server.server_address[1]
    threading.Thread(target=mcp_server.serve_forever, daemon=True).start()

    osc_receive = free_port()
    osc_feedback = free_port()
    osc = StubOsc(osc_receive, osc_feedback)
    osc.start()

    workspace = Path(tempfile.mkdtemp(prefix="magda-transport-selftest-"))
    record = workspace / f"remote-api-{os.getpid()}.json"
    record.write_text(json.dumps({
        "port": ws.port,
        "token": TOKEN,
        "url": f"ws://127.0.0.1:{ws.port}/rpc",
        "mcpPort": mcp_port,
        "mcpUrl": f"http://127.0.0.1:{mcp_port}/mcp",
        "pid": os.getpid(),
    }))
    record.chmod(0o600)

    bridge = workspace / "magda-mcp"
    bridge.write_text(FAKE_BRIDGE.format(python=sys.executable))
    bridge.chmod(0o755)

    # No --all: with no suite flag the harness runs everything anyway, which
    # leaves room for `selftest.py --ws --json` to narrow it by hand.
    argv = [
        "--write",
        "--record", str(record),
        "--data-dir", str(workspace),
        "--bridge-path", str(bridge),
        "--osc-port", str(osc.receive_port),
        "--osc-feedback-port", str(osc_feedback),
        "--stream-window", "1.5",
    ] + sys.argv[1:]

    print("=" * 72)
    print("  running the harness against the stub")
    print("=" * 72)

    report = run_harness(argv)

    ws.stop()
    osc.stop()
    mcp_server.shutdown()

    if report is None:
        print("  the harness could not resolve the stub's discovery record")
        return 1

    return verdict(report)


#: The one check the stub cannot satisfy. The harness resolves the executable
#: behind the discovery record's pid, and that pid here is the Python running
#: this file, so the bridge is never where a real install would put it. A
#: *passing* result would mean the check was not looking at anything, so it is
#: expected to fail rather than excused.
EXPECTED_FAILURES = {"magda-mcp is where the settings page points"}


def verdict(report) -> int:
    """Fail only on findings the stub was built to satisfy."""
    unexpected = [
        check for check in report.checks
        if check.status is Status.FAIL and check.name not in EXPECTED_FAILURES
    ]
    missing = [
        name for name in EXPECTED_FAILURES
        if not any(c.name == name and c.status is Status.FAIL for c in report.checks)
    ]
    inconclusive = [c for c in report.checks if check_is_unclear(c)]

    print("=" * 72)
    if unexpected:
        print("  the harness reported failures the stub should have satisfied:")
        for check in unexpected:
            print(f"    {check.suite} / {check.name}\n      {check.detail}")
    if missing:
        print("  these were expected to fail against the stub and did not:")
        for name in missing:
            print(f"    {name}")
    if inconclusive:
        print("  inconclusive (not a failure, but worth an eye):")
        for check in inconclusive:
            print(f"    {check.suite} / {check.name}")
    if not unexpected and not missing:
        print(f"  harness self-test passed: {len(report.checks)} checks behaved as expected")
    print("=" * 72)
    return 1 if (unexpected or missing) else 0


def check_is_unclear(check) -> bool:
    return check.status is Status.INCONCLUSIVE


if __name__ == "__main__":
    sys.exit(main())
