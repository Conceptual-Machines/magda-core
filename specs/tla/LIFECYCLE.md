# Device restart and native-host teardown

These models continue #2863 after the plan-swap and hand-back models. They share
the callback-removal boundary but check separate protocols:

| Model | Code | What is checked |
| --- | --- | --- |
| `device_restart` | `EngineHost::Impl::handleAsyncUpdate`, `rebuild`, `audioDeviceAboutToStart`, `audioDeviceStopped`, `audioDeviceIOCallbackWithContext` | No rendering through a freed session or with a mismatched sample rate/block size; eventual readiness after restarts stop |
| `host_teardown` | `EngineHost::Impl::~Impl`, `detach`, JUCE `AsyncUpdater`, `AudioDeviceManager::removeAudioCallback` | Drain in-flight audio before releasing resources; unregister listeners and stop the timer; cancel the update posted by removal; late queued messages do not invoke a destroyed owner; teardown eventually completes |

Host code is in `magda/daw/engine/host/EngineHost.cpp`. `AudioIOService::openFitted`
uses its JUCE manager to reopen or switch devices. These models begin at the
manager's notifications, not at platform-specific device discovery or opening.

The JUCE contracts were checked against the repository's `third_party/JUCE`:

- `juce_AudioDeviceManager.cpp`: audio rendering and device start/stop
  notifications hold `audioCallbackLock`. Removal takes that lock, removes the
  callback, then calls `audioDeviceStopped` outside it. Addition invokes
  `audioDeviceAboutToStart` before inserting the callback.
- `juce_AsyncUpdater.cpp`: cancellation clears the delivery flag. The queued
  message can outlive its owner and dispatch safely while that flag is clear.
  Cancellation does not join a handler already executing on another thread.

## Device restart

The device can change sample rate and block size independently between two
symbolic values each. The message thread reads them separately. The generation
check and the subsequent ready-generation store are also separate actions: a
restart between them must not make a stale session eligible for rendering.

A rebuild removes and drains the callback before freeing the session. Adding
it back generates a new notification and schedules another handler; the
rebuilding handler returns without opening the gate. A later handler can mark
that generation ready once its observed configuration matches the preparation.

The initial state is an already prepared, running device. Restarts include
unchanged settings, changed settings, and multiple restarts before the handler
catches up. A physical restart holds the callback lock across its stop/start
notifications. Restarts while the host is unregistered are excluded; this model
assumes the configuration stays stable during that detached rebuild interval.
It therefore does **not** establish correctness for a device disappearing or
changing again while the callback is detached. Reattachment to no device,
backend open failures, hardware channel-map contents, MIDI, capture, offline
render suspension, and plugin preparation failure are also outside its scope.

Successful plan preparation and publication are abstracted. The session IDs
are not reused. `SessionOwned` checks the live session handle; assertions at
render check the handle saved by the callback and the prepared configuration.
`RebuildQuiescent` requires the entire callback to have left before rebuilding,
including a callback that is muted at the generation gate.
There is no claim about uninterrupted audio: silence while generations differ
is the intended behaviour. `EventuallyReady` assumes device changes stop,
callbacks finish, and message-thread work is weakly fair.

## Native-host teardown

The message loop may dispatch timer events, listener events, or queued async
updates before destruction starts. A finite callback budget permits audio to
be in flight at that point. Callback removal waits for it and then posts the
stop notification's async update. Listener removal and cancellation precede
resource destruction. The model explicitly dispatches queued updater and timer
messages after destruction; their delivery flags must suppress owner access.

Destruction, timer/listener delivery, and `handleAsyncUpdate` are on the same
message thread. This serialization is an assumption, not a guarantee provided
by `cancelPendingUpdate`. The model covers the normal attached-host destructor
path. It excludes repeated stop/start, a never-started host, reentrant message
pumping during destruction, other threads destroying the host, plugin-loader
callbacks, MIDI callbacks, and the internal joins of voice/prefetch workers.
Those producers need their own lifetime protocols; an updater cancellation
cannot stand in for stopping all producers.

## What this says about the JUCE test shutdown

This is **not a proof that the flaky `magda_juce_tests` process shutdown is fixed**.
`tests/JuceTestStateGuard.hpp` uses
`removeAllJobs(false, 10000)` followed by `runDispatchLoopUntil(10)`, both before
and after resetting project state. The return value of the job drain is ignored.
JUCE's `ThreadPool::removeAllJobs` can return false with a running job still
active; it only waits for the jobs captured at the beginning of that call.
A short message-loop pump also does not prevent subsequent work from being
posted. This helper provides best-effort test isolation, not a general shutdown
barrier. `tests/juce_tests_main.cpp` still uses `std::_Exit()` to bypass static
destruction.

No new C++ crash or use-after-free is claimed here. Completing that candidate
requires identifying the particular surviving job/callback, its owner and
cancel/join protocol, then reproducing any counterexample in the real test
binary. The broader issue remains open.

## Running and bounds

```sh
make tla ARGS="device_restart"
make tla ARGS="host_teardown"
```

The existing local runner discovers these configurations automatically. The
algorithms and generated translations are kept together in each `.tla` file.
All processes use weak fairness. These are interleaving models with coherent
atomic observations, not C++ weak-memory proofs or real-time deadline proofs.

| Configuration | Bounds | Distinct states |
| --- | --- | ---: |
| Device Safety | 2 restarts, 2 callbacks, 2 rates, 2 block sizes | 327,660 |
| Device Liveness | 1 restart, 2 callbacks, 2 rates, 2 block sizes | 10,300 |
| Teardown Safety | 3 callbacks, 3 pre-destruction message-loop iterations | 732 |
| Teardown Liveness | 2 callbacks, 2 pre-destruction message-loop iterations | 349 |

All four configurations passed with pinned `tla2tools` v1.7.4. These bounds and
the environment assumptions above delimit that result.

Eight deliberately broken temporary copies produced the expected counterexamples:

| Mutation | Failure |
| --- | --- |
| Bypass the device generation gate | Prepared-context assertion |
| Remove the callback without draining it before rebuild | `RebuildQuiescent` |
| Store the current generation instead of the handler's captured generation | Prepared-context assertion |
| Discard the retry after a generation changes during the handler | `EventuallyReady` |
| Tear down without waiting for an in-flight callback | `NoRenderAfterRelease` |
| Omit async cancellation | `DetachedAtDestruction` |
| Cancel before removal, allowing `audioDeviceStopped` to post again afterward | `DetachedAtDestruction` |
| Leave the timer running | `DetachedAtDestruction` |

These mutations validate the checks; they are not C++ defects found in the
unmodified protocol. In particular, a missing drain can overlap a rebuild with
an already muted callback without reaching the freed-session assertion. The
explicit quiescence invariant checks the stronger callback-removal contract.
