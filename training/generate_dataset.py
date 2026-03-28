#!/usr/bin/env python3
"""Generate MAGDA fine-tuning dataset with DSL command examples.

Router examples: kept from existing dataset
Music examples: kept from existing dataset (compact format)
Command examples: regenerated with DSL syntax
"""

import json

# Shortened system prompts for training (full prompt is too long for fine-tuning)
ROUTER_SYSTEM = "Classify the user's request as COMMAND, MUSIC, or BOTH. Respond with ONLY one word."

COMMAND_SYSTEM = (
    "You are MAGDA, a DAW AI assistant. Respond ONLY with DSL code. No prose.\n"
    "Syntax: track(name=\"X\", new=true), track(id=N), filter(tracks, track.name == \"X\")\n"
    "Chains: .clip.new(bar=1, length_bars=4), .track.set(volume_db=-6, pan=0.5, mute=true, solo=true),\n"
    ".fx.add(name=\"reverb\"), .delete(), .select(), .clip.rename(index=0, name=\"X\"),\n"
    ".clip.delete(index=0), .for_each(...), .clips.select(clip.length_bars > 2)\n"
    "Notes: .notes.add(pitch=C4, beat=0, length=1, velocity=100),\n"
    ".notes.add_chord(root=C4, quality=major, beat=0, length=1),\n"
    ".notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, pattern=up, fill=true)\n"
    "Functions: random(min, max)\n"
    "When user says 'create a track', use new=true. One statement per line."
)

COMMAND_SYSTEM_WITH_STATE = (
    lambda state: COMMAND_SYSTEM + "\n\nCurrent DAW state:\n" + state
)

MUSIC_SYSTEM = "Generate musical content using compact notation. Respond ONLY with instructions. No prose. One instruction per line."


def msg(system, user, assistant):
    return {
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
            {"role": "assistant", "content": assistant},
        ]
    }


