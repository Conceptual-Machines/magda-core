"""Break the stub one way at a time, and check the harness notices.

A suite where every check says OK proves nothing until each check has been
watched failing for its own reason. This copies the harness to a scratch
directory, applies one mutation to `selftest.py`'s stub MAGDA, runs the whole
harness against it, and asserts the check that exists to catch that regression
is the one that goes red.

    python3 tools/transport_check/mutation_test.py

Every mutation below is a regression MAGDA could really ship — auth bypassed,
a mirrored header unvalidated, a permission ignored, a revision frozen, OSC
gone quiet. Adding a check to `suites.py` without adding a mutation here leaves
a line that can only ever print OK.

Slow by construction: it runs the full harness once per mutation.
"""

from __future__ import annotations

import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HARNESS = Path(__file__).resolve().parent

#: The one check the stub cannot satisfy, so it fails in every run including
#: the baseline. See `selftest.EXPECTED_FAILURES`.
ALWAYS_FAILS = {"magda-mcp is staged beside the MAGDA executable"}

#: (what is broken, the text to replace in selftest.py, what to replace it with)
MUTATIONS: list[tuple[str, str, str]] = [
    (
        "MCP bearer token not checked",
        '        return self.headers.get("Authorization") == f"Bearer {TOKEN}"',
        "        return True",
    ),
    (
        "MCP Origin never refused",
        '        if self.headers.get("Origin"):\n'
        '            self._send_json(403, {"error": "origin not allowed"})\n'
        "            return True",
        "        pass",
    ),
    (
        "WebSocket Origin never refused",
        '            if "origin" in headers:\n'
        '                conn.sendall(b"HTTP/1.1 403 Forbidden\\r\\nContent-Length: 0\\r\\n\\r\\n")\n'
        "                return",
        "            pass",
    ),
    (
        "WebSocket auth accepts any token",
        '            if headers.get("authorization") != f"Bearer {TOKEN}":\n'
        '                conn.sendall(b"HTTP/1.1 401 Unauthorized\\r\\nContent-Length: 0\\r\\n\\r\\n")\n'
        "                return",
        "            pass",
    ),
    (
        "read-only client allowed to write over WebSocket",
        "if method in writes and client_name == clients.READONLY_CLIENT_NAME:",
        "if False:",
    ),
    (
        "read-only client allowed to write over MCP",
        'if name == "tracks.create" and client_name == clients.READONLY_CLIENT_NAME:',
        "if False:",
    ),
    (
        "resources/read disagrees with tools/call",
        '            result({"contents": [{"uri": uri, "mimeType": "application/json", '
        '"text": TRACKS_JSON}]})',
        '            result({"contents": [{"uri": uri, "mimeType": "application/json", '
        '"text": "{\\"tracks\\": []}"}]})',
    ),
    (
        "Mcp-Name mismatch not validated",
        "            if raw != params.get(name_field):\n"
        '                self._send_json(400, self._error(rid, -32020, "Mcp-Name mismatch"))\n'
        "                return",
        "            pass",
    ),
    (
        "clientCapabilities not required",
        "if not isinstance(meta.get(clients.META_CLIENT_CAPABILITIES), dict):",
        "if False:",
    ),
    (
        "a stateless request echoes a session id",
        '        self._dispatch(rid, method, params, era="modern", client_name=client_name)',
        "        if session:\n"
        '            self._send_json(200, {"jsonrpc": "2.0", "id": rid, "result": {"tools": TOOLS}},\n'
        '                            headers={"Mcp-Session-Id": session})\n'
        "            return\n"
        '        self._dispatch(rid, method, params, era="modern", client_name=client_name)',
    ),
    (
        "the acknowledgement echoes URIs that name nothing",
        '        agreed = [u for u in asked if u == "magda://tracks" or u == "magda://transport"]',
        "        agreed = list(asked)",
    ),
    (
        "an unknown method answers 200 instead of 404",
        '            self._send_json(404, self._error(rid, -32601, f"no such method {method}"))',
        '            self._send_json(200, self._error(rid, -32601, f"no such method {method}"))',
    ),
    (
        "an unknown tool reported as a tool execution error",
        '            self._send_json(200, self._error(rid, -32602, f"no tool named {name}"))',
        '            result({"isError": True, "content": [{"type": "text", "text": "no such tool"}]})',
    ),
    (
        "a write does not advance the revision",
        "            STATE['revision'] += 1\n            result({\"tempo\": STATE['tempo']})",
        "            result({\"tempo\": STATE['tempo']})",
    ),
    (
        "a stale expectedRevision is ignored",
        'if method in writes and "expectedRevision" in meta '
        "and meta[\"expectedRevision\"] != STATE['revision']:",
        "if False:",
    ),
    (
        "subscribe returns no snapshot",
        '            result({"snapshots": [',
        '            result({"snapshots": [] and [',
    ),
    (
        "OSC never answers a new surface",
        "            if peer[0] not in self.known:",
        "            if False:",
    ),
    (
        "an OSC command has no effect",
        "                    STATE['tempo'] = float(message.args[0])",
        "                    pass",
    ),
    (
        "legacy initialize mints no session header",
        '        }, headers={"Mcp-Session-Id": session})',
        "        }, headers={})",
    ),
    (
        "the token file is world-readable",
        "    record.chmod(0o600)",
        "    record.chmod(0o644)",
    ),
]

