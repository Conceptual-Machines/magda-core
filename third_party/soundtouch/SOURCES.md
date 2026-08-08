# SoundTouch

SoundTouch 2.1pre (`SOUNDTOUCH_VERSION_ID` 20009) from https://www.surina.net/soundtouch,
taken from the copy the Tracktion fork carries under
`modules/tracktion_engine/3rd_party/soundtouch`.

Unmodified upstream sources. The only thing done to the tree was deleting five one-line
headers under `source/SoundTouch/` that forwarded to their real definitions in
`include/`; upstream reaches those through the include path, and so does the target here.

`SOUNDTOUCH_FLOAT_SAMPLES` is set in `include/soundtouch_config.h`, as it comes.

## Licence

LGPL-2.1-or-later. `COPYING.TXT` is the full licence text.

MAGDA is distributed under GPL-3.0, and LGPL-2.1 is compatible with it, so linking this
into a MAGDA binary carries no obligation beyond the ones GPL-3.0 already imposes.

It is built as its own static library from unmodified sources rather than compiled into
`magda_engine` so that the relink option LGPL-2.1 section 6 talks about stays open: the
archive can be rebuilt from a different version of SoundTouch and put back without
touching anything else. That matters if MAGDA is ever distributed under other terms; it
costs nothing now.

## Why it is here at all

The persisted time-stretch mode values are pinned project-file integers
(`magda/daw/core/TimeStretchModes.hpp`): `kSoundTouchNormal` is 3 and `kSoundTouchBetter`
is 4. Projects saved with either play through SoundTouch, so the native engine (#1882)
needs it to play those projects the way they were made.