# ============================================================================
# Router examples (kept from existing dataset — just regenerate clean)
# ============================================================================
router_examples = [
    # COMMAND
    ("create a bass track", "COMMAND"),
    ("delete track 2", "COMMAND"),
    ("add reverb to the vocals track", "COMMAND"),
    ("mute the drums", "COMMAND"),
    ("set volume to -6 dB", "COMMAND"),
    ("add a 4 bar clip on track 1", "COMMAND"),
    ("rename track 3 to Strings", "COMMAND"),
    ("solo the piano", "COMMAND"),
    ("add EQ and compressor to track 1", "COMMAND"),
    ("create 5 tracks named after planets", "COMMAND"),
    ("delete all clips shorter than 1 bar", "COMMAND"),
    ("pan track 2 hard left", "COMMAND"),
    ("mute all tracks with drum in the name", "COMMAND"),
    ("select all clips on the Bass track", "COMMAND"),
    ("add a clip at bar 5, 2 bars long", "COMMAND"),
    ("unmute everything", "COMMAND"),
    ("create an empty track called FX Bus", "COMMAND"),
    ("set all tracks to -12 dB", "COMMAND"),
    ("add delay to the guitar track", "COMMAND"),
    ("create 8 clips of 2 bars each", "COMMAND"),
    ("delete track 5", "COMMAND"),
    ("select all muted tracks", "COMMAND"),
    ("rename the first clip to Intro", "COMMAND"),
    ("add a limiter to the master", "COMMAND"),
    ("set pan to center on all tracks", "COMMAND"),
    ("delete all clips longer than 8 bars", "COMMAND"),
    ("solo tracks 1 and 3", "COMMAND"),
    ("add 3 clips on track 2 starting at bar 1", "COMMAND"),
    ("create a track called Percussion", "COMMAND"),
    ("select track 1", "COMMAND"),
    ("add chorus to the synth track", "COMMAND"),
    ("remove track 4", "COMMAND"),
    ("set volume of the bass to -3", "COMMAND"),
    ("create a track with Surge XT", "COMMAND"),
    ("add Pro-Q 3 to the vocals", "COMMAND"),
    ("rename all selected clips to Verse", "COMMAND"),
    ("set up a basic band template", "COMMAND"),
    ("add a plugin to track 1", "COMMAND"),
    ("move the clip to bar 9", "COMMAND"),
    ("duplicate track 2", "COMMAND"),
    ("select clips longer than 4 bars", "COMMAND"),
    ("mute tracks 1 through 4", "COMMAND"),
    # MUSIC
    ("play a C major chord", "MUSIC"),
    ("write a chord progression in G", "MUSIC"),
    ("arpeggiate D minor", "MUSIC"),
    ("12 bar blues in A", "MUSIC"),
    ("ii-V-I in Bb major", "MUSIC"),
    ("add some jazzy chords", "MUSIC"),
    ("bass line in E minor", "MUSIC"),
    ("pentatonic melody", "MUSIC"),
    ("chord progression for a ballad", "MUSIC"),
    ("walking bass in F", "MUSIC"),
    ("neo-soul chords in Eb", "MUSIC"),
    ("write a funk bass line", "MUSIC"),
    ("Coltrane changes", "MUSIC"),
    ("gospel chord progression", "MUSIC"),
    ("bossa nova progression in C", "MUSIC"),
    ("lofi hip hop chords", "MUSIC"),
    ("dark ambient pad chords", "MUSIC"),
    ("reggae rhythm in G", "MUSIC"),
    ("minor key progression with 7ths", "MUSIC"),
    ("suspended chord sequence", "MUSIC"),
    ("four chord pop progression", "MUSIC"),
    ("sad ballad in D minor", "MUSIC"),
    ("blues turnaround in E", "MUSIC"),
    ("add a melody in A minor", "MUSIC"),
    ("ascending arpeggio in C major", "MUSIC"),
    ("descending chromatic bass line", "MUSIC"),
    ("write some jazz voicings", "MUSIC"),
    ("synth arpeggio pattern", "MUSIC"),
    ("simple C-F-G-C", "MUSIC"),
    ("modal interchange chords", "MUSIC"),
    ("tritone substitution progression", "MUSIC"),
    # BOTH
    ("create a piano track and add a C major progression", "BOTH"),
    ("make a bass track with a walking bass line in G", "BOTH"),
    ("create a track called Strings and add a chord progression", "BOTH"),
    ("add a 4 bar clip and fill it with an arpeggio in A minor", "BOTH"),
    ("create a synth track and write a melody", "BOTH"),
    ("make a pad track with some ambient chords", "BOTH"),
    ("set up a piano track and add a ii-V-I in C", "BOTH"),
    ("create a bass track and add a funk bass line", "BOTH"),
    ("make a new track with a 12 bar blues", "BOTH"),
    ("add an 8 bar clip and write a chord progression in D", "BOTH"),
    ("create a Rhodes track and add jazz chords", "BOTH"),
    ("set up a guitar track with a pentatonic melody", "BOTH"),
    ("create a track for horns and add a brass melody", "BOTH"),
    ("make a lead synth track and add an arpeggio", "BOTH"),
    ("create an organ track with a gospel progression", "BOTH"),
    ("add a clip on the Keys track and write some chords", "BOTH"),
    ("create a marimba track with a looping pattern", "BOTH"),
    ("make a harp track and add an ascending arpeggio", "BOTH"),
    ("set up a vibraphone track with jazz voicings", "BOTH"),
    ("create a celesta track and add a simple melody", "BOTH"),
    ("create a track, add a clip and write some notes", "BOTH"),
    ("make a new track with reverb and add a chord progression", "BOTH"),
    ("create a pad track and fill it with dark ambient chords", "BOTH"),
    ("add a bass track and write a reggae bass line", "BOTH"),
    ("create a track called Lead and write a melody in E minor", "BOTH"),
    ("set up a strings section and add a slow chord progression", "BOTH"),
    ("make a synth track with delay and add an arpeggio", "BOTH"),
    ("create a piano track with EQ and write a ballad progression", "BOTH"),
    ("add a new track and compose a bossa nova pattern", "BOTH"),
    ("create a wurlitzer track and add some neo-soul chords", "BOTH"),
    ("make a track called Choir and add a hymn-like progression", "BOTH"),
    ("create a dulcimer track with a folk melody", "BOTH"),
    ("set up a bell track and add a pentatonic pattern", "BOTH"),
    ("make an accordion track with a tango progression", "BOTH"),
    ("create a sitar track and add a raga-inspired melody", "BOTH"),
    ("add an 8 bar clip with random velocity notes", "BOTH"),
]

