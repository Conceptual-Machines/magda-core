# Loop length intent (#2675)

An audio loop can name a region of its source or a musical count. Correcting
the source tempo must preserve whichever quantity the user chose.

| Loop edit | Tempo correction preserves |
| --- | --- |
| Whole file or a source-time length | Source samples; the displayed beat count changes |
| A musical length | Beat count; the source sample length changes |

For example, a four-bar loop inside a longer recording remains sixteen beats
when its detected tempo is corrected from 100 to 120 BPM. A whole two-second
file corrected from 120 to 240 BPM remains two seconds and becomes eight beats.
The clip's timeline placement is independent of both operations.

`AudioEvent` records the loop-length intent separately from its extent. Musical
lengths retain an authoritative beat count and resolve a sample length using
the event's interpreted tempo. The source anchor and loop start remain source
positions. Changing a loop's position alone preserves its length intent.

Project JSON stores the intent and musical count. Projects without these fields
retain source-region behavior. Undo restores the intent together with the
length, so undoing a musical edit does not convert an imported whole-file loop.

DAWproject 1.0 has no separate loop-intent field. Beat-based audio loops import
as musical lengths and time-based loops as source regions. Exporting a
source-region loop in beat mode and importing it again therefore preserves its
current region but makes later tempo corrections follow its exported beat
count. Native project save/load preserves the distinction.

Native compilation resolves the musical length at the source's sample rate;
Tracktion synchronization uses its beat length. Existing backend handling of
regions beyond the source end remains a separate constraint.
