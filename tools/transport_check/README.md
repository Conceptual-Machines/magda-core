# magda-transport-check

Drives a **running MAGDA** over every remote transport and reports what
answered: JSON-RPC over WebSocket, MCP in both protocol eras over HTTP, MCP
over stdio through `magda-mcp`, and OSC over UDP.

```bash
python3 tools/transport_check --all
```

Python 3.9+, no dependencies. That is deliberate: this is meant to be run
against an *installed* build, often on a machine that is not a dev box, and
"create a virtualenv first" is a step that stops people running it.

One suite is the exception. `--mcp-sdk` drives the endpoint with the **official
MCP SDK**, which needs Python 3.10+ and an install; it is skipped, with a note
saying why, when it is not importable. See [A second, independent
host](#a-second-independent-host) — it is the suite most likely to find
something.

## Why it exists

The in-tree tests already drive these transports over real sockets, and
`test_remote_mcp_bridge.cpp` forks the real bridge binary. What none of them can
establish is the thing [#2059](https://github.com/Conceptual-Machines/magda-core/issues/2059)
asks for: whether an installed artefact answers a client written from
`docs/remote-api-mcp.md` rather than from the source. This is that client. It
shares no code with the app — it re-derives the discovery record location, the
header rules and the `_meta` shapes from the documentation, so a disagreement
between the docs and the build shows up here as a failing check.

## What it needs

| Suite | Needs |
|---|---|
| everything | MAGDA running with **AI Settings → Remote** switched on |
| `--osc` | **OSC enabled** in the Controllers settings (it is off by default) |
| `--bridge` | `magda-mcp` beside the running executable, on `PATH`, or `--bridge-path` |
| `--mcp-sdk` | Python 3.10+ with `pip install -r tools/transport_check/requirements.txt` |

MAGDA is found the way the bridge finds it — the newest live
`remote-api-<pid>.json` under `MAGDA_DATA_DIR`, the `dataDir` in `config.json`,
or the per-user app data directory, in that order.

## Running it

```bash
python3 tools/transport_check --all          # every suite, nothing mutated
python3 tools/transport_check --ws --osc     # just those two
python3 tools/transport_check --all --write  # add the mutating checks
python3 tools/transport_check --all --json   # machine-readable report
```

Exit status is `0` if nothing failed, `1` if something did, `2` if no running
MAGDA could be found.

### `--write`

Default runs change nothing. `--write` adds the checks that have to mutate to
mean anything — that a write advances the revision, that a stale
`expectedRevision` is refused, that an OSC message reaches the project — and
each one reverts what it touched. It points at whatever project you have open,
so it leaves a couple of undo steps behind. Use a scratch project.

## A second, independent host

Everything above is a client written from `docs/remote-api-mcp.md`. That shares
a blind spot with MAGDA's own tests: it asks the way the documentation says to,
so a disagreement between the documentation and the specification is invisible
to both.

`--mcp-sdk` closes that. The official SDK validates every response against the
protocol schema for the version it negotiated, so it rejects shapes a
hand-written client accepts:

```bash
python3 -m venv .venv
.venv/bin/pip install -r tools/transport_check/requirements.txt
.venv/bin/python tools/transport_check --mcp-sdk
```

It also reports **which era it negotiated**. The SDK probes the modern era with
`server/discover` and falls back to the `initialize` handshake if that probe
does not take, so a modern endpoint that answers `server/discover` in a shape
the client cannot parse silently serves every host its legacy path. That shows
up here as an inconclusive verdict naming the version it settled for, rather
than as a pass.

### What the schema requires of a modern result

In `2026-07-28` a cacheable result must carry `ttlMs` and `cacheScope` beside
`resultType` — `tools/list`, `resources/list`, `resources/templates/list`,
`resources/read` and `server/discover` all do. `tools/call` does not. The
legacy schema requires none of it. A server that stamps `resultType` alone is
refused by a validating client on every one of those methods, which is a
failure no amount of testing against a permissive client will show.

## Sharing the endpoint

The MCP endpoint's rate limit is **one bucket for the whole endpoint**, not one
per client — deliberately, since every caller arrives from 127.0.0.1 presenting
the same token, so nothing there can tell two local processes apart. The bucket
holds `maxConcurrentRequests` tokens (eight) and refills at
`maxRequestsPerSecond`.

Two consequences:

- The harness paces itself (~40 req/s) and rides out a 429 with backoff rather
  than reporting it. Without that, a dozen checks drain the bucket and every
  check after them fails with a 429 that says nothing about what it was
  checking. `--cooldown` sets the pause between suites.
- **Anything else you have configured against this MAGDA spends from the same
  budget** — an MCP host in an editor, say. If the run reports that it was
  throttled, that is what it means. Quit the other client for a clean run.

## The four verdicts

`OK` and `FAIL` are what they look like. The other two carry weight:

- **`skip`** — the check did not run. A missing `magda-mcp`, `--write` not
  passed, no `mcpUrl` in the record.
- **`?` (inconclusive)** — the check ran and could not reach a verdict. Some
  of what this asks cannot be decided from outside the app. The clearest case
  is the OSC snapshot: MAGDA sends a full state dump to a surface it has not
  heard from before and nothing to one it already knows, so a second run in one
  MAGDA session sees silence that means nothing is wrong. Reporting that as
  `FAIL` would train you to ignore red, so it gets its own colour and a
  sentence saying what else it could be.

## Clients it registers

Two entries appear in **AI Settings → Clients** after a run:

| Name | Grant it |
|---|---|
| `magda-transport-check` | `edit` and `transport`, if you want `--write` to work |
| `magda-transport-check-readonly` | **nothing, ever** |

The second exists because "a new client can only read" is the first thing a
real user hits, and checking it needs a client the user has not trusted.
Granting that name anything makes those checks pass for the wrong reason — the
harness says so in its output rather than quietly going green.

## Checking the harness itself

```bash
python3 tools/transport_check/selftest.py
```

Stands up a stub MAGDA — WebSocket, MCP in both eras, OSC, and a stand-in
bridge — and runs the whole harness against it. Needs no MAGDA and no network,
so it belongs anywhere CI can run Python.

One check is *expected* to fail there: `magda-mcp is staged beside the MAGDA
executable` looks beside the binary behind the record's pid, which under the
self-test is the Python interpreter. The self-test asserts that it fails, since
a pass would mean the check was not looking at anything.

### Proving the checks are not vacuous

```bash
python3 tools/transport_check/mutation_test.py
```

Breaks the stub one way at a time — auth bypassed, `Mcp-Name` unvalidated,
permissions ignored, revisions frozen, OSC silenced — and asserts that the
check which exists to catch that regression is the one that goes red. All 20
mutations are currently caught — 20 always, plus 3 more when the official
SDK is installed.

This is what keeps the suite honest. **When you add a check to `suites.py`, add
the matching mutation here**; if nothing goes red without it, you have added a
line that can only ever print `OK`. It runs the whole harness once per
mutation, so it is slow — a minute or two — and is not part of `make test`.

## Layout

| File | |
|---|---|
| `cli.py` | argument parsing and suite selection |
| `discovery.py` | finding a live MAGDA; record parsing and permissions |
| `wire.py` | WebSocket client, OSC codec, SSE reader — all stdlib |
| `clients.py` | JSON-RPC / MCP-HTTP / MCP-stdio / OSC clients |
| `suites.py` | the checks |
| `report.py` | verdicts and console rendering |
| `sdk_suite.py` | the same endpoint driven by the official MCP SDK |
| `selftest.py` | the stub MAGDA and the harness's own test |
| `mutation_test.py` | breaks the stub to prove each check can fail |