# ============================================================================
# Command examples (DSL syntax)
# ============================================================================
command_examples = [
    # --- Basic track creation ---
    ("create a bass track", 'track(name="Bass", new=true)'),
    ("create a track called Lead", 'track(name="Lead", new=true)'),
    ("make a drums track", 'track(name="Drums", new=true)'),
    ("create a track called Synth Pad", 'track(name="Synth Pad", new=true)'),
    ("create a track", "track(new=true)"),
    ("add a new track called FX Bus", 'track(name="FX Bus", new=true)'),
    ("create a Vocals track", 'track(name="Vocals", new=true)'),
    ("make a guitar track", 'track(name="Guitar", new=true)'),
    ("create a track called Percussion", 'track(name="Percussion", new=true)'),
    ("create a track called Sub Bass", 'track(name="Sub Bass", new=true)'),

    # --- Multiple tracks ---
    ("set up a basic band: drums, bass, guitar, keys, vocals",
     'track(name="Drums", new=true)\ntrack(name="Bass", new=true)\ntrack(name="Guitar", new=true)\ntrack(name="Keys", new=true)\ntrack(name="Vocals", new=true)'),
    ("create a track for each member of a string quartet",
     'track(name="Violin 1", new=true)\ntrack(name="Violin 2", new=true)\ntrack(name="Viola", new=true)\ntrack(name="Cello", new=true)'),
    ("create 3 synth tracks",
     'track(name="Synth 1", new=true)\ntrack(name="Synth 2", new=true)\ntrack(name="Synth 3", new=true)'),
    ("set up a brass section",
     'track(name="Trumpet", new=true)\ntrack(name="Trombone", new=true)\ntrack(name="French Horn", new=true)\ntrack(name="Tuba", new=true)'),

    # --- Track with FX ---
    ("create a track called Lead and add reverb and delay",
     'track(name="Lead", new=true).fx.add(name="reverb")\ntrack(name="Lead").fx.add(name="delay")'),
    ("make a track called Ambient Pad with delay, reverb and chorus",
     'track(name="Ambient Pad", new=true).fx.add(name="delay")\ntrack(name="Ambient Pad").fx.add(name="reverb")\ntrack(name="Ambient Pad").fx.add(name="chorus")'),
    ("create a track called Pad with EQ and compressor",
     'track(name="Pad", new=true).fx.add(name="eq")\ntrack(name="Pad").fx.add(name="compressor")'),
    ("add reverb to the vocals track", 'track(name="Vocals").fx.add(name="reverb")'),
    ("add EQ and compressor to track 1",
     'track(id=1).fx.add(name="eq")\ntrack(id=1).fx.add(name="compressor")'),
    ("add delay to the guitar track", 'track(name="Guitar").fx.add(name="delay")'),
    ("add chorus to the synth track", 'track(name="Synth").fx.add(name="chorus")'),
    ("add a phaser to track 3", 'track(id=3).fx.add(name="phaser")'),
    ("add a filter to the bass", 'track(name="Bass").fx.add(name="filter")'),

    # --- Third-party plugins ---
    ("create a track with Surge XT",
     'track(name="Surge XT", new=true).fx.add(name="Surge XT")'),
    ("add Pro-Q 3 to track 1", 'track(id=1).fx.add(name="Pro-Q 3")'),
    ("add Pro-Q 3 VST3 to the vocals",
     'track(name="Vocals").fx.add(name="Pro-Q 3", format="VST3")'),
    ("add Valhalla Room to the strings track",
     'track(name="Strings").fx.add(name="Valhalla Room")'),
    ("create a track with Diva",
     'track(name="Diva", new=true).fx.add(name="Diva")'),

    # --- Track properties ---
    ("set volume of track 2 to -6 dB", "track(id=2).track.set(volume_db=-6)"),
    ("set volume to -3 on the bass track", 'track(name="Bass").track.set(volume_db=-3)'),
    ("pan track 2 hard left", "track(id=2).track.set(pan=-1.0)"),
    ("pan the guitar slightly right", 'track(name="Guitar").track.set(pan=0.3)'),
    ("set pan to center on track 1", "track(id=1).track.set(pan=0)"),
    ("mute the drums", 'track(name="Drums").track.set(mute=true)'),
    ("solo the piano", 'track(name="Piano").track.set(solo=true)'),
    ("unmute track 3", "track(id=3).track.set(mute=false)"),
    ("unsolo the bass", 'track(name="Bass").track.set(solo=false)'),
    ("rename track 3 to Strings", 'track(id=3).track.set(name="Strings")'),
    ("set volume to -9 and pan right on track 2", "track(id=2).track.set(volume_db=-9, pan=0.8)"),
    ("create a strings track, pan it slightly right, volume at -9",
     'track(name="Strings", new=true).track.set(pan=0.3, volume_db=-9)'),
    ("create a Sub Bass track, set volume to -3 and pan center",
     'track(name="Sub Bass", new=true).track.set(volume_db=-3, pan=0)'),

    # --- Track + FX + properties ---
    ("create a bass track, add a 4 bar clip, and set volume to -3",
     'track(name="Bass", new=true).clip.new(bar=1, length_bars=4).track.set(volume_db=-3)'),
    ("create a track called Pad with EQ and compressor, set volume to -6",
     'track(name="Pad", new=true).fx.add(name="eq")\ntrack(name="Pad").fx.add(name="compressor")\ntrack(name="Pad").track.set(volume_db=-6)'),

    # --- Clips ---
    ("add a 4 bar clip on track 1", "track(id=1).clip.new(bar=1, length_bars=4)"),
    ("add a clip at bar 5, 2 bars long", "track(id=1).clip.new(bar=5, length_bars=2)"),
    ("make a guitar track with an 8 bar clip at bar 1",
     'track(name="Guitar", new=true).clip.new(bar=1, length_bars=8)'),
    ("add a 4 bar clip", "track(id=1).clip.new(length_bars=4)"),
    ("add 4 clips of 4 bars each with section names",
     'track(id=1).clip.new(bar=1, length_bars=4)\ntrack(id=1).clip.rename(index=0, name="Intro")\n'
     'track(id=1).clip.new(bar=5, length_bars=4)\ntrack(id=1).clip.rename(index=1, name="Verse")\n'
     'track(id=1).clip.new(bar=9, length_bars=4)\ntrack(id=1).clip.rename(index=2, name="Chorus")\n'
     'track(id=1).clip.new(bar=13, length_bars=4)\ntrack(id=1).clip.rename(index=3, name="Bridge")'),
    ("add 2 bar clips at bars 1, 3, 5, 7 on track 1",
     "track(id=1).clip.new(bar=1, length_bars=2)\ntrack(id=1).clip.new(bar=3, length_bars=2)\n"
     "track(id=1).clip.new(bar=5, length_bars=2)\ntrack(id=1).clip.new(bar=7, length_bars=2)"),
    ("create 8 clips of 2 bars each on track 1",
     "track(id=1).clip.new(bar=1, length_bars=2)\ntrack(id=1).clip.new(bar=3, length_bars=2)\n"
     "track(id=1).clip.new(bar=5, length_bars=2)\ntrack(id=1).clip.new(bar=7, length_bars=2)\n"
     "track(id=1).clip.new(bar=9, length_bars=2)\ntrack(id=1).clip.new(bar=11, length_bars=2)\n"
     "track(id=1).clip.new(bar=13, length_bars=2)\ntrack(id=1).clip.new(bar=15, length_bars=2)"),
    ("add a clip with random length between 1 and 8 bars",
     "track(id=1).clip.new(length_bars=random(1, 8))"),

    # --- Clip rename ---
    ("rename the first clip on track 1 to Intro",
     'track(id=1).clip.rename(index=0, name="Intro")'),
    ("rename selected clips to Verse", 'track(id=1).clip.rename(name="Verse")'),
    ("rename selected clips with numbering",
     'track(id=1).clip.rename(name="Clip {i}")'),

    # --- Clip delete ---
    ("delete the first clip on track 2", "track(id=2).clip.delete(index=0)"),
    ("delete the second clip on the bass track", 'track(name="Bass").clip.delete(index=1)'),

    # --- Clip select ---
    ("select all clips longer than 2 bars on track 1",
     "track(id=1).clips.select(clip.length_bars > 2)"),
    ("select all clips shorter than or equal to 1 bar on track 2",
     "track(id=2).clips.select(clip.length_bars <= 1)"),
    ("select clips starting after bar 4 on track 1",
     "track(id=1).clips.select(clip.start_bar > 4)"),

    # --- Delete track ---
    ("delete track 2", "track(id=2).delete()"),
    ("remove track 4", "track(id=4).delete()"),
    ("delete track 5", "track(id=5).delete()"),

    # --- Select track ---
    ("select track 1", "track(id=1).select()"),
    ("select the bass track", 'track(name="Bass").select()'),

    # --- Solo/mute multiple ---
    ("solo tracks 1 and 3",
     "track(id=1).track.set(solo=true)\ntrack(id=3).track.set(solo=true)"),
    ("mute tracks 1 through 4",
     "track(id=1).track.set(mute=true)\ntrack(id=2).track.set(mute=true)\n"
     "track(id=3).track.set(mute=true)\ntrack(id=4).track.set(mute=true)"),

    # --- Filter operations ---
    ("mute all tracks with drum in the name",
     'filter(tracks, track.name == "Drum").track.set(mute=true)'),
    ("solo all tracks with bass in the name",
     'filter(tracks, track.name == "Bass").track.set(solo=true)'),
    ("delete all tracks named Unused",
     'filter(tracks, track.name == "Unused").delete()'),
    ("unmute everything",
     'filter(tracks, track.mute == true).track.set(mute=false)'),
    ("set all tracks to -12 dB",
     'filter(tracks, track.name == "").track.set(volume_db=-12)'),
    ("add reverb to all tracks named Vocals",
     'filter(tracks, track.name == "Vocals").for_each(.fx.add(name="reverb"))'),
    ("add a 4 bar clip to all tracks named Synth",
     'filter(tracks, track.name == "Synth").for_each(.clip.new(bar=1, length_bars=4))'),

    # --- With DAW state context ---
]

