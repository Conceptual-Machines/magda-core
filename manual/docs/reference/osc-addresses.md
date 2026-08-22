# OSC Address Reference

MAGDA answers a fixed set of OSC addresses out of the box. A stock TouchOSC mixer template drives it with no mapping step and no learn gesture: point the surface at MAGDA's receive port and the addresses below already mean something.

Turning the socket on, choosing which interfaces answer, and setting the receive and feedback ports all happen in **Settings → Connections → OSC**. See [Connections](../interface/connections.md#osc) for those, and for the status area that tells you which surfaces MAGDA has heard from.

## Transport

| Address | Argument | Meaning |
|---|---|---|
| `/magda/transport/play` | none, or non-zero | Start playback |
| `/magda/transport/stop` | none, or non-zero | Stop playback |
| `/magda/transport/record` | `0`/`1`, or none | Recording on or off; no argument flips it |
| `/magda/transport/loop` | `0`/`1`, or none | Loop on or off; no argument flips it |
| `/magda/transport/tempo` | float BPM | Set the tempo |
| `/magda/transport/position` | float beats | Locate to a position |
| `/magda/transport/seek` | float beats | Move by this much, clamped at zero |
| `/magda/transport/seek/bars` | int bars | Move by this many bars, meter-aware |

## Tracks

| Address | Argument | Meaning |
|---|---|---|
| `/magda/track/{n}/volume` | float 0-1 | Track fader |
| `/magda/track/{n}/pan` | float 0-1 | Pan, `0.5` centre |
| `/magda/track/{n}/mute` | `0`/`1`, or none | Mute on or off; no argument flips it |
| `/magda/track/{n}/solo` | `0`/`1`, or none | Solo on or off; no argument flips it |
| `/magda/track/{n}/send/{m}` | float 0-1 | Send level |

## Master and focused device

| Address | Argument | Meaning |
|---|---|---|
| `/magda/master/volume` | float 0-1 | Master fader |
| `/magda/master/pan` | float 0-1 | Master pan |
| `/magda/focused/macro/{k}` | float 0-1 | Macro of the [focused device](../modulation/macros.md) |

## How the numbers work

`n`, `m` and `k` are all 1-based, the way surface templates and mixer strips are numbered.

**`n` is a mixer position, not a track ID.** Track IDs survive a reload but go sparse: delete tracks 2 and 3 and the remaining ones are 1, 4 and 5. A template addressing eight strips needs eight dense numbers, so `n` counts positions in the mixer's visible track order instead. A position with no track behind it is ignored, which means a sixteen-strip template on an eight-track project drives the eight that exist rather than failing.

The ceilings are 128 tracks, 8 sends per track, and 16 macros. A number past its ceiling is ignored rather than clamped, so a misconfigured template cannot land a fader on the wrong track.

## Strict parsing

Addresses are matched exactly. These are all rejected rather than interpreted:

- An OSC address **pattern** where a number belongs, such as `/magda/track/*/volume`. Patterns are a real part of the protocol, and reading one as an index would drive every track at once when the template author meant one.
- A **leading zero**, such as `/magda/track/03/volume`, which would otherwise be a second spelling of strip 3 that could drift apart from the first.
- An **out-of-range** index, or a **trailing component** after a complete address.

Nothing here is an error: an address this list does not cover is simply passed to the binding layer instead.

## Buttons and toggles

Surfaces send both a press and a release down the same address, so the two are told apart by argument:

- **Triggers** (`play`, `stop`) act on a press. No argument or a non-zero one fires; a zero argument is the release half and does nothing. Without that rule, letting go of Play would stop the transport.
- **Toggles** (`record`, `loop`, `mute`, `solo`) take `0` or `1` as a state. Sent with no argument at all they flip whatever the state currently is, so a momentary button works as well as a latching one.

`seek` and `seek/bars` are distances rather than positions, and they accumulate: two rewind presses move two bars.

## Feedback

MAGDA echoes its own state back so a surface shows the right values instead of whatever its template defaulted to. Set the feedback port in [Connections](../interface/connections.md#osc). There is no destination address to configure: MAGDA answers whoever is talking to it.

What gets sent:

- **On change** - track and master volume, pan, mute, solo and sends, and the focused device's macros.
- **Sampled at 30 Hz** - play, record, loop, tempo and the playhead.

A value only goes out when it differs from what was last sent, and an address is sent at most once per flush however often it changed in between, so a moving fader does not flood the surface.

A value the surface just sent is not echoed straight back, which would fight a motorised control. The one send you do get after a gesture ends is the confirmation of what MAGDA rounded your value to.

A full snapshot is sent when the feedback destination changes, and when inbound traffic resumes after five seconds of silence. Large snapshots are spread over consecutive flushes rather than dropped, so a sixty-track project takes a few ticks to populate rather than arriving incomplete.

## Beyond the fixed namespace

The addresses above cover a mixer surface. Binding an arbitrary OSC address to any parameter MAGDA can name is a separate mechanism layered on top, and it has no interface yet, so it is not something you can set up from the app in this release.
