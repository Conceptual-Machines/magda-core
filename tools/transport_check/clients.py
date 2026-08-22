"""Protocol clients: JSON-RPC over WebSocket, MCP over HTTP, MCP over stdio.

Each of these speaks to MAGDA the way a real client would, from outside the
process. Nothing here imports anything from the app — that is the whole point
of the exercise, since what #2059 asks to establish is exactly the part the
in-tree tests cannot reach: that an installed build answers a client that was
built against the documentation rather than against the source.
"""

from __future__ import annotations

import http.client
import json
import os
import socket
import subprocess
import threading
import time
from dataclasses import dataclass
from typing import Any

from wire import SseParser, WebSocket, osc_decode, osc_encode

#: What this harness calls itself. Stable rather than random so it occupies one
#: row in AI Settings -> Clients instead of a new one per run.
CLIENT_NAME = "magda-transport-check"

#: A second identity that must never be granted anything. The read-only checks
#: need a client the user has not trusted, and reusing the name above would
#: mean the first grant made those checks pass for the wrong reason.
READONLY_CLIENT_NAME = "magda-transport-check-readonly"

CLIENT_VERSION = "1.0"

MODERN_PROTOCOL = "2026-07-28"
LEGACY_PROTOCOL = "2025-11-25"

META_PROTOCOL_VERSION = "io.modelcontextprotocol/protocolVersion"
META_CLIENT_CAPABILITIES = "io.modelcontextprotocol/clientCapabilities"
META_CLIENT_INFO = "io.modelcontextprotocol/clientInfo"
MAGDA_META_REVISION = "com.conceptualmachines.magda/revision"
MAGDA_META_EXPECTED_REVISION = "com.conceptualmachines.magda/expectedRevision"
MAGDA_META_REQUEST_ID = "com.conceptualmachines.magda/requestId"


class ProtocolError(Exception):
    pass


# ---------------------------------------------------------------------------
# WebSocket JSON-RPC
# ---------------------------------------------------------------------------


@dataclass
class Reply:
    """A JSON-RPC reply, kept whole so a check can assert on any part of it."""

    raw: dict[str, Any]

    @property
    def ok(self) -> bool:
        return "result" in self.raw

    @property
    def result(self) -> Any:
        return self.raw.get("result")

    @property
    def error(self) -> dict[str, Any] | None:
        return self.raw.get("error")

    @property
    def code(self) -> int | None:
        error = self.error
        return error.get("code") if error else None

    @property
    def revision(self) -> int | None:
        meta = self.raw.get("meta") or {}
        value = meta.get("revision")
        if value is None and self.error:
            value = (self.error.get("data") or {}).get("revision")
        return value


class WsClient:
    """MAGDA's `/rpc`, as a blocking request/response client.

    Pushed events share the socket with replies, so `call` buffers any
    notification it meets while waiting rather than discarding it — the
    subscription check reads them back out afterwards, and a discarded event
    would make a working push look like a broken one.
    """

    def __init__(
        self,
        host: str,
        port: int,
        token: str,
        client_name: str = CLIENT_NAME,
        timeout: float = 10.0,
        origin: str | None = None,
    ) -> None:
        self.host = host
        self.port = port
        self.token = token
        self.client_name = client_name
        self.timeout = timeout
        self.origin = origin
        self._ws: WebSocket | None = None
        self._next_id = 1
        self.pending_notifications: list[dict[str, Any]] = []

    def connect(self) -> None:
        headers = {"Authorization": f"Bearer {self.token}"}
        if self.origin is not None:
            headers["Origin"] = self.origin
        path = "/rpc"
        if self.client_name:
            path += f"?client={self.client_name}"
        self._ws = WebSocket(self.host, self.port, path, headers, timeout=self.timeout)
        self._ws.connect()

    def close(self) -> None:
        if self._ws is not None:
            self._ws.close()
            self._ws = None

    def __enter__(self) -> "WsClient":
        self.connect()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def call(
        self,
        method: str,
        params: dict[str, Any] | None = None,
        meta: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> Reply:
        if self._ws is None:
            raise ProtocolError("not connected")
        request_id = self._next_id
        self._next_id += 1
        request: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params or {},
        }
        if meta:
            request["meta"] = meta
        self._ws.send_text(json.dumps(request))

        deadline = time.monotonic() + (timeout or self.timeout)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"no reply to {method}")
            message = json.loads(self._ws.recv(remaining))
            if message.get("id") == request_id:
                return Reply(message)
            if "method" in message:
                self.pending_notifications.append(message)

    def collect_notifications(self, window: float) -> list[dict[str, Any]]:
        """Everything pushed over `window` seconds, plus anything buffered."""
        collected = list(self.pending_notifications)
        self.pending_notifications.clear()
        if self._ws is None:
            return collected
        deadline = time.monotonic() + window
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return collected
            try:
                message = json.loads(self._ws.recv(remaining))
            except TimeoutError:
                return collected
            if "method" in message:
                collected.append(message)