# Command examples with DAW state
state_examples = [
    # Simple state — track selected
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio"},{"id":2,"name":"Drums","type":"Audio"},{"id":3,"name":"Keys","type":"Audio"}],"track_count":3,"selected_track_id":1}',
        "add a 4 bar clip",
        "track(id=1).clip.new(length_bars=4)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio"},{"id":2,"name":"Drums","type":"Audio"}],"track_count":2,"selected_track_id":2}',
        "add reverb",
        'track(id=2).fx.add(name="reverb")',
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio"}],"track_count":1,"selected_track_id":1}',
        "add an 8 bar clip and set volume to -6",
        "track(id=1).clip.new(length_bars=8).track.set(volume_db=-6)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"},{"id":3,"name":"Keys","type":"Audio"},{"id":4,"name":"Strings","type":"Audio"}],"track_count":4,"selected_track_id":4}',
        "add reverb and set volume to -9",
        'track(id=4).fx.add(name="reverb")\ntrack(id=4).track.set(volume_db=-9)',
    ),
    (
        '{"tracks":[{"id":1,"name":"Lead","type":"Audio"},{"id":2,"name":"Pad","type":"Audio"}],"track_count":2,"selected_track_id":1}',
        "mute this track",
        "track(id=1).track.set(mute=true)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"}],"track_count":2,"selected_track_id":2}',
        "solo it and pan left",
        "track(id=2).track.set(solo=true, pan=-1.0)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"},{"id":3,"name":"Guitar","type":"Audio"}],"track_count":3,"selected_track_id":3}',
        "delete this track",
        "track(id=3).delete()",
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio"},{"id":2,"name":"Strings","type":"Audio"}],"track_count":2,"selected_track_id":1}',
        "rename this to Electric Piano",
        'track(id=1).track.set(name="Electric Piano")',
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"},{"id":3,"name":"Guitar","type":"Audio"}],"track_count":3}',
        "add a new synth track with delay",
        'track(name="Synth", new=true).fx.add(name="delay")',
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]},{"id":2,"name":"Bass","type":"Audio"}],"track_count":2,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "rename this clip to Intro",
        'track(id=1).clip.rename(index=0, name="Intro")',
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"},{"id":3,"name":"Keys","type":"Audio"}],"track_count":3,"selected_track_id":2}',
        "add EQ and compressor",
        'track(id=2).fx.add(name="eq")\ntrack(id=2).fx.add(name="compressor")',
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"},{"id":3,"name":"Lead","type":"Audio"}],"track_count":3,"selected_track_id":3}',
        "add a 2 bar clip at bar 5",
        "track(id=3).clip.new(bar=5, length_bars=2)",
    ),
    # No track selected — user creates new
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"}],"track_count":1}',
        "create a bass track",
        'track(name="Bass", new=true)',
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio"},{"id":2,"name":"Strings","type":"Audio"}],"track_count":2}',
        "mute all tracks",
        'track(id=1).track.set(mute=true)\ntrack(id=2).track.set(mute=true)',
    ),
    (
        '{"tracks":[{"id":1,"name":"Drums","type":"Audio"},{"id":2,"name":"Bass","type":"Audio"},{"id":3,"name":"Guitar","type":"Audio"},{"id":4,"name":"Keys","type":"Audio"}],"track_count":4,"selected_track_id":2}',
        "set volume to -6 and add a 4 bar clip",
        "track(id=2).track.set(volume_db=-6)\ntrack(id=2).clip.new(length_bars=4)",
    ),
]

