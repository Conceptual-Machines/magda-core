"""The router's label space — MAGDA's ConsoleIntent set, verbatim.

Deliberately NOT the command model's label space. The command model classifies
38 *DSL command* intents (create_track, add_plugin, notes_quantize, ...); the
router answers a coarser and completely different question — *which agent*. The
two label spaces must stay distinct: a music request routed into the command
model would be silently mangled into DSL.

Order is fixed and mirrored by magda::ConsoleIntent / toIntentString(); the
exported id -> name table is what the C++ backend returns, so these strings are
exactly what RouterAgent::classify() hands to intentFromString().
"""
from __future__ import annotations

LABELS = [
    "COMMAND",     # structural project edits — the command model's own surface
    "MUSIC",       # musical content generation (chords, melody, harmony)
    "BOTH",        # generate content AND place it (music agent -> command agent)
    "AUTOMATION",  # automation curves / clips
    "DRUM",        # drum patterns and groove edits
    "MIXING",      # mix analysis and mixing advice
    "SESSION",     # clip launch / scenes / performance
]

LABEL_ID = {name: i for i, name in enumerate(LABELS)}
