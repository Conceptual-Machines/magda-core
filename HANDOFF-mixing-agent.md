# Handoff: Mixing Agent (#886) + mixer-view cockpit (#1403)

## Status
- **Branch:** `feat/mixing-agent` (off `dev/0.11.0`). **Nothing pushed** (branch or PRs).
- **Submodule:** `third_party/juce-llm` is pushed + tagged **v0.3.0** (token-usage parsing). The parent pins that commit (`961db65`).
- The whole-mix analysis agent works end-to-end and is validated on real audio (raw multitrack sessions + finished/mixed material). The UI integration (offline render) and the local-ML layer (CLAP) are the open threads.

## What's built (3 layers)

### 1. #1403 cockpit — AI console reference + capture UI (committed, builds clean)
`magda/daw/ui/panels/content/AIChatConsoleContent.{cpp,hpp}`
- In any view (when a track is selected): a footer **reference-track dropdown** (chevron) + **capture** (record dot) button, with thin vertical separators (not box borders). Reference menu uses the theme font.
- Capture arms `TrackMeasurementManager` (subject + refs + masking), accumulates during playback, gathers on stop; restores prior measurement enablement.
- Mixer view (or an attached capture, any view) hard-scopes the console's `MIXING` route, bypassing the RouterAgent; `@alias` / `[COMMAND:]` still opt out.
- The `MIXING` dispatch now CALLS the agent for the **capture path**: an attached capture's per-track snapshots + masking findings are mapped into `MixAnalysisAgent::Input` (subject identity + user prompt -> `question`, project tempo -> `bpm`), `generate()` runs on the existing worker thread, and the prose lands in the console via a new `mixAnalysis` output channel. Spectral/tonal/timeline are left unset on this path (realtime capture doesn't compute them). The **offline-render "analyse all tracks"** path (full input) is still open (see thread A).
- Slice-1 (active-view glyph) + slice-2 (routing + glyph dark-fill fix) were merged separately as PR #1405 into dev/0.11.0.

### 2. #886 MixAnalysisAgent — whole-mix "listening" via measurements
`magda/agents/mixing_agent.{hpp,cpp}` (in `magda_agents`)
- Analysis-only (returns prose, touches no device). `Input` -> LLM -> `Result`.
- `Input`: per-track `TrackMix` + master + `MaskingPair[]` + `Segment[]` timeline + `references` (TrackMix[]) + bpm/genre/question.
- `TrackMix` fields: LUFS-I, sample peak, true-peak(+valid), PLR, PSR, correlation, width, spectralCentroid/Flatness/Rolloff, tonalDb[6] (sub/low/low-mid/mid/high-mid/high).
- `Segment`: label + start/endSec + integratedLufs + centroid + width + tonalDb[3] (low/mid/high). Source-agnostic so UI song-sections or auto fixed-windows both fit.
- `generate()` blocks; uses `Config::getAgentLLMConfig(role::COMMAND)` -> `createLLMClient` -> `sendRequest`. Run off the message thread.
- Prompt (in `getSystemPrompt()`): genre-relative judgement, percussion/clipping nuance, "trust measured masking", compare to `[REF]` rows when present.

### 3. MixAnalysisInput — shared measurement pipeline (the reusable core)
`magda/daw/audio/analysis/MixAnalysisInput.{hpp,cpp}` (in `magda_daw`)
- `build(sampleRate, tracks, master?, references, opts)` -> `MixAnalysisAgent::Input`. Takes audio buffers (rendered stems in the app / loaded files in tests). `master==nullptr` => normalised (-1 dBFS) stem sum.
- `fingerprint(buf, sr, name, role)` -> master-style TrackMix (true-peak on).
- Internally: `TrackMeasurer` (LUFS/peak/corr/width), `BandSpectrum::computeMaskingBandsDb` (per-track avg spectrum), `MaskingDetector::detectMasking`, spectral features, timeline slicing.
- Caller fills bpm/genre/question afterward.

### Harness (exploration, not CI)
`tests/test_mix_analysis_audio.cpp` — hidden `[.][mix_analysis][audio]` test. Loads stems, builds input via `MixAnalysisInput`, runs N models, prints. Sweeps a whole session/pack library.
`tests/test_mix_analysis_agent.cpp` — deterministic synthetic payload test (runs in CI, no network).

## Commits on the branch (newest first)
```
52d96c809 Extract mix-analysis measurement into MixAnalysisInput module
7cc8abbb7 gitignore exploratory mix-analysis run output
7ea59e53e Mix analysis: reference tracks as the genre target
649b1d8ae Mix analysis harness: Full Mix stem as master; neutral question
1c73f2182 Mix analysis harness: fix measurement speed (true-peak + FFT-frame)
6de71490e Mix analysis harness: iterate a directory of song sessions
f3b1f5445 Mix analysis: BPM + genre as project-supplied context (no detection)
ac15c3e25 Mix analysis: spectral descriptors + section/window timeline
43277a2e2 Mix analysis: true-peak, tonal balance, real masking + prompt
df7d7a475 Mix analysis: report token usage + compare multiple models
5b75556f3 Add real-audio mix-analysis harness (#886)
4e09ad139 Add MixAnalysisAgent (#886)
005e834db..df084acc8  AI console cockpit (#1403) x4
```
(juce-llm submodule: `961db65` = v0.3.0, pushed.)

