"""Transport primitives: a WebSocket client, an OSC codec, an SSE reader.

All three are implemented here rather than pulled from PyPI so the harness is
`python3` and nothing else. A tester running this against a freshly installed
build — often on a machine that is not a dev box — should not have to create a
virtualenv first, and none of these three protocols is large enough to justify
the dependency.
"""

from __future__ import annotations

import base64
import hashlib
import os
import socket
import struct
import time
from dataclasses import dataclass, field

WS_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WireError(Exception):
    pass


class HandshakeError(WireError):
    """The upgrade was refused. Carries the HTTP status, which is the point.

    A `401` and a `403` are the two refusals the WebSocket transport is
    specified to produce before switching protocols, and telling them apart is
    most of what the auth checks assert.
    """

    def __init__(self, status: int, reason: str, body: str = "") -> None:
        super().__init__(f"HTTP {status} {reason}".strip())
        self.status = status
        self.reason = reason
        self.body = body


class _Reader:
    """A buffered reader that survives a timeout mid-message.

    `socket.makefile` would be shorter, but a timeout part-way through a
    buffered read can leave its internal buffer in a state the next read does
    not recover from. Owning the buffer means a slow frame is resumed rather
    than lost, which matters here because the SSE and subscription checks sit
    waiting on a socket that is quiet by design.
    """

    def __init__(self, sock: socket.socket) -> None:
        self.sock = sock
        self.buf = bytearray()

    def _fill(self, deadline: float) -> None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("deadline passed")
        self.sock.settimeout(remaining)
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout as exc:  # noqa: UP041 - alias only from 3.10
            raise TimeoutError("deadline passed") from exc
        if not chunk:
            raise ConnectionError("peer closed the connection")
        self.buf += chunk

    def read_exact(self, count: int, deadline: float) -> bytes:
        while len(self.buf) < count:
            self._fill(deadline)
        out = bytes(self.buf[:count])
        del self.buf[:count]
        return out

    def read_until(self, sep: bytes, deadline: float) -> bytes:
        while True:
            index = self.buf.find(sep)
            if index >= 0:
                out = bytes(self.buf[:index])
                del self.buf[: index + len(sep)]
                return out
            self._fill(deadline)

    def read_some(self, deadline: float) -> bytes:
        if not self.buf:
            self._fill(deadline)
        out = bytes(self.buf)
        self.buf.clear()
        return out


# ---------------------------------------------------------------------------
# WebSocket (RFC 6455, client side, text frames)
# ---------------------------------------------------------------------------


class WebSocket:
    """Enough of RFC 6455 to speak JSON-RPC to MAGDA's `/rpc`.

    Client frames are masked because the specification requires it and
    cpp-httplib enforces it; server frames never are. Continuation frames are
    reassembled, pings are answered, and a close frame ends the conversation.
    """

    def __init__(
        self,
        host: str,
        port: int,
        path: str,
        headers: dict[str, str] | None = None,
        timeout: float = 10.0,
    ) -> None:
        self.host = host
        self.port = port
        self.path = path
        self.headers = dict(headers or {})
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self._reader: _Reader | None = None

    # -- lifecycle --------------------------------------------------------

    def connect(self) -> None:
        deadline = time.monotonic() + self.timeout
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._reader = _Reader(self.sock)

        key = base64.b64encode(os.urandom(16))
        lines = [
            f"GET {self.path} HTTP/1.1",
            f"Host: {self.host}:{self.port}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {key.decode()}",
            "Sec-WebSocket-Version: 13",
        ]
        lines += [f"{name}: {value}" for name, value in self.headers.items()]
        self.sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("utf-8"))

        status_line = self._reader.read_until(b"\r\n", deadline).decode("latin-1")
        parts = status_line.split(" ", 2)
        status = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0
        reason = parts[2] if len(parts) > 2 else ""

        raw_headers = self._reader.read_until(b"\r\n\r\n", deadline).decode("latin-1")
        received = {}
        for line in raw_headers.split("\r\n"):
            name, _, value = line.partition(":")
            if name:
                received[name.strip().lower()] = value.strip()

        if status != 101:
            body = ""
            length = int(received.get("content-length", "0") or 0)
            if length:
                try:
                    body = self._reader.read_exact(length, deadline).decode("utf-8", "replace")
                except (TimeoutError, ConnectionError):
                    body = ""
            self.close()
            raise HandshakeError(status, reason, body)

        expected = base64.b64encode(hashlib.sha1(key + WS_GUID).digest()).decode()
        if received.get("sec-websocket-accept") != expected:
            self.close()
            raise WireError("server did not return a valid Sec-WebSocket-Accept")

    def close(self) -> None:
        if self.sock is None:
            return
        try:
            self._write_frame(0x8, b"")
        except OSError:
            pass
        try:
            self.sock.close()
        finally:
            self.sock = None
            self._reader = None

    def __enter__(self) -> "WebSocket":
        self.connect()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    # -- framing ----------------------------------------------------------

    def _write_frame(self, opcode: int, payload: bytes) -> None:
        if self.sock is None:
            raise WireError("socket is not open")
        header = bytearray([0x80 | opcode])
        length = len(payload)
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header += struct.pack(">H", length)
        else:
            header.append(0x80 | 127)
            header += struct.pack(">Q", length)
        mask = os.urandom(4)
        header += mask
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        self.sock.sendall(bytes(header) + masked)

    def send_text(self, text: str) -> None:
        self._write_frame(0x1, text.encode("utf-8"))

    def recv(self, timeout: float | None = None) -> str:
        """The next text message, reassembling continuations.

        Control frames are handled here rather than surfaced: a ping that went
        unanswered would have the server drop a connection the checks are
        still using, and a close is the end of the message stream whichever
        check is waiting on it.
        """
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        buffer = bytearray()
        assembling = False
        while True:
            assert self._reader is not None
            first, second = self._reader.read_exact(2, deadline)
            fin = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            length = second & 0x7F
            if length == 126:
                (length,) = struct.unpack(">H", self._reader.read_exact(2, deadline))
            elif length == 127:
                (length,) = struct.unpack(">Q", self._reader.read_exact(8, deadline))
            mask = self._reader.read_exact(4, deadline) if masked else b""
            payload = self._reader.read_exact(length, deadline) if length else b""
            if masked:
                payload = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))

            if opcode == 0x8:
                raise ConnectionError("server sent a close frame")
            if opcode == 0x9:
                self._write_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x0:
                if not assembling:
                    raise WireError("continuation frame with nothing to continue")
                buffer += payload
            elif opcode in (0x1, 0x2):
                buffer = bytearray(payload)
                assembling = True
            else:
                raise WireError(f"unexpected opcode 0x{opcode:x}")
            if fin:
                return bytes(buffer).decode("utf-8")


