# Parity envelope bench

The first slice of the parity envelope suite (#2082): the native engine measured against
Tracktion on the real-project corpus, at matched sample rate and block size, on one machine.

```bash
make parity-bench                                   # every project at 128 and 512 samples
make parity-bench PARITY_ARGS="--projects project.demo --block-sizes 256"
python3 scripts/parity_bench.py --report            # re-judge this machine's last run
```

`make parity-bench` builds `magda_parity_bench` in `cmake-build-parity`, a Release tree with
tests off (`MAGDA_BUILD_PARITY_BENCH=ON`), so both engines are compiled as they ship: no test
hooks and no assertions. It then runs `scripts/parity_bench.py`, which runs the bench once per
engine, project and block size. Each measurement is its own process. The script exits non-zero
when a native figure misses its threshold.

## What is measured

Both engines are the app's own. `createDefaultAudioEngine` builds them headless. A device with
no hardware behind it is registered with each engine's device manager and opened through its
`AudioIOControl::apply`. A realtime thread pulls that device once per block period, so every
figure comes from the engine's real audio callback, its own worker threads and its own
background readers.

The project goes through `ProjectManager::commitStagedProject`, the commit both load paths use.
The fixture rig stages it with every source pointed at its stand-in material
(`tests/MgdFixtures.cpp`).

| Figure | How |
| --- | --- |
| `load_ms` | From the commit until the engine renders all of the model: the plan published and settled (native), or the graph built and every proxy rendered (Tracktion). Includes waiting for every external plugin to load. |
| `cpu_mean_us`, `cpu_p99_us` | Wall time of each device callback, over `--passes` passes of the project's window. One untimed pass comes first. Pulled at the device's pace (`--speed` 1 is real time), so worker threads sleep and wake as they do live. |
| `session_mb` | Highest physical footprint during load and play, minus the footprint just before the commit. The footprint is sampled every 5 ms: `phys_footprint` on macOS, resident set on Linux, private commit on Windows. |
| `latency_samples` | The latency the engine reports. A second run takes every clip out, adds one track playing a seeded noise burst at the window's start, and exports the window through the engine's own offline render. The export is trimmed by the reported latency, so the burst lands late by exactly what the report left out. The burst is found by cross-correlation, because some projects keep sounding with no clips (test tones, generators). |

The JSON also carries figures that are recorded but not gated:

- `p50`, `p95` and `max` of the callback times.
- Callbacks that overran their block.
- Process CPU per block, across every thread.
- The output peak. A silent run is failed rather than measured.

## Thresholds

`tests/parity/thresholds.json`. A native figure passes when it is at most
`fork * ratio + slack`, where `fork` is Tracktion's figure from the same run on the same
machine. The slack is the noise floor, so a sub-millisecond project is not failed by jitter.

On top of the threshold, a native reported latency that differs from its measured latency fails
on its own. The same disagreement on Tracktion is printed as a note.

Where native is deliberately different, the difference goes in `divergences` with its
mechanism. The run then prints it as `declared` instead of failing it.

## History

Every run is appended to `~/.magda-parity/<machine>.jsonl`. Override the directory with
`--history-dir` or `MAGDA_PARITY_HISTORY`. The machine key hashes the CPU, core count, memory
and architecture; the OS version is recorded beside each run. The report prints each native
figure's change against this machine's previous run with the same build and settings. No
number is ever compared across machines.

## Not covered here

- Dropouts under stress (small buffers, heavy sessions, edits during playback) and round-trip
  latency need a real audio device. They build on the smoke tests' live run.
- A project whose external plugin is not installed on this machine is reported `not run`. The
  plugins are searched for by name in the default plugin folders, as the null-diff corpus does.
