# MAGDA Model Training

How to retrain the local model. Read this first every time, because the two
silent footguns (steps below) waste a full Colab run if you miss them.

## What this trains

One LoRA adapter over **Qwen2.5-Coder-7B-Instruct**, covering the three console
agents:

- **Router** - classify a request as COMMAND / MUSIC / BOTH
- **Command** - generate MAGDA DSL (`track(...)`, `filter(tracks)...`, etc.)
- **Music** - generate notes / chords / arpeggios in compact notation

Output is a `q4_k_m` GGUF that MAGDA loads as the local model (provider
`LLAMA_LOCAL`). The cloud providers read the full runtime prompt and don't need
any of this; **only the local model has to be retrained when the DSL or prompts
change.**

## The loop (TL;DR)

1. Edit examples in `generate_dataset.py`.
2. From the **repo root**: `python3 training/generate_dataset.py` -> rewrites `dataset.jsonl`.
3. Commit `generate_dataset.py` + `dataset.jsonl`.
4. Run `finetune_magda.ipynb` in **Google Colab** (GPU) -> GGUF.
5. Download the GGUF, load it in MAGDA's AI Settings (or publish it - see below).

## Files

| File | Role |
|------|------|
| `generate_dataset.py` | Source of the **router + command** examples; regenerates `dataset.jsonl`. |
| `dataset.jsonl` | Generated training data. **Also the only home for the music examples** - they are not in the script. |
| `finetune_magda.ipynb` | The Colab fine-tune notebook. |

## Step 1 - edit examples

Everything except music lives in Python lists in `generate_dataset.py`:

- `router_examples` - `(user_text, "COMMAND" | "MUSIC" | "BOTH")`
- `command_examples` - `(user_text, dsl)` with no state context
- `state_examples` - `(state_json, user_text, dsl)` - include a `"scope":"all_tracks"`
  snapshot when the example is the master-selected fan-out
- `note_state_examples`, `groove_examples`, `combined_examples`
- `COMMAND_SYSTEM` / `ROUTER_SYSTEM` / `MUSIC_SYSTEM` - the **shortened** training
  prompts. These are intentionally short (the full runtime prompt is too long to
  fine-tune on); keep them terse and only add a line when you teach new syntax.

The **music** examples are carried over verbatim from the existing
`dataset.jsonl` (the script reads them back out by system prompt). They are not
reproducible from the script.

## Step 2 - regenerate the dataset

```bash
# from the repo root, NOT from training/
python3 training/generate_dataset.py
```

It prints the counts (router / command / music) and rewrites `training/dataset.jsonl`.

> **Footgun 1 - never delete `dataset.jsonl` first.** The script reads music
> examples out of the existing file before overwriting it. Delete it and you
> permanently lose every music example. It must be run from the repo root too,
> or it can't find the file.

Commit both `generate_dataset.py` and the regenerated `dataset.jsonl`.

## Step 3 - run the Colab notebook

Open `finetune_magda.ipynb` in Google Colab with a **GPU runtime** (A100 best,
T4 works). It can't run on the Mac - it's CUDA + Colab-specific (Drive mount,
`unsloth`). Run the cells top to bottom; it trains the LoRA (5 epochs), then
merges and exports a `q4_k_m` GGUF to Google Drive
(`MyDrive/magda-training/`).

> **Footgun 2 - replace the Drive copy of the dataset.** The "load dataset" cell
> prefers `MyDrive/magda-training/dataset.jsonl` if it already exists. If you
> leave an old copy there it silently trains on stale data. Either upload the
> new `dataset.jsonl` over it, or delete the Drive copy so the cell re-uploads
> the fresh one.

## Step 4 - deploy the GGUF

> **Never download the GGUF directly out of Colab** (the file-browser download
> or `files.download()`). It crawls and takes forever for a multi-GB file. The
> notebook already copies the GGUF to Google Drive (`MyDrive/magda-training/`) -
> always grab it from **drive.google.com** instead, which is far faster.

Download the `.gguf` from Drive to the Mac, then use it locally: MAGDA -> AI
Settings -> set the local model path to the `.gguf` and select the local
provider (`Config::getLocalModelPath()` / `AISettingsDialog`).

## Step 5 - publish to HuggingFace (REQUIRED to ship it)

A local GGUF only works on your machine. Users get the model through the in-app
downloader, which pulls **one pinned file** from HuggingFace. Shipping a new
model is two parts and it is NOT done until both are:

1. **Upload the GGUF** to `ConceptualMachines/magda-gguf` with a bumped version
   in the filename (`magda-vX.Y.Z-q4_k_m.gguf` - bump from the current
   `magda-v0.3.0-...`):

   ```bash
   pip install -U huggingface_hub
   huggingface-cli login                       # once, needs a write token
   huggingface-cli upload ConceptualMachines/magda-gguf \
       "/path/to/local.gguf" magda-vX.Y.Z-q4_k_m.gguf
   ```

2. **Bump the pinned URL** in `ModelDownloader::getDefaultModelUrl()`
   (`magda/agents/model_downloader.cpp`) to the new filename, then commit. The
   downloader is pinned to a single versioned filename, so **until this string
   changes the new model is invisible to every user** - this is the step that is
   easy to forget and the whole reason a retrain ships nothing on its own.

## Validating

In-notebook inference is unreliable (the notebook says so - RoPE shape bugs in
the 4-bit path). Validate after export by loading the GGUF in MAGDA (or
`llama-server`) and trying the things you added. For the all-tracks work, select
the master track and try "group all tracks" / "mute all tracks" - it should emit
`filter(tracks).track.group(...)` / `filter(tracks).track.set(mute=true)`.

## Knobs (in the notebook)

Base `Qwen/Qwen2.5-Coder-7B-Instruct`, LoRA `r=32` / `alpha=32`, 5 epochs,
`lr=2e-4`, cosine schedule, export quant `q4_k_m`. Bump epochs or add examples
if a new construct isn't sticking.