# Note operation examples (with clip selected state)
note_state_examples = [
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "add a C4 note at beat 0",
        "track(id=1).notes.add(pitch=C4, beat=0, length=1, velocity=100)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "add a C major chord at beat 0",
        "track(id=1).notes.add_chord(root=C4, quality=major, beat=0, length=1)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "add an A minor arpeggio",
        "track(id=1).notes.add_arpeggio(root=A3, quality=min, beat=0, step=0.5, fill=true)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "select all C4 notes",
        "track(id=1).notes.select(note.pitch == C4)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "transpose selected notes up 5 semitones",
        "track(id=1).notes.transpose(semitones=5)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "delete selected notes",
        "track(id=1).notes.delete()",
    ),
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "set velocity to 80",
        "track(id=1).notes.set_velocity(value=80)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Bass","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "quantize to 16th notes",
        "track(id=1).notes.quantize(grid=0.25)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Lead","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":2}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "add a C major chord first inversion at beat 2",
        "track(id=1).notes.add_chord(root=C4, quality=major, beat=2, length=1, inversion=1)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Keys","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "add notes with random velocity",
        "track(id=1).notes.add(pitch=C4, beat=0, length=1, velocity=random(60, 127))",
    ),
    (
        '{"tracks":[{"id":1,"name":"Synth","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":8}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "add an up-down D minor 7 arpeggio that fills the clip",
        "track(id=1).notes.add_arpeggio(root=D4, quality=min7, beat=0, step=0.5, pattern=updown, fill=true)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "set the note to F1",
        "track(id=1).notes.set_pitch(pitch=F1)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "resize notes to half a beat",
        "track(id=1).notes.resize(length=0.5)",
    ),
    (
        '{"tracks":[{"id":1,"name":"Piano","type":"Audio","clips":[{"index":0,"start_bar":1,"length_bars":4}]}],"track_count":1,"selected_track_id":1,"selected_clip_index":0,"selected_clip_track_id":1}',
        "select notes with velocity above 100",
        "track(id=1).notes.select(note.velocity > 100)",
    ),
]