# ---------------------------------------------------------------------------
# MCP over Streamable HTTP
# ---------------------------------------------------------------------------


@dataclass
class HttpReply:
    status: int
    headers: dict[str, str]
    body: str

    def json(self) -> dict[str, Any]:
        try:
            return json.loads(self.body)
        except ValueError as exc:
            raise ProtocolError(f"HTTP {self.status} body is not JSON: {self.body[:200]}") from exc

    @property
    def session_id(self) -> str | None:
        return self.headers.get("mcp-session-id")


class McpHttpClient:
    """The MCP endpoint, driven in both protocol eras.

    A fresh connection per request rather than a kept-alive one, because a
    stateless modern request is allowed to arrive on a new socket every time
    and that is the shape most likely to catch a server that quietly depends
    on the connection.
    """

    def __init__(
        self,
        origin: str,
        path: str,
        token: str,
        client_name: str = CLIENT_NAME,
        timeout: float = 15.0,
        min_interval: float = 0.025,
        max_retries: int = 4,
        backoff: float = 0.25,
    ) -> None:
        self.origin = origin
        self.path = path
        self.token = token
        self.client_name = client_name
        self.timeout = timeout
        #: Seconds between requests. 40/s against a 50/s limit, so the bucket
        #: gains a little on every call instead of draining.
        self.min_interval = min_interval
        self.max_retries = max_retries
        self.backoff = backoff
        #: How many 429s were ridden out. Reported once per run, because a
        #: number here means something else is sharing the endpoint.
        self.throttled = 0
        self._last_request = 0.0
        scheme, _, hostport = origin.partition("://")
        self.scheme = scheme
        host, _, port = hostport.partition(":")
        self.host = host
        self.port = int(port) if port else (443 if scheme == "https" else 80)

    # -- plumbing ---------------------------------------------------------

    def _connect(self, timeout: float | None = None) -> http.client.HTTPConnection:
        return http.client.HTTPConnection(self.host, self.port, timeout=timeout or self.timeout)

    def _pace(self) -> None:
        """Stay under the endpoint's rate limit rather than measuring it.

        The bucket holds `maxConcurrentRequests` tokens — eight — and refills at
        `maxRequestsPerSecond`. Firing checks back to back drains it in about a
        dozen requests, and everything after that fails with a 429 that says
        nothing about the thing being checked.
        """
        if self.min_interval <= 0:
            return
        wait = self._last_request + self.min_interval - time.monotonic()
        if wait > 0:
            time.sleep(wait)

    def request(
        self,
        method: str,
        body: str | None = None,
        headers: dict[str, str] | None = None,
        http_method: str = "POST",
    ) -> HttpReply:
        """One request, paced, retrying a 429 the way a real client would.

        The limit is one bucket for the endpoint rather than one per client —
        deliberately, since every caller arrives from 127.0.0.1 with the same
        token — so anything else talking to MAGDA spends from the same budget.
        A harness that treated a shared-budget 429 as a verdict would report
        failures that belong to whatever else was connected.
        """
        for attempt in range(self.max_retries + 1):
            self._pace()
            conn = self._connect()
            try:
                conn.request(http_method, self.path, body, headers or {})
                response = conn.getresponse()
                payload = response.read().decode("utf-8", "replace")
                reply = HttpReply(
                    response.status,
                    {k.lower(): v for k, v in response.getheaders()},
                    payload,
                )
            finally:
                self._last_request = time.monotonic()
                conn.close()

            if reply.status != 429 or attempt == self.max_retries:
                return reply
            self.throttled += 1
            time.sleep(self.backoff * (2 ** attempt))
        return reply  # unreachable, but keeps the type honest

    def base_headers(self) -> dict[str, str]:
        return {
            "Authorization": f"Bearer {self.token}",
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }

    def modern_meta(self, extra: dict[str, Any] | None = None) -> dict[str, Any]:
        meta = {
            META_PROTOCOL_VERSION: MODERN_PROTOCOL,
            META_CLIENT_CAPABILITIES: {},
            META_CLIENT_INFO: {"name": self.client_name, "version": CLIENT_VERSION},
        }
        meta.update(extra or {})
        return meta

    @staticmethod
    def _name_for(method: str, params: dict[str, Any]) -> str | None:
        """The value `Mcp-Name` has to mirror, for the methods that have one."""
        if method in ("tools/call", "prompts/get"):
            return params.get("name")
        if method == "resources/read":
            return params.get("uri")
        return None

    # -- modern era -------------------------------------------------------

    def modern(
        self,
        method: str,
        params: dict[str, Any] | None = None,
        request_id: int | str = 1,
        meta_extra: dict[str, Any] | None = None,
        header_overrides: dict[str, str | None] | None = None,
        omit_meta: bool = False,
    ) -> HttpReply:
        """A stateless request. Overrides exist so the negative checks can lie.

        `header_overrides` sets a header to a wrong value, or to None to drop
        it — which is how the mirrored-header rule is tested from the outside
        rather than taken on trust.
        """
        params = dict(params or {})
        if not omit_meta:
            params["_meta"] = self.modern_meta(meta_extra)
        body = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}

        headers = self.base_headers()
        headers["MCP-Protocol-Version"] = MODERN_PROTOCOL
        headers["Mcp-Method"] = method
        if (name := self._name_for(method, params)) is not None:
            headers["Mcp-Name"] = name
        for key, value in (header_overrides or {}).items():
            if value is None:
                headers.pop(key, None)
            else:
                headers[key] = value
        return self.request(method, json.dumps(body), headers)

    # -- legacy era -------------------------------------------------------

    def initialize(self, version: str = LEGACY_PROTOCOL) -> HttpReply:
        body = {
            "jsonrpc": "2.0",
            "id": "init",
            "method": "initialize",
            "params": {
                "protocolVersion": version,
                "capabilities": {},
                "clientInfo": {"name": self.client_name, "version": CLIENT_VERSION},
            },
        }
        return self.request("initialize", json.dumps(body), self.base_headers())

    def legacy(
        self,
        method: str,
        params: dict[str, Any] | None = None,
        session: str | None = None,
        request_id: int | str = 1,
        version: str = LEGACY_PROTOCOL,
    ) -> HttpReply:
        headers = self.base_headers()
        headers["MCP-Protocol-Version"] = version
        if session:
            headers["Mcp-Session-Id"] = session
        body = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params or {},
        }
        return self.request(method, json.dumps(body), headers)

    def delete_session(self, session: str) -> HttpReply:
        headers = self.base_headers()
        headers["Mcp-Session-Id"] = session
        return self.request("DELETE", None, headers, http_method="DELETE")

    # -- streams ----------------------------------------------------------

    def stream(
        self,
        window: float,
        body: str | None = None,
        headers: dict[str, str] | None = None,
        http_method: str = "POST",
    ) -> tuple[int, list[Any], int]:
        """Hold a stream open for `window` seconds.

        Returns the HTTP status, the JSON messages that arrived, and how many
        keep-alive comments were seen. The comment count is the evidence that a
        quiet stream stayed up rather than merely not having failed yet.
        """
        # A stream costs a token like any other request, and opening one on a
        # drained bucket answers 429 instead of an event stream.
        self._pace()
        self._last_request = time.monotonic()
        conn = self._connect(timeout=window + self.timeout)
        parser = SseParser()
        messages: list[Any] = []
        try:
            conn.request(http_method, self.path, body, headers or {})
            response = conn.getresponse()
            if response.status != 200:
                response.read()
                return response.status, [], 0
            deadline = time.monotonic() + window
            while time.monotonic() < deadline:
                remaining = deadline - time.monotonic()
                if conn.sock is not None:
                    conn.sock.settimeout(max(0.05, min(remaining, 1.0)))
                try:
                    chunk = response.read1(65536)
                except (socket.timeout, TimeoutError):
                    continue
                except OSError:
                    break
                if not chunk:
                    break
                for event in parser.feed(chunk):
                    try:
                        messages.append(json.loads(event.data))
                    except ValueError:
                        messages.append(event.data)
            return response.status, messages, parser.comments
        finally:
            conn.close()

    def listen_modern(self, uris: list[str], window: float) -> tuple[int, list[Any], int]:
        params = {
            "notifications.resourceSubscriptions": uris,
            "_meta": self.modern_meta(),
        }
        body = {
            "jsonrpc": "2.0",
            "id": "listen",
            "method": "subscriptions/listen",
            "params": params,
        }
        headers = self.base_headers()
        headers["MCP-Protocol-Version"] = MODERN_PROTOCOL
        headers["Mcp-Method"] = "subscriptions/listen"
        return self.stream(window, json.dumps(body), headers)

    def listen_legacy(self, session: str, window: float) -> tuple[int, list[Any], int]:
        headers = self.base_headers()
        headers["MCP-Protocol-Version"] = LEGACY_PROTOCOL
        headers["Mcp-Session-Id"] = session
        return self.stream(window, None, headers, http_method="GET")


