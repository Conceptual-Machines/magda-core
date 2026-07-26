# Spike: MAGDA chord model (harmonic idiom, local)

Status: Phase 0 scaffold. This doc records decisions so the next session
doesn't re-litigate them. Sibling spike to `prototypes/command-model-poc/`.

## Goal

A tiny, local, learned model of chord-progression **idiom** that plugs into
MAGDA's existing chord engine. Not "compose music" — model harmony, well,
small, offline, and legally clean for a shipping product.

## Locked decisions (with rationale)

1. **Real data, not synthetic.** Rule-generated progressions only replay the
   rules you encode — they can't learn idiom (which substitutions actually get
   used, genre feel, real cadential habits). So we train on real progressions.

2. **Commercially-clean sources only.** MAGDA is a commercial GPLv3 open-core
   product, so research-only / non-commercial corpora are out. Usable set:
   McGill Billboard (CC0), Nottingham (public-domain trad. folk), OpenEWLD
   (public-domain subset of Wikifonia). Avoided: Hooktheory/TheoryTab (ToS
   forbids scraping/TDM/bulk use), full Wikifonia/EWLD (non-commercial +
   copyrighted). Legal basis for chords specifically: bare chord progressions
   are generally not copyrightable (functional building blocks — iReal Pro's
   position), and we only ingest CC0 / public-domain annotation sets. NB:
   "not copyrightable in the abstract" ≠ "scrape any source"; the source ToS
   still governs, which is why Hooktheory is out. Get legal review before ship.

3. **Key-relative representation.** A chord token is
   `(interval-from-tonic 0..11, quality-class)`, e.g. `7:dom7` = V7. This is
   transposition-invariant, collapses the vocab to ~50–150 tokens, and
   generalizes across keys — the direct analogue of the command model going
   English-only to collapse its vocab. Absolute chords would blow the vocab up
   12×.

4. **Deterministic reconstruction, model stays symbolic.** The model predicts
   `(interval, quality-class)` only. A deterministic step maps that back to a
   concrete `magda::music::Chord` in the *current* key via the engine's
   existing `getChordIntervals()` / `buildChordInRootPosition()`. The model
   never emits MIDI ⇒ it cannot produce an invalid chord (same correctness
   guarantee as the command model).

5. **Reduced quality vocab.** MAGDA's `ChordQuality` has 36 values; we model
   ~10 quality-classes (maj/min/dom7/maj7/min7/dim/dim7/hdim7/aug/sus). The
   Python side is self-contained; the class→`ChordQuality` mapping is applied
   at the C++ integration boundary (Phase 2 wiring). See `chords/vocab.py`.

6. **GRU for Phase 1.** Chord context is short (6–8 chords), so the model is
   tiny either way; a GRU captures long-range harmonic structure (turnarounds,
   cadences) better than a causal-conv of the same size, and that long-range
   sense is exactly what should beat the current Krumhansl rule engine. (Conv
   would be the pick only if an FPGA target reappeared — not a concern here;
   this runs CPU-local like llama.cpp.)

7. **One model, three capabilities.** Continuation = condition on prefix.
   Reharmonization = condition on progression, resample per slot. Generation =
   condition on key/mood only. Train once, unlock incrementally.

## Integration seam (verified against the codebase)

The chord engine already has most of the scaffolding:

- `magda/daw/music/ChordSuggestionEngine.hpp` — an existing next-chord
  predictor (Krumhansl-statistics based). Keeps a `std::deque<Chord>` context
  (max 6) + inferred key/mode. **The learned model slots in as an additional
  candidate `source`**, same input (context + key), same output type.
- `SuggestionItem = {Chord, score, degree, source}` — already has a `source`
  field to distinguish model vs rule suggestions.
- `magda/daw/audio/plugins/MidiChordEnginePlugin.hpp` — holds
  `AIProgression {name, description, vector<Chord>}` + `getAIProgressions()`,
  which backs the UI's AI-progression list → the target for Generation output.
- `magda/agents/music_agent.hpp` — already emits `OpCode::Chord` instructions;
  downstream consumer for the agent-driven path.
- Suggestions are computed for the UI, not inside `applyToBuffer`, so model
  inference is off the audio thread — no real-time-safety constraint.

`magda::music::Chord` (ChordTypes.hpp) carries both a symbolic form
(`ChordRoot` pitch-class + `ChordQuality` + optional scale `degree`) and a MIDI
voicing. We model the symbolic/key-relative part; the `degree` field is the
bridge to our interval token.

## Metric

Objective, like the command model's `val_e2e`:
- next-chord **top-1 / top-k** accuracy on held-out songs
- **perplexity** of the held-out sequences
- musical sanity: **in-key %**, **cadence/resolution rate**

Phase 0 (n-gram) sets the floor; Phase 1 (GRU) must beat it.

## Phases

- **Phase 0** (this scaffold): representation + KN n-gram baseline + eval +
  data pipeline. Runnable end-to-end via `smoke.py` with zero downloads.
- **Phase 1 — Continuation**: download corpora → tokenize → train the GRU →
  beat the n-gram floor → wire into `ChordSuggestionEngine` as a `source`.
- **Phase 2 — Reharmonization**: same weights, condition on progression.
- **Phase 3 — Generation**: same weights, key/mood → `AIProgression`.

## Open questions

- Mood/genre conditioning: Billboard has some metadata; decide the tag set
  once real data is in.
- Voicing/inversion: modelled as symbolic only for now; inversion + voice
  leading handled deterministically at reconstruction (engine already voices).
- How much the GRU actually beats a good 3-gram — chord n-grams are strong, so
  Phase 1 has to earn its keep on long-range structure.
