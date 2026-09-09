# Linux builds and audio crash diagnostics

After installing the build dependencies and fetching submodules and LFS assets
as described in the main README, build with a bounded number of compiler jobs:

```sh
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --target magda_daw_app -j4
```

Use fewer jobs if memory is limited. Fresh Release, RelWithDebInfo and MinSizeRel
builds default to `MAGDA_BUILD_TESTS=OFF`. Debug and multi-configuration generators
default to tests on. An explicit `-DMAGDA_BUILD_TESTS=ON` remains supported in
Release. CMake preserves an existing cached value when changing build type.
No NanoRange warning suppression flags are needed: the pinned Tracktion fork
backports upstream's removal of NanoRange (Tracktion/tracktion_engine#402).

## Capturing an audio startup crash (#2528)

Build this branch with symbols and run it on the affected Linux machine:

```sh
cmake -S . -B cmake-build-audio-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMAGDA_BUILD_TESTS=OFF
cmake --build cmake-build-audio-debug --target magda_daw_app -j4
gdb -batch \
  -ex 'set pagination off' \
  -ex 'handle SIGPIPE nostop noprint pass' \
  -ex run \
  -ex 'thread apply all bt full' \
  -ex 'info sharedlibrary' \
  --args cmake-build-audio-debug/magda/daw/magda_daw_app_artefacts/Debug/MAGDA \
  > magda-gdb.txt 2>&1
```

Keep the configuration that triggers the crash for this run. If startup succeeds,
close MAGDA and retry; the reported failure is intermittent. GDB collects all
thread stacks as soon as it stops at the crash, before continuing execution.

Attach `magda-gdb.txt` and the corresponding `magda.log` to the issue. On Linux
the default log is `~/.config/MAGDA/Logs/magda.log` (under `$XDG_CONFIG_HOME/MAGDA`
when set). If MAGDA's data directory was customised, look in its `Logs` directory;
if that directory could not be created, the fallback is `/tmp/MAGDA-Logs/magda.log`.
Copy the log before another run. Review attachments for device names, local paths
and other details you do not want to publish.

The `[AudioStartup]` entries record backend scans, saved device selection,
requested channel counts, the active sample rate/buffer size, and preference
changes. Messages before each device-opening step help locate failures that
happen before the call returns. They use the existing file logger in both Debug
and Release, with no logging added to the audio callback.

Also include your audio interface model, whether selecting PulseAudio avoids
the crash, and the output of:

```sh
uname -a
cat /etc/os-release
c++ --version
dpkg-query -W pipewire libpipewire-0.3-0 libspa-0.2-modules libasound2t64 libasound2-plugins
```

The existing report stops in PipeWire's audio conversion library on the
`alsa-pipewire` thread. These diagnostics collect evidence to distinguish a
MAGDA/JUCE setup problem from a PipeWire failure; they do not change backend
selection or claim to fix the runtime crash. PulseAudio remains the reporter's
known workaround.