# Groove examples
groove_examples = [
    ("add swing to this clip", 'groove.set(template="Basic 8th Swing", strength=0.5)'),
    ("what grooves are available", "groove.list()"),
    ("create a funky 16th groove",
     'groove.new(name="Funky 16ths", notesPerBeat=4, shifts="0.0,0.2,-0.05,0.35,0.0,0.15,-0.1,0.3")'),
    ("extract the groove from this audio clip",
     'groove.extract(clip=0, resolution=16, name="Extracted Feel")'),
    ("apply heavy swing", 'groove.set(template="Basic 8th Swing", strength=0.8)'),
    ("add subtle shuffle", 'groove.set(template="Basic 16th Swing", strength=0.3)'),
]

# Combined clip + notes (BOTH-style but command only — track+clip+notes in DSL)
combined_examples = [
    ("create a track called Lead and add a 4 bar clip at bar 1",
     'track(name="Lead", new=true).clip.new(bar=1, length_bars=4)'),
    ("create a track called Horns, add reverb, and a 4 bar clip at bar 1",
     'track(name="Horns", new=true).fx.add(name="reverb")\ntrack(name="Horns").clip.new(bar=1, length_bars=4)'),
    ("add a clip with a C major arpeggio over 2 bars on track 1",
     "track(id=1).clip.new(length_bars=2).notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, fill=true)"),
    ("create a piano track with a 4 bar clip and add a D minor chord",
     'track(name="Piano", new=true).clip.new(bar=1, length_bars=4).notes.add_chord(root=D4, quality=min, beat=0, length=4)'),
    ("add 2 clips with arpeggios on track 1",
     "track(id=1).clip.new(length_bars=4).notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5, fill=true)\n"
     "track(id=1).clip.new(length_bars=4).notes.add_arpeggio(root=A3, quality=min, beat=0, step=0.5, fill=true)"),
]


