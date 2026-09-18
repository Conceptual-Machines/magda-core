# Native hardware output routing (#2272)

An explicit hardware output bypasses the master bus. The track's fader, pan,
mute, sends and meters still run; the final output operation writes to its
selected channels. Multiple tracks selecting the same destination sum there.

The host resolves saved output names using the current audio interface's
channel catalog and active channel mask. Physical channel numbers are mapped
to packed callback indices, so selecting outputs 3–4 works both when channels
1–4 are active and when only 3–4 are active. The internal processing graph stays
stereo regardless of how many hardware channels are open.

Existing `stereo:<name>` and bare output names remain project data. They are
resolved against the current interface rather than replaced by callback indices
in the project. If a saved destination is unavailable, its direct output is
silent and the compiler reports the name. Internal track routes and the default
master destination keep their existing behavior.

Mono destinations carry the track's left channel, matching Tracktion's channel
remapping contract. Stereo destinations carry left and right independently.

Offline export has one stereo destination. Before compiling that render, the
offline model redirects hardware-routed tracks to master. This preserves the
existing export mix and includes those tracks even without the original audio
interface. It does not change the saved project.

Automated tests cover routing, rendered channel isolation, output map changes,
and export behavior. Physical listening checks are tracked separately in
[the hardware output smoke checklist](../smoke-tests/native-hardware-outputs.md).

Hardware I/O ownership and opening channel masks remain part of #2588.
