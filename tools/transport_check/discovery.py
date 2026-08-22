"""Finding a running MAGDA the way a real client does.

This deliberately reimplements `magda::paths::dataDir()` and the record sweep
in `magda/mcp_bridge/main.cpp` rather than shelling out to anything, because
resolving the record *is* one of the things #2059 asks to verify. A harness
that asked MAGDA where its record was would be assuming the answer.
"""

from __future__ import annotations

import ctypes
import errno
import json
import os
import stat
import sys
from dataclasses import dataclass
from pathlib import Path

RECORD_GLOB = "remote-api-*.json"


class DiscoveryError(Exception):
    pass


def os_default_app_data_dir() -> Path:
    """`juce::File::userApplicationDataDirectory` / "MAGDA" — `alwaysOSDefault()`.

    The three values JUCE resolves that special location to, per platform,
    read off `juce_Files_*` rather than assumed:

    - macOS is `~/Library`, **not** `~/Library/Application Support`. JUCE keeps
      the latter for `userApplicationDataDirectory` on no platform, and an app
      that wants it appends it itself. MAGDA does not, so its data lives in
      `~/Library/MAGDA`.
    - Windows is `CSIDL_APPDATA`, the roaming profile.
    - Linux is `XDG_CONFIG_HOME` or `~/.config`.
    """
    if sys.platform == "darwin":
        base = Path.home() / "Library"
    elif sys.platform == "win32":
        appdata = os.environ.get("APPDATA")
        base = Path(appdata) if appdata else Path.home() / "AppData" / "Roaming"
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    return base / "MAGDA"


def data_dir(override: str | os.PathLike[str] | None = None) -> Path:
    """Where discovery records live: env, then config, then the OS default.

    The same three steps the bridge takes. `config.json` itself never moves —
    only the data directory it names does — so the config is always read from
    the OS default location even when it redirects everything else.
    """
    if override is not None:
        return Path(override)
    if env := os.environ.get("MAGDA_DATA_DIR"):
        return Path(env)

    default = os_default_app_data_dir()
    config = config_file(default)
    if config.is_file():
        try:
            configured = json.loads(config.read_text(encoding="utf-8")).get("dataDir")
        except (OSError, ValueError):
            configured = None
        if configured:
            return Path(configured)
    return default


def config_file(default_dir: Path) -> Path:
    """Where MAGDA reads its configuration from — `paths::configFile()`.

    `MAGDA_CONFIG_FILE` moves the file itself, which is a separate thing from
    `MAGDA_DATA_DIR` moving everything the file points at. A harness that
    honoured only the second would read a stale `dataDir` out of the default
    config while MAGDA was using another one entirely.
    """
    if override := os.environ.get("MAGDA_CONFIG_FILE"):
        return Path(override)
    return default_dir / "config.json"


def process_alive(pid: int) -> bool:
    """Whether the process that wrote a record is still running.

    A record outliving its process is debris from a crash; its port may since
    have been handed to something else entirely, so trusting it would point
    the harness at an unrelated listener.
    """
    if pid <= 0:
        return False
    if sys.platform == "win32":
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return False
        try:
            code = ctypes.c_ulong()
            if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return False
            return code.value == STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except PermissionError:
        # Alive, and someone else's.
        return True
    except OSError as exc:
        if exc.errno == errno.ESRCH:
            return False
        raise
    return True


@dataclass
class Record:
    """A parsed `remote-api-<pid>.json`."""

    path: Path
    pid: int
    token: str
    port: int
    url: str
    mcp_port: int | None
    mcp_url: str | None
    mtime: float

    @property
    def has_mcp(self) -> bool:
        return bool(self.mcp_url) and bool(self.mcp_port)

    def owner_only(self) -> bool | None:
        """True iff the record is readable by its owner alone.

        #2059 asks that the credential is written with owner-only permissions
        from an *installed* build, which is a property of the file rather than
        of any request, so it is checked here. Windows has no mode bits worth
        reading — the file inherits the per-user directory's ACL — so this
        returns None there rather than a misleading answer.
        """
        if sys.platform == "win32":
            return None
        mode = self.path.stat().st_mode
        return not (mode & (stat.S_IRWXG | stat.S_IRWXO))

    def mcp_origin_and_path(self) -> tuple[str, str]:
        """Split `http://127.0.0.1:51735/mcp` into origin and path."""
        if not self.mcp_url:
            raise DiscoveryError("record carries no mcpUrl")
        scheme, _, rest = self.mcp_url.partition("://")
        host, slash, path = rest.partition("/")
        return f"{scheme}://{host}", (slash + path) if slash else "/mcp"


def parse_record(path: Path) -> Record | None:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    try:
        return Record(
            path=path,
            pid=int(raw.get("pid", 0)),
            token=str(raw.get("token", "")),
            port=int(raw.get("port", 0)),
            url=str(raw.get("url", "")),
            mcp_port=int(raw["mcpPort"]) if raw.get("mcpPort") else None,
            mcp_url=str(raw["mcpUrl"]) if raw.get("mcpUrl") else None,
            mtime=path.stat().st_mtime,
        )
    except (TypeError, ValueError):
        return None


def find_records(directory: Path) -> list[Record]:
    """Every live record in `directory`, most recently written first.

    Most-recent-first is the bridge's own rule for picking between two running
    instances: it is the one the user most likely just started.
    """
    if not directory.is_dir():
        return []
    found = []
    for path in sorted(directory.glob(RECORD_GLOB)):
        record = parse_record(path)
        if record is None or not record.token or not process_alive(record.pid):
            continue
        found.append(record)
    return sorted(found, key=lambda r: r.mtime, reverse=True)


def resolve(
    record_path: str | os.PathLike[str] | None = None,
    data_dir_override: str | os.PathLike[str] | None = None,
) -> tuple[Record, list[Record]]:
    """The record to test against, plus every other live one found.

    Returning the others matters: two instances is a supported thing to run,
    and a tester who does not know a second copy is up would otherwise read a
    confusing result off the wrong one.
    """
    if record_path is not None:
        path = Path(record_path)
        record = parse_record(path)
        if record is None:
            raise DiscoveryError(f"{path} is not a readable discovery record")
        return record, []

    directory = data_dir(data_dir_override)
    records = find_records(directory)
    if not records:
        raise DiscoveryError(
            f"no live MAGDA found in {directory}\n"
            "  Start MAGDA and switch on AI Settings -> Remote, or point at a\n"
            "  record with --record / a directory with --data-dir."
        )
    return records[0], records[1:]