# ---------------------------------------------------------------------------
# MCP over stdio, through the magda-mcp bridge
# ---------------------------------------------------------------------------


class McpBridgeClient:
    """The `magda-mcp` binary, spoken to the way an MCP host does.

    This is the integration surface that matters: the settings page tells users
    to configure the bridge, not the URL, so a bridge that cannot reach a
    running MAGDA is a broken product however well the HTTP endpoint behaves.

    stderr is drained on a thread. The bridge writes diagnostics there and a
    full pipe would block it mid-request, which would look from here exactly
    like a hang in MAGDA.
    """

    def __init__(
        self,
        path: str,
        data_dir: str | os.PathLike[str] | None = None,
        timeout: float = 20.0,
    ) -> None:
        self.path = path
        self.timeout = timeout
        self.env = dict(os.environ)
        if data_dir is not None:
            self.env["MAGDA_DATA_DIR"] = str(data_dir)
        self.process: subprocess.Popen[bytes] | None = None
        self.stderr: list[str] = []
        self._next_id = 1
        self._stderr_thread: threading.Thread | None = None

    def start(self) -> None:
        self.process = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=self.env,
        )

        def drain() -> None:
            assert self.process is not None and self.process.stderr is not None
            for line in self.process.stderr:
                self.stderr.append(line.decode("utf-8", "replace").rstrip())

        self._stderr_thread = threading.Thread(target=drain, daemon=True)
        self._stderr_thread.start()

    def stop(self) -> None:
        if self.process is None:
            return
        try:
            if self.process.stdin:
                self.process.stdin.close()
            self.process.wait(timeout=5)
        except (subprocess.TimeoutExpired, OSError):
            self.process.kill()
            self.process.wait(timeout=5)
        finally:
            self.process = None

    def __enter__(self) -> "McpBridgeClient":
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.stop()

    def call(self, method: str, params: dict[str, Any] | None = None, era: str = "modern") -> dict[str, Any]:
        """One newline-delimited JSON-RPC round trip over the pipes."""
        if self.process is None or self.process.stdin is None or self.process.stdout is None:
            raise ProtocolError("bridge is not running")
        request_id = self._next_id
        self._next_id += 1
        params = dict(params or {})
        if era == "modern":
            params["_meta"] = {
                META_PROTOCOL_VERSION: MODERN_PROTOCOL,
                META_CLIENT_CAPABILITIES: {},
                META_CLIENT_INFO: {"name": self.client_name(), "version": CLIENT_VERSION},
            }
        request = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
        self.process.stdin.write((json.dumps(request) + "\n").encode("utf-8"))
        self.process.stdin.flush()

        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            line = self._readline(deadline)
            if line is None:
                break
            try:
                message = json.loads(line)
            except ValueError:
                continue
            if message.get("id") == request_id:
                return message
        raise TimeoutError(f"bridge did not answer {method}")

    def client_name(self) -> str:
        return CLIENT_NAME

    def _readline(self, deadline: float) -> str | None:
        """A line from stdout, or None if the bridge exited.

        `readline` on the pipe blocks with no timeout of its own, so the wait
        is bounded by a reader thread rather than by the file object — the
        no-MAGDA-running case is precisely the one where a hang would be
        indistinguishable from a slow answer.
        """
        assert self.process is not None and self.process.stdout is not None
        result: list[bytes | None] = []

        def read() -> None:
            assert self.process is not None and self.process.stdout is not None
            result.append(self.process.stdout.readline() or None)

        thread = threading.Thread(target=read, daemon=True)
        thread.start()
        thread.join(max(0.0, deadline - time.monotonic()))
        if thread.is_alive() or not result:
            return None
        line = result[0]
        return line.decode("utf-8", "replace").strip() if line else None


