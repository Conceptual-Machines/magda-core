# Signalsmith Stretch

- Signalsmith Stretch 1.3.2, commit `57b93f4e9206a089a45387eaa39bdc9f310d3308`
  from https://github.com/Signalsmith-Audio/signalsmith-stretch
- Signalsmith Linear 0.3.1 from https://github.com/Signalsmith-Audio/linear

Both are header-only and MIT licensed; the licence texts sit beside the sources.
Unmodified.

The native engine (#1882) uses this as its default stretch engine, the one the pinned
mode value `kSignalsmith` in `magda/daw/core/TimeStretchModes.hpp` names. It is vendored
here rather than reached for inside the Tracktion fork because the engine may not include
Tracktion, and that boundary is checked at configure time.

The fork carries its own copy under
`third_party/tracktion_engine/modules/tracktion_engine/3rd_party/signalsmith`. The two
are the same version and the duplicate goes away with the fork.
