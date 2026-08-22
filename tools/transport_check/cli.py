"""magda-transport-check — drive a running MAGDA over every remote transport.

    python3 tools/transport_check --all

What the in-tree tests cannot establish is whether an *installed* build answers
a client that was written from the documentation. This is that client. It finds
MAGDA the way the bridge does, then speaks JSON-RPC over WebSocket, MCP in both
protocol eras over HTTP, MCP over stdio through `magda-mcp`, and OSC over UDP,
asserting the things #2059 lists as needing doing against a build.

Nothing it does by default changes the open project. `--write` adds the checks
that have to mutate to mean anything; they revert what they touch.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# Running the directory puts it on sys.path, so the modules beside this one
# import by bare name whether this is run as a directory, a file, or -m.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import discovery  # noqa: E402
import suites  # noqa: E402
from report import Report  # noqa: E402

SUITES = ("discovery", "ws", "mcp", "mcp_sdk", "bridge", "readonly", "osc")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="magda-transport-check",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "MAGDA must be running with Connections -> MCP and/or WebSocket on.\n"
            "The OSC suite additionally needs OSC enabled in the Controllers settings.\n"
        ),
    )
    selection = parser.add_argument_group("what to run (default: everything)")
    selection.add_argument("--all", action="store_true", help="run every suite")
    selection.add_argument("--ws", "--websocket", dest="ws", action="store_true",
                           help="the WebSocket transport")
    selection.add_argument("--mcp", action="store_true",
                           help="the MCP endpoint, both protocol eras")
    selection.add_argument("--mcp-sdk", dest="mcp_sdk", action="store_true",
                           help="the MCP endpoint driven by the official SDK "
                                "(needs Python 3.10+ and `pip install mcp`)")
    selection.add_argument("--bridge", action="store_true",
                           help="MCP through the magda-mcp stdio bridge")
    selection.add_argument("--readonly", action="store_true",
                           help="that an ungranted client is refused a write")
    selection.add_argument("--osc", action="store_true", help="the OSC listener and its feedback")

    behaviour = parser.add_argument_group("behaviour")
    behaviour.add_argument("--write", action="store_true",
                           help="also run the checks that mutate the open project "
                                "(each reverts what it changed)")
    behaviour.add_argument("--stream-window", type=float, default=3.0, metavar="SECONDS",
                           help="how long to hold a notification stream open (default: 3)")
    behaviour.add_argument("--timeout", type=float, default=10.0, metavar="SECONDS",
                           help="per-request timeout (default: 10)")
    behaviour.add_argument("--cooldown", type=float, default=1.5, metavar="SECONDS",
                           help="pause between suites so the endpoint's shared rate "
                                "limit bucket refills (default: 1.5)")

    where = parser.add_argument_group("where to look")
    where.add_argument("--record", metavar="PATH", help="use this discovery record")
    where.add_argument("--data-dir", metavar="PATH",
                       help="look for discovery records here (as MAGDA_DATA_DIR would)")
    where.add_argument("--bridge-path", metavar="PATH",
                       help="the magda-mcp binary to drive, if it is not beside MAGDA")
    where.add_argument("--osc-port", type=int, default=9000, metavar="PORT",
                       help="MAGDA's OSC receive port (default: 9000)")
    where.add_argument("--osc-feedback-port", type=int, default=9001, metavar="PORT",
                       help="the port MAGDA sends feedback to (default: 9001)")

    output = parser.add_argument_group("output")
    output.add_argument("--json", action="store_true", help="emit the report as JSON on stdout")
    output.add_argument("-v", "--verbose", action="store_true",
                        help="show the detail line for passing checks too")
    output.add_argument("--no-colour", "--no-color", dest="colour",
                        action="store_false", default=None, help="never colour the output")
    return parser


def chosen(args: argparse.Namespace) -> set[str]:
    picked = {name for name in SUITES if getattr(args, name.replace("-", "_"), False)}
    if args.all or not picked:
        return set(SUITES)
    # Discovery is the run's own preamble: every other suite needs the record
    # it resolves, so it is never opt-out.
    picked.add("discovery")
    return picked


def run(argv: list[str] | None = None) -> Report | None:
    """Run the selected suites and return the report, or None if no MAGDA was found.

    Split from `main` so `selftest.py` can inspect the findings rather than
    only the exit status — a self-test that could see nothing but "non-zero"
    could not tell a harness bug from the check it is supposed to fail.
    """
    args = build_parser().parse_args(argv)
    selected = chosen(args)

    report = Report(verbose=args.verbose, colour=args.colour)

    try:
        record, others = discovery.resolve(args.record, args.data_dir)
    except discovery.DiscoveryError as exc:
        print(f"\n  {exc}\n", file=sys.stderr)
        return None

    print(f"\n  MAGDA pid {record.pid}"
          + (f"  ws :{record.port}" if record.has_websocket else "  ws: not listening")
          + (f"  mcp :{record.mcp_port}" if record.has_mcp else "  mcp: not listening"))
    print(f"  {record.path}")
    if args.write:
        print("\n  --write: the mutating checks will run and revert what they change.")

    context = suites.Context(
        record=record,
        report=report,
        write=args.write,
        stream_window=args.stream_window,
        osc_send_port=args.osc_port,
        osc_feedback_port=args.osc_feedback_port,
        bridge_path=args.bridge_path,
        data_dir=Path(args.data_dir) if args.data_dir else None,
        timeout=args.timeout,
    )

    def cooldown() -> None:
        """Let the endpoint's token bucket refill between suites.

        One bucket serves the whole endpoint and the SSE checks hold a
        connection for their whole window, so a suite that starts the instant
        the last one finished can be refused for reasons that have nothing to
        do with what it is checking.
        """
        time.sleep(args.cooldown)

    if "discovery" in selected:
        suites.run_discovery(context, others)
    cooldown()
    if "ws" in selected:
        suites.run_websocket(context)
    cooldown()
    if "mcp" in selected:
        suites.run_mcp_modern(context)
        suites.run_mcp_legacy(context)
    cooldown()
    if "mcp_sdk" in selected:
        from sdk_suite import run_sdk

        run_sdk(context)
    cooldown()
    if "bridge" in selected:
        suites.run_bridge(context)
        suites.run_bridge_without_magda(context)
    cooldown()
    if "readonly" in selected:
        suites.run_readonly(context)
    cooldown()
    if "osc" in selected:
        suites.run_osc(context)

    throttled = context.throttled()
    if throttled:
        report.note(
            f"the endpoint rate-limited this run {throttled} time(s) and the harness "
            "backed off. The limit is one bucket for the whole endpoint, so another "
            "client — an MCP host you have configured, for instance — shares it."
        )

    report.summarise()
    if args.json:
        print(report.to_json())

    return report


def main(argv: list[str] | None = None) -> int:
    report = run(argv)
    if report is None:
        return 2
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
