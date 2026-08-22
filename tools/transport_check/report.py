"""Results and console rendering for the transport check.

The harness exists to be run by a person against a build they just installed,
so the output is the product: a line per check, the reason on failure, and an
exit status CI can read. `--json` emits the same thing for a machine.
"""

from __future__ import annotations

import enum
import json
import os
import sys
import time
from dataclasses import dataclass, field
from typing import Any


class Status(enum.Enum):
    """What a check concluded.

    `INCONCLUSIVE` is separate from `FAIL` on purpose. Some of what this
    harness asks cannot be established from outside the app — whether an OSC
    snapshot was withheld because feedback is broken or because this host was
    already a known peer, for one — and reporting that as a failure would
    train the reader to ignore red. It is a distinct, non-fatal state that
    names what it could not decide.
    """

    PASS = "pass"
    FAIL = "fail"
    SKIP = "skip"
    INCONCLUSIVE = "inconclusive"


@dataclass
class Check:
    """One assertion, its verdict, and enough detail to act on it."""

    suite: str
    name: str
    status: Status
    detail: str = ""
    duration_ms: float = 0.0
    #: Anything worth keeping for the JSON report but too long for a console
    #: line — a protocol version, a returned error body, a port number.
    data: dict[str, Any] = field(default_factory=dict)

    def to_json(self) -> dict[str, Any]:
        out = {
            "suite": self.suite,
            "name": self.name,
            "status": self.status.value,
            "durationMs": round(self.duration_ms, 1),
        }
        if self.detail:
            out["detail"] = self.detail
        if self.data:
            out["data"] = self.data
        return out


class Report:
    """Collects checks and prints them as they land.

    Streaming rather than batching: a suite can block for seconds on a socket
    that will never answer, and a reader watching nothing happen needs to know
    which check is hanging.
    """

    _COLOURS = {
        Status.PASS: "\033[32m",
        Status.FAIL: "\033[31m",
        Status.SKIP: "\033[90m",
        Status.INCONCLUSIVE: "\033[33m",
    }
    _LABELS = {
        Status.PASS: "OK",
        Status.FAIL: "FAIL",
        Status.SKIP: "skip",
        Status.INCONCLUSIVE: "?",
    }

    def __init__(self, verbose: bool = False, colour: bool | None = None) -> None:
        self.checks: list[Check] = []
        self.verbose = verbose
        if colour is None:
            colour = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
        self.colour = colour
        self._suite_open: str | None = None

    # -- emitting ---------------------------------------------------------

    def note(self, message: str) -> None:
        """A line that is not a check — discovery detail, a warning."""
        print(f"  {self._dim(message)}")

    def add(self, check: Check) -> Check:
        self.checks.append(check)
        if check.suite != self._suite_open:
            print(f"\n{self._bold(check.suite)}")
            self._suite_open = check.suite
        label = self._paint(check.status, self._LABELS[check.status].rjust(4))
        line = f"  {label}  {check.name}"
        if check.detail and check.status is not Status.PASS:
            line += f"\n        {self._dim(check.detail)}"
        elif check.detail and self.verbose:
            line += f"\n        {self._dim(check.detail)}"
        print(line, flush=True)
        return check

    # -- summary ----------------------------------------------------------

    def counts(self) -> dict[Status, int]:
        return {s: sum(1 for c in self.checks if c.status is s) for s in Status}

    @property
    def failed(self) -> bool:
        return any(c.status is Status.FAIL for c in self.checks)

    def summarise(self) -> None:
        counts = self.counts()
        parts = [self._paint(Status.PASS, f"{counts[Status.PASS]} passed")]
        if counts[Status.FAIL]:
            parts.append(self._paint(Status.FAIL, f"{counts[Status.FAIL]} failed"))
        if counts[Status.INCONCLUSIVE]:
            parts.append(
                self._paint(Status.INCONCLUSIVE, f"{counts[Status.INCONCLUSIVE]} inconclusive")
            )
        if counts[Status.SKIP]:
            parts.append(self._paint(Status.SKIP, f"{counts[Status.SKIP]} skipped"))
        print("\n  " + ", ".join(parts) + "\n")

    def to_json(self) -> str:
        counts = self.counts()
        return json.dumps(
            {
                "ok": not self.failed,
                "counts": {s.value: counts[s] for s in Status},
                "checks": [c.to_json() for c in self.checks],
            },
            indent=2,
        )

    # -- painting ---------------------------------------------------------

    def _paint(self, status: Status, text: str) -> str:
        if not self.colour:
            return text
        return f"{self._COLOURS[status]}{text}\033[0m"

    def _bold(self, text: str) -> str:
        return f"\033[1m{text}\033[0m" if self.colour else text

    def _dim(self, text: str) -> str:
        return f"\033[90m{text}\033[0m" if self.colour else text


class timed:
    """Context manager that records how long a check took.

    Used by the suites so every check carries a duration without each one
    having to remember to measure — the durations are what tell a reader that
    a `subscriptions/listen` stream really did stay open for its full window
    rather than returning instantly.
    """

    def __init__(self) -> None:
        self.ms = 0.0
        self._start = 0.0

    def __enter__(self) -> "timed":
        self._start = time.monotonic()
        return self

    def __exit__(self, *exc: object) -> None:
        self.ms = (time.monotonic() - self._start) * 1000.0