# ---------------------------------------------------------------------------
# OSC 1.0
# ---------------------------------------------------------------------------


def _pad4(size: int) -> int:
    return (size + 3) & ~3


def _osc_string(text: str) -> bytes:
    raw = text.encode("utf-8") + b"\x00"
    return raw.ljust(_pad4(len(raw)), b"\x00")


def osc_encode(address: str, *args: object) -> bytes:
    """One OSC message. `bool` is checked before `int` — it is a subclass."""
    tags = ","
    body = b""
    for arg in args:
        if isinstance(arg, bool):
            tags += "T" if arg else "F"
        elif isinstance(arg, int):
            tags += "i"
            body += struct.pack(">i", arg)
        elif isinstance(arg, float):
            tags += "f"
            body += struct.pack(">f", arg)
        elif isinstance(arg, str):
            tags += "s"
            body += _osc_string(arg)
        else:
            raise TypeError(f"cannot encode {type(arg).__name__} as an OSC argument")
    return _osc_string(address) + _osc_string(tags) + body


@dataclass
class OscMessage:
    address: str
    args: list[object] = field(default_factory=list)


def _read_osc_string(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(b"\x00", offset)
    return data[offset:end].decode("utf-8", "replace"), offset + _pad4(end - offset + 1)


def osc_decode(data: bytes) -> list[OscMessage]:
    """Every message in a datagram, flattening bundles.

    MAGDA's feedback arrives as plain messages today, but a surface is allowed
    to receive bundles and the parser costs four lines, so this reads what the
    protocol permits rather than what one sender happens to emit.
    """
    if data[:8] == b"#bundle\x00":
        messages: list[OscMessage] = []
        offset = 16  # tag + time tag
        while offset + 4 <= len(data):
            try:
                (size,) = struct.unpack_from(">i", data, offset)
            except struct.error:
                break
            offset += 4
            if size <= 0 or offset + size > len(data):
                break
            messages += osc_decode(data[offset : offset + size])
            offset += size
        return messages

    try:
        address, offset = _read_osc_string(data, 0)
        if not address.startswith("/"):
            return []
        tags, offset = _read_osc_string(data, offset)
    except ValueError:
        return []

    args: list[object] = []
    try:
        for tag in tags[1:]:
            if tag in ("i", "f"):
                if offset + 4 > len(data):
                    raise ValueError("argument runs past the end of the datagram")
                fmt = ">i" if tag == "i" else ">f"
                value = struct.unpack_from(fmt, data, offset)[0]
                args.append(value if tag == "i" else round(value, 6))
                offset += 4
            elif tag == "s":
                text, offset = _read_osc_string(data, offset)
                args.append(text)
            elif tag == "T":
                args.append(True)
            elif tag == "F":
                args.append(False)
            else:  # a type this harness does not send and does not need to read
                break
    except (ValueError, IndexError, struct.error):
        # A message whose arguments do not fit its type tags is rejected whole,
        # which is what MAGDA's own reader does with a truncated argument. This
        # socket is an unauthenticated UDP port that anything on the machine can
        # write to, so "reject" has to mean "return nothing", never "raise".
        return []
    return [OscMessage(address, args)]


# ---------------------------------------------------------------------------
# Server-sent events
# ---------------------------------------------------------------------------


@dataclass
class SseEvent:
    event: str
    data: str


class SseParser:
    """Frames out of a byte stream, the same shape `SseParser` in the bridge reads.

    Keep-alive comments are counted rather than discarded: "the stream stayed
    up while quiet" is one of the things #2059 asks to see, and a comment is
    the only evidence of it that crosses the wire.
    """

    def __init__(self) -> None:
        self.buffer = ""
        self.comments = 0

    def feed(self, chunk: bytes) -> list[SseEvent]:
        self.buffer += chunk.decode("utf-8", "replace")
        events: list[SseEvent] = []
        while "\n\n" in self.buffer:
            block, _, self.buffer = self.buffer.partition("\n\n")
            name = "message"
            data: list[str] = []
            for line in block.split("\n"):
                line = line.rstrip("\r")
                if not line or line.startswith(":"):
                    if line.startswith(":"):
                        self.comments += 1
                    continue
                field_name, _, value = line.partition(":")
                value = value[1:] if value.startswith(" ") else value
                if field_name == "event":
                    name = value
                elif field_name == "data":
                    data.append(value)
            if data:
                events.append(SseEvent(name, "\n".join(data)))
        return events