# ---------------------------------------------------------------------------
# OSC
# ---------------------------------------------------------------------------


class OscClient:
    """A surface: sends to MAGDA's receive port, listens on the feedback port.

    The two ports are separate because MAGDA replies to the *host* on a fixed
    port rather than to the socket a message came from — a real surface sends
    from an ephemeral port and listens on a known one, and `oscFeedbackPort`
    exists for exactly that asymmetry.
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        send_port: int = 9000,
        feedback_port: int = 9001,
    ) -> None:
        self.host = host
        self.send_port = send_port
        self.feedback_port = feedback_port
        self._send: socket.socket | None = None
        self._listen: socket.socket | None = None

    def open(self, listen: bool = True) -> None:
        self._send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Connected UDP: the kernel reports an ICMP port-unreachable back as
        # ECONNREFUSED, which is the only way to learn from outside that
        # nothing is bound without asking MAGDA.
        self._send.connect((self.host, self.send_port))
        if listen:
            self._listen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._listen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._listen.bind(("0.0.0.0", self.feedback_port))

    def close(self) -> None:
        for sock in (self._send, self._listen):
            if sock is not None:
                sock.close()
        self._send = self._listen = None

    def __enter__(self) -> "OscClient":
        self.open()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def send(self, address: str, *args: object) -> None:
        if self._send is None:
            raise ProtocolError("socket is not open")
        self._send.send(osc_encode(address, *args))

    def check_reachable(self) -> str | None:
        """None if the port accepted a datagram, else why it did not.

        UDP has no acknowledgement, so this sends and then looks for the error
        the previous send queued — which on loopback arrives promptly when
        nothing is listening, and never when something is.
        """
        if self._send is None:
            raise ProtocolError("socket is not open")
        try:
            self._send.send(osc_encode("/magda/transport/loop"))
            time.sleep(0.15)
            self._send.send(osc_encode("/magda/transport/loop"))
        except ConnectionRefusedError:
            return f"nothing is listening on {self.host}:{self.send_port}"
        except OSError as exc:
            return f"{self.host}:{self.send_port} rejected a datagram: {exc}"
        return None

    def collect(self, window: float) -> list[Any]:
        """Every OSC message that arrives on the feedback port within `window`."""
        if self._listen is None:
            return []
        received: list[Any] = []
        deadline = time.monotonic() + window
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return received
            self._listen.settimeout(remaining)
            try:
                data, _ = self._listen.recvfrom(65536)
            except (socket.timeout, TimeoutError):
                return received
            except OSError:
                return received
            received.extend(osc_decode(data))