def build_dataset():
    dataset = []

    # Router examples
    for user_msg, intent in router_examples:
        dataset.append(msg(ROUTER_SYSTEM, user_msg, intent))

    # Command examples (no state)
    for user_msg, dsl in command_examples:
        dataset.append(msg(COMMAND_SYSTEM, user_msg, dsl))

    # Command examples with DAW state
    for state, user_msg, dsl in state_examples:
        dataset.append(msg(COMMAND_SYSTEM_WITH_STATE(state), user_msg, dsl))

    # Note operation examples with state
    for state, user_msg, dsl in note_state_examples:
        dataset.append(msg(COMMAND_SYSTEM_WITH_STATE(state), user_msg, dsl))

    # Groove examples
    for user_msg, dsl in groove_examples:
        dataset.append(msg(COMMAND_SYSTEM, user_msg, dsl))

    # Combined examples
    for user_msg, dsl in combined_examples:
        dataset.append(msg(COMMAND_SYSTEM, user_msg, dsl))

    # Music examples — import from existing dataset
    with open("training/dataset.jsonl") as f:
        for line in f:
            d = json.loads(line)
            sys_content = d["messages"][0]["content"]
            if "musical content" in sys_content.lower() or "Generate musical" in sys_content:
                dataset.append(d)

    return dataset


if __name__ == "__main__":
    dataset = build_dataset()

    # Count by type
    router = sum(1 for d in dataset if "Classify" in d["messages"][0]["content"])
    music = sum(
        1
        for d in dataset
        if "musical content" in d["messages"][0]["content"].lower()
        or "Generate musical" in d["messages"][0]["content"]
    )
    command = len(dataset) - router - music

    print(f"Total: {len(dataset)} examples")
    print(f"  Router: {router}")
    print(f"  Command (DSL): {command}")
    print(f"  Music (compact): {music}")

    with open("training/dataset.jsonl", "w") as f:
        for entry in dataset:
            f.write(json.dumps(entry) + "\n")

    print(f"\nWritten to training/dataset.jsonl")
