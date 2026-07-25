# In-app test prompts — encoder command model (#1847)

In AI Settings -> Advanced, set the **Command** row's provider to
**Fast Inference (Command)**, then type these into the console. The debug log says which backend answered:

    MAGDA CommandAgent (fast_inference, encoder): ...   <- the new model
    MAGDA CommandAgent (fast_inference): ...            <- fell back to the conv net

"Expected" below is what the shipping build actually produces, captured from
`model/artifacts_onnx/dist`. If the app disagrees with this file, the C++ path
has drifted from the Python one and `test_command_model_onnx.cpp` should be
catching it — check that first.

---

## 1. The cases that motivated the issue

Every one of these was wrong before. This is the group worth trying first.

| type this | expect |
|---|---|
| `can you mute the guitar` | `track(name="Guitar").track.set(mute=true)` |
| `track with @fm_0` | `track(name="", new=true).fx.add(name="<fm_0>")` |
| `put @serum on a new track` | `track(name="", new=true).fx.add(name="<serum>")` |
| `i want a track with @massive` | `track(name="", new=true).fx.add(name="<massive>")` |
| `push Keys up to 2 dB` | `track(name="Keys").track.set(volume_db=2)` |
| `the bass needs @pro_q_3` | `track(name="Bass").fx.add(name="<pro_q_3>")` |
| `drag Keys to slot 5` | `track(name="Keys").track.move(index=5)` |
| `get rid of the vocals track` | `track(name="Vocals").delete()` |

The old model answered these with `track(name="Can You")`, `track(name="Push")`,
`track(name="Bass Needs")` and a rack on the selected track.

## 2. Tracks and devices

| type this | expect |
|---|---|
| `gimme a track called Night Bass` | `track(name="Night Bass", new=true)` |
| `make me a punchy drum track` | `track(name="Punchy Drum", new=true)` |
| `start a lead track with @vital and @ott on it` | two lines: create Lead + `<vital>`, then `<ott>` |
| `chuck @reverb and @delay on the pads track` | two `fx.add` lines on Pads |
| `wrap @serum in a rack on the bass track` | `track(name="Bass").rack.new().fx.add(name="<serum>")` |

## 3. Mixing

| type this | expect |
|---|---|
| `bring the bass down to -6db` | `volume_db=-6` — note the missing space |
| `pan the hats left` | `pan=-0.5` |
| `put the guitar hard right` | `pan=1` |
| `make the vocals track pink` | `colour="#ff6ad5"` |

## 4. Clips and notes

| type this | expect |
|---|---|
| `clips under 2 bars on the drums track` | `clips.select(clip.length_bars < 2)` |
| `put an 8 bar clip on Bass` | `clip.new(length_bars=8)` |
| `quantize the keys to 16ths` | `notes.quantize(grid=0.25)` |
| `drop the bass notes 12 semitones` | `notes.transpose(semitones=-12)` — **negative** |
| `select every C3 on the bass track` | `notes.select(note.pitch == C3)` |
| `bump the drums velocity to 110` | `notes.set_velocity(value=110)` |

## 5. Sloppy typing

| type this | expect |
|---|---|
| `pls make a bass track` | `track(name="Bass", new=true)` |
| `rename Keys, make it Rhodes` | `track(name="Keys").track.set(name="Rhodes")` |
| `new track, @surge_xt on it` | `track(name="", new=true).fx.add(name="<surge_xt>")` |

---

## Known wrong — don't report these as new bugs

Three failures are already understood and are *not* the encoder's fault.

**`solo the drums for a sec`** → emits `clip.new(length_bars=0)`. Genuine model
error, one of the ~8 misses in 231. `solo the drums` on its own is fine.

**`i dont need the perc track anymore, delete it`** → creates a track instead of
deleting one. Two-clause sentences where the verb trails the object are a known
weak spot; `delete the perc track` works.

**`select everything on the bass track`** → emits `.clips.select()` with no
condition, which the DSL interpreter **rejects**. This is a pre-existing bug in
the DSL surface, not the model: `command_model.cpp` has always emitted this for
`select_all_clips`, and the shipping grammar has never accepted it. Expect an
interpreter error, and see findings.md.

---

## If it falls back to the conv net

`CommandAgent` uses the encoder only when all three files are present in
`<dataDir>/CommandModel/models/`:

```
command_model.onnx   442 MB
tokenizer.json         8 MB
maps.json              1 KB
```

On macOS that is `~/Library/MAGDA/CommandModel/models/` — JUCE's
`userApplicationDataDirectory` is `~/Library`, not `~/Library/Application
Support`. It sits beside `MediaDB/models` and `StemSeparation/models`. Point
`MAGDA_COMMAND_MODEL_DIR` elsewhere to override. Regenerate the bundle with:

```bash
cd prototypes/command-model-poc
python -m model.export_onnx        # writes model/artifacts_onnx/dist/
```
