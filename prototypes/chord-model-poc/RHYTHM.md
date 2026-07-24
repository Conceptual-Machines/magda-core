# Rhythm model — sibling POC (design + data + licensing notes)

Parked design note, not yet built. Captures the plan for a third "tiny local
model of idiom" — this one for **time** (rhythmic figures / grooves) — the way
`findings.md` covers the chord model for **harmony**. Same philosophy: small,
local, trained on real idiom, legally clean for a commercial product.

## Where it fits in MAGDA

- **Drum grid** — generate patterns; also **groove/humanize templates** (real
  microtiming + velocity, not a quantized grid).
- **Arpeggiator** — a rhythmic engine for the arp beyond the fixed patterns.
- **DrummerAgent** (`magda/agents/`) — a learned local backend for drum ideas,
  alongside the cloud path.

## Licensing — even cleaner than chords

Rhythms and drum patterns are generally **not copyrightable**: courts treat a
groove as a functional, shared building block (it keeps time), even more
clearly than a chord progression. Standard cells — backbeat, four-on-the-floor,
clave, tresillo, the 808/909 pattern canon — are common musical vocabulary.

The only protected layer is the **sound recording** of a performance. Because a
rhythm model works on **symbolic MIDI**, not audio, that copyright never
attaches. So symbolic rhythmic figures are about as free as data gets.

Caveat (same as chords): an *exceptionally* distinctive pattern is a gray area,
and anything shipping in a paid product should get legal review. But standard
grooves and cells are fair game.

## Data (commercially clean)

**Drum grooves — gold standard, real feel:**

| source | license | notes |
|---|---|---|
| Groove MIDI Dataset (GMD) | CC BY 4.0 | 13.6 h, 1,150 MIDI files, 22k measures, real pro drummers; genre/tempo/drummer labels; **microtiming + velocity** |
| Expanded Groove MIDI (E-GMD) | CC BY 4.0 | ~444 h; same terms; scale-up |

CC BY = usable commercially, just attribute. Symbolic MIDI ⇒ no
recording-copyright issue.

**General rhythmic figures (any instrument):** derive the rhythm layer from the
same public-domain corpora the chord model already uses — Nottingham (ABC),
OpenEWLD, plus the Essen Folksong Collection. Classic named cells are
public-domain and usable directly.

Split of responsibility:
- **Drum grid / DrummerAgent → GMD / E-GMD** (drum-kit grooves, real feel).
- **Arp / melodic rhythm → derive from Nottingham / OpenEWLD / Essen** (PD).

GMD is drum-*kit* specific; for abstract cross-instrument rhythm, the
derive-from-PD-scores route is the base and GMD is the drums specialist.

## Representation sketch (to be firmed up)

Mirror the chord model's "constrained, relative, deterministic-reconstruct"
approach, but for time:

- **Meter-relative grid.** Positions within a bar at a fixed resolution (e.g.
  16th or 48-tick), analogous to key-relative for chords — makes patterns
  tempo/meter-invariant and collapses the vocab.
- **Token = (grid-position, voice, velocity-bucket)** for drums; or
  (grid-position, duration) for melodic rhythm. Microtiming (offset from grid)
  and velocity modelled as small side-channels so "feel" survives — that's the
  thing GMD uniquely provides and a quantized dataset can't.
- **Deterministic reconstruction**: model emits symbolic grid events → a
  deterministic step lays them onto the MAGDA drum-grid / MIDI clip at the
  current tempo. Model never emits raw audio or absolute-time MIDI.

Model shape: same tiny-GRU (or causal-conv) family as the chord model; short
context (a bar or two), tens of k params, CPU-local.

## Relationship to the chord model

Two small local models of the two things a musical assistant most needs to
*know*: **harmony** (chords) and **time** (rhythm). Both trained on real idiom,
both legally clean (harmony via CC0/PD chord sets; rhythm via CC-BY GMD + PD
scores), both deterministic-reconstruct so they can't emit something invalid.
Melody/lyrics/recordings — the genuinely copyrighted layers — are exactly what
neither model needs to touch.

## Status

Idea captured; **not started**. Sequencing: land the chord model
(continuation → reharm → generation) first; rhythm is the natural next sibling,
reusing the same training/eval scaffolding shape.