## How to run the harness
```
./cmake-build-debug/tests/magda_tests "[mix_analysis][audio]"
```
Env vars:
- `MIX_ANALYSIS_AUDIO_DIR` — root of per-song subfolders (or a single song's stems). Default: bundled turkuaz fixture.
- `MIX_ANALYSIS_MODELS` — comma list, provider inferred per name (gpt-5*/o* => responses, claude* => anthropic, gemini* => gemini, deepseek* => deepseek). Default `gpt-5`.
- `MIX_ANALYSIS_REFERENCES` — dir of wavs or comma-sep files (reference masters; measured once).
- `MIX_ANALYSIS_GENRE`, `MIX_ANALYSIS_BPM` — song context (project-supplied; omitted if unset).
- `MIX_ANALYSIS_SEGMENTS` — timeline slice count (default 16).
- API keys read from `<repo>/.env` (OPENAI/ANTHROPIC/GEMINI/DEEPSEEK_API_KEY).
- A `000_Full_Mix` stem in a song folder is auto-used as the master (else a normalised stem sum).
- Test fixtures: gitignored `tests/fixtures/audio/turkuaz/` (Turkuaz multitrack). External libraries tested: Telefunken sessions + Splice "Deep House Stems" (on the user's external SSD; paths in shell history).
- Run output dumps (`mix-analysis-*.txt`) are gitignored.

## Exploration findings
- **Payload is tiny** (~2-7 KB / ~1-3k tokens even with all metrics + 16-segment timeline). Input size is a non-issue; cost/latency is dominated by model output reasoning.
- **GPT-5** is a reasoning model (the agent runs it at reasoning effort "low" via llm_config_utils); its output token count includes hidden reasoning tokens.
- **Token usage** is now parsed from all providers (juce-llm v0.3.0). NOT YET parsed: Anthropic `cache_read_input_tokens` / `cache_creation_input_tokens` (cached runs report `input_tokens=1`). Open follow-up.
- **Raw stems vs finished mixes:** on raw multitracks the model prescribes a full rebuild; on finished masters it gives proportionate mastering notes (mostly the mild inter-sample TP clipping all loud masters share). Blind test (question says neither "raw" nor "mixed").
- **References fixed genre miscalibration:** without refs the model flagged deep-house's low-end-forward / gentle-top balance as a fault; with reference masters it says "genre-appropriate, no need to brighten" and instead finds real anomalies vs the genre (e.g. a master wider/less-mono than the references). References > prose genre-conditioning.
- **Perf:** true-peak (4x oversampler) is master-only (per-track = sample peak), and the per-band FFT is sampled ~128 frames/song (not every block — `computeMaskingBandsDb` rebuilds an FFT per call). A 4-song sweep went from >20 min (stuck) to minutes.

## Open threads / next steps

### A. Offline mix analysis (the LOCKED design)

Design settled 2026-06-08. Everything is OFFLINE RENDER. There is no live capture
and no "whole-mix vs relational" mode split -- the whole mix IS the maximally
relational case (every track judged against every other; masking is already
pairwise across the full set). It is ONE analysis with two optional knobs and two
depths.

**One action, triggered from the CONSOLE** (results land in the console, so the
trigger belongs there). Reuse the #1403 footer record-button SLOT, repurposed to
"Analyze" with a processing state ("Rendering... / Analysing..."), NOT an
armed/playback affordance. Swap the glyph to an analyze/spark icon + "Analyze mix"
tooltip.

**Two knobs:**
- *Track set* -- default is ALL tracks (the whole mix). Narrowing to a few is just
  a smaller `tracksToDo` bitset (driven by selection + what's ticked in the
  references dropdown). The agent reasons over whatever set it's given, always
  relationally. The `Input` struct has NO subject field -- correct; focus is a
  prompt line, not a structural mode.
- *Range* -- if a loop region or time selection is set, render only that; else the
  full edit. Export already does this (`ExportRange::LoopRegion` reads
  `transport.getLoopRange()`; time-selection has a TODO to pull from
  `SelectionManager`).

**Two depths** (the fast-vs-thorough tradeoff; both offline, both range-bounded):
- *Shallow ("Quick / master")* -- ONE render pass of the full mix (empty
  `tracksToDo` = all tracks summed) -> `MixAnalysisInput::fingerprint(masterBuf)`
  -> agent. Top-level mix/master feedback. Cheap even on plugin-heavy projects.
- *Deep ("Per-track")* -- master pass (all tracks) + each track isolated (N+1
  passes, one `tracksToDo` bit each) -> `MixAnalysisInput::build(...)` (cross-track
  masking + per-track tonal balance + timeline) -> agent. Relational per-track
  feedback. Scales with track count; shallow is the sensible default when N is large.
- No new DSP: `fingerprint()` (shallow) and `build()` (deep) already exist; two callers.

**Render primitive:** `te::Renderer::renderToFile(desc, outFile, edit, range,
tracksToDo, usePlugins=true, useACID, clips={}, useThread)` -- `tracksToDo` is a
`juce::BigInteger` bitset over the edit's tracks (empty = all). Map MAGDA `TrackId`
-> TE track -> index in `te::getAllTracks(edit)` order to set bits. Render each pass
to a temp WAV, then load the buffer with the format manager (the harness already
loads WAVs into buffers) and hand to `MixAnalysisInput`.

**Threading:** the edit-mutating setup (stop transport, `freePlaybackContextIfNotRecording`,
`ReallocationInhibitor`, enable plugins, `prepareForRendering()`) must run on the
message thread -- see `MainWindowExport.cpp:290-383`. The render passes themselves
run off the message thread (`useThread`/a bg job). Sequence: message-thread setup
-> bg: N renders + load buffers + `build`/`fingerprint` + `generate()` ->
`MessageManager::callAsync` to post prose into the `MIXING` channel. Show the
processing state in the console throughout. Add `endAllowance` (a tail) so
reverbs/delays don't get cut at the range end.

**Cost note to surface (no silent caps):** deep = N+1 passes; `log`/note the pass
count and that shallow avoids it. Later optimization (deferred): a SINGLE offline
render of the full edit with the measurement taps armed, sampling per-track as it
renders (one pass at offline speed) -- replaces N passes; not now.

References = future EXTERNAL reference masters (genre targets), separate from the
in-project track-set selector. See thread B (CLAP auto-retrieval).

The realtime-capture path already committed (`mixCapture_` snapshots -> agent) is now
LEGACY/dormant; offline render supersedes it. The #1403 live-capture UI can be
removed once offline lands (ask before ripping out).

### B. Local ML — already in the build (CLAP), it's WIRING not model selection
MAGDA already ships the full CLAP zero-shot stack (issues #768/#1319), in `magda/daw/media_db/`:
- `ClapAudioEncoder` (audio -> 512-d embedding), `ClapTextEncoder` + `RobertaTokenizer` (text -> 512-d), `ZeroShotTagger` (instrument taxonomy + family map, cosine scoring), and the media DB indexes audio with embeddings + tags + semantic search.
Reuse for mix analysis:
1. **Per-stem instrument role** -> run each rendered stem through `ZeroShotTagger`, replacing the brittle filename `inferRole` (which collapsed "DRUMS - Kick" -> "DRUMS").
2. **Auto reference retrieval** -> `ClapAudioEncoder.embed(master)` + cosine search against a reference-embedding store (reuses media-DB semantic-search). Makes the reference feature automatic.
3. **Genre** -> same zero-shot mechanism with a genre label set (small add).
- Optional upgrades only if CLAP zero-shot isn't crisp enough: **PANNs** (AudioSet instrument tags), **Essentia** genre heads, **Demucs** (separate a stereo song into stems).
- **Platform gate:** ONNX is absent on Intel-Mac builds -> all CLAP features must degrade to filename roles + manual references.

### C. Smaller follow-ups
- Anthropic cache-token parsing (see findings).
- The "Analyze" button is a no-prompt trigger by construction (it runs the offline
  analysis with no question), so the old "allow empty send" item is absorbed -- the
  prompt box stays optional-question only.
- `#886` work becomes its own PR(s) off the active dev branch when ready; the branch
  currently stacks everything.

## Gotchas
- Pre-commit hook runs clang-format and aborts if it reformats; just `git add` the reformatted files and commit again (no `--amend`, no `--no-verify`).
- `juce-llm` is a submodule with its OWN remote/PR flow; `main` is protected (push went through with an admin-bypass warning — prefer a PR or a version tag). We tagged v0.3.0.
- `std::cout` is fully buffered when redirected to a file — the harness sets `std::cout << std::unitbuf` so multi-song progress is visible.
- The harness reads from an external SSD for the non-fixture libraries; copying a couple of sessions into the local gitignored fixtures dir is faster if iterating.

## Related
- Issues: #886 mixing agent, #1403 mixer reference-capture UI, #1402 view-context routing (merged via #1405), #1388 measurement layer, #1390 masking, #768/#1319 CLAP/media-DB.
- Memory: `project_mixing_agent_ux.md`, `project_analysis_devices.md`, `project_gpt5_no_temperature.md`, `project_0_8_0_media_db.md`.