#: Mutations that only the official-SDK suite can catch, so they are skipped
#: when it is. Run this file with an interpreter that has `mcp` installed to
#: exercise them:
#:
#:     .venv/bin/python tools/transport_check/mutation_test.py
SDK_MUTATIONS: list[tuple[str, str, str]] = [
    (
        "modern results omit ttlMs and cacheScope",
        '                if method in CACHEABLE_RESULTS:\n'
        '                    value.setdefault("ttlMs", 0)\n'
        '                    value.setdefault("cacheScope", "private")',
        "                pass",
    ),
    (
        "modern results omit resultType",
        '                value.setdefault("resultType", "complete")',
        "                pass",
    ),
    (
        "server/discover omits capabilities, so a host falls back to legacy",
        '                "capabilities": {"tools": {"listChanged": False},\n'
        '                                 "resources": {"subscribe": True, "listChanged": False}},',
        "",
    ),
]


def sdk_available() -> bool:
    return all(importlib.util.find_spec(m) is not None for m in ("mcp", "httpx2"))


def run(mutation: tuple[str, str, str] | None) -> tuple[list[str], list[str], str]:
    """Run the harness against a copy of the stub, mutated or not."""
    workspace = Path(tempfile.mkdtemp(prefix="magda-transport-mutation-"))
    copy = workspace / "transport_check"
    shutil.copytree(HARNESS, copy)
    if mutation is not None:
        _, old, new = mutation
        target = copy / "selftest.py"
        text = target.read_text()
        if old not in text:
            raise LookupError(f"the stub no longer contains:\n{old}")
        target.write_text(text.replace(old, new, 1))
    try:
        completed = subprocess.run(
            [sys.executable, str(copy / "selftest.py")],
            capture_output=True, text=True, timeout=600,
        )
    finally:
        shutil.rmtree(workspace, ignore_errors=True)
    stdout = completed.stdout
    failed = re.findall(r"^  FAIL  (.+)$", stdout, re.M)
    # Inconclusive is a verdict too: the OSC snapshot check is specified to
    # report "could not decide" rather than fail, because silence there has an
    # innocent explanation the harness cannot rule out from outside.
    unclear = re.findall(r"^\s+\?\s\s(.+)$", stdout, re.M)
    return failed, unclear, stdout


def main() -> int:
    print("baseline, with the stub intact:")
    failed, base_unclear, stdout = run(None)
    if set(failed) != ALWAYS_FAILS:
        print(f"  expected only {sorted(ALWAYS_FAILS)} to fail, got {sorted(failed)}")
        print(stdout)
        return 1
    print(f"  clean ({len(base_unclear)} inconclusive, {len(failed)} expected failure)\n")

    mutations = list(MUTATIONS)
    if sdk_available():
        mutations += SDK_MUTATIONS
    else:
        print("  (skipping the official-SDK mutations: `mcp` is not importable "
              "by this interpreter)\n")

    caught = 0
    missed: list[str] = []
    for mutation in mutations:
        name = mutation[0]
        try:
            failed, unclear, stdout = run(mutation)
        except LookupError as exc:
            print(f"  STALE   {name}\n            {exc}")
            missed.append(name)
            continue
        new_failures = [f for f in failed if f not in ALWAYS_FAILS]
        new_unclear = [u for u in unclear if u not in base_unclear]
        if new_failures or new_unclear:
            caught += 1
            print(f"  caught  {name}")
            for entry in new_failures:
                print(f"            -> FAIL {entry}")
            for entry in new_unclear:
                print(f"            -> inconclusive: {entry}")
        else:
            missed.append(name)
            print(f"  MISSED  {name}")

    print(f"\n  {caught}/{len(mutations)} mutations caught")
    if missed:
        print("\n  Nothing went red for these, so no check is watching them:")
        for name in missed:
            print(f"    {name}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
