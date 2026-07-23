# %% [markdown]
# # MAGDA command model - LoRA fine-tune (Colab + unsloth)
#
# Thin notebook: pulls the FROZEN dataset, LoRA fine-tunes a tiny base, exports
# GGUF for the repo's llama.cpp. Logic lives in the repo, not here.
#
# Runtime: GPU (free T4 is enough for a 0.5B LoRA). Paste cells into Colab, or
# open this file directly (it's jupytext `# %%` format).
#
# Inputs : Drive  My Drive/magda-command-model/{train,val}.chat.jsonl
#          (staged from data/*.chat.jsonl via Google Drive for Desktop)
# Output : My Drive/magda-command-model/command-model.gguf

# %% [markdown]
# ## 1. Install
# %%
import subprocess, sys

def pip_install(*args):
    subprocess.run([sys.executable, "-m", "pip", "install", "-q", *args], check=True)

pip_install("unsloth[colab-new] @ git+https://github.com/unslothai/unsloth.git")
pip_install("--no-deps", "trl", "peft", "accelerate", "bitsandbytes")

# %% [markdown]
# ## 2. Get the data (Google Drive)
# Data is staged in Drive at My Drive/magda-command-model/. We mount and read it,
# and save the trained GGUF back to the same folder so it auto-syncs to the Mac
# (no manual upload/download). The chat files carry the SYSTEM prompt the C++
# inference path must also send (model/format.py).
# %%
from google.colab import drive
drive.mount("/content/drive")

DRIVE_DIR = "/content/drive/MyDrive/magda-command-model"

import json, os

def load_chat(name):
    return [json.loads(l) for l in open(os.path.join(DRIVE_DIR, name), encoding="utf-8") if l.strip()]

train_rows = load_chat("train.chat.jsonl")
val_rows = load_chat("val.chat.jsonl")
print(f"train={len(train_rows)} val={len(val_rows)}")

# %% [markdown]
# ## 3. Base model (tiny, instruct, multilingual)
# Start at 0.5B; Llama-3.2-1B is the fallback. The eval back home picks the winner.
# %%
from unsloth import FastLanguageModel

BASE = "unsloth/Qwen2.5-0.5B-Instruct"
MAX_SEQ = 1024

model, tokenizer = FastLanguageModel.from_pretrained(
    model_name=BASE, max_seq_length=MAX_SEQ, load_in_4bit=True,
)
model = FastLanguageModel.get_peft_model(
    model, r=16, lora_alpha=16, lora_dropout=0.0, bias="none",
    target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                    "gate_proj", "up_proj", "down_proj"],
    use_gradient_checkpointing="unsloth", random_state=7,
)

# %% [markdown]
# ## 4. Apply the chat template
# %%
from datasets import Dataset

def fmt(rows):
    texts = [tokenizer.apply_chat_template(r["messages"], tokenize=False,
                                           add_generation_prompt=False) for r in rows]
    return Dataset.from_dict({"text": texts})

train_ds, val_ds = fmt(train_rows), fmt(val_rows)

# %% [markdown]
# ## 5. Train (LoRA SFT)
# %%
from trl import SFTTrainer, SFTConfig

trainer = SFTTrainer(
    model=model, tokenizer=tokenizer,
    train_dataset=train_ds, eval_dataset=val_ds,
    args=SFTConfig(
        dataset_text_field="text", max_seq_length=MAX_SEQ,
        per_device_train_batch_size=16, gradient_accumulation_steps=1,
        warmup_ratio=0.05, num_train_epochs=3, learning_rate=2e-4,
        logging_steps=20, eval_strategy="epoch", optim="adamw_8bit",
        weight_decay=0.01, lr_scheduler_type="cosine", seed=7,
        output_dir="outputs", report_to="none",
    ),
)
trainer.train()

# %% [markdown]
# ## 6. Sanity check
# %%
FastLanguageModel.for_inference(model)
SYSTEM = train_rows[0]["messages"][0]["content"]
for req in ["create a bass track with serum and ott", "mute Drums", "把贝斯轨道设为蓝色"]:
    msgs = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": req}]
    ids = tokenizer.apply_chat_template(msgs, tokenize=True, add_generation_prompt=True, return_tensors="pt").to("cuda")
    out = model.generate(input_ids=ids, max_new_tokens=128, do_sample=False)
    print(req, "->", tokenizer.decode(out[0][ids.shape[1]:], skip_special_tokens=True))

# %% [markdown]
# ## 7. Export GGUF for llama.cpp -> Drive (auto-syncs to Mac)
# Then back home:
#   mkdir -p tools/command-model-poc/model/artifacts
#   cp "/path/to/Google Drive/My Drive/magda-command-model/command-model.gguf" tools/command-model-poc/model/artifacts/command-model.gguf
#   cd tools/command-model-poc
#   python3 -m eval.run --model model/artifacts/command-model.gguf
# %%
import glob, os, shutil

model.save_pretrained_gguf("command-model", tokenizer, quantization_method="q4_k_m")

ggufs = glob.glob("command-model/*.gguf") + glob.glob("*.gguf")
if not ggufs:
    raise RuntimeError("GGUF export produced no .gguf files")

dst = os.path.join(DRIVE_DIR, "command-model.gguf")
shutil.copy(ggufs[0], dst)
print("synced to Drive ->", dst)
