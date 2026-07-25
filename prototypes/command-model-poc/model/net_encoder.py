"""Pretrained-encoder intent+slots model (#1847 option 3).

Same shape as the from-scratch net in `net.py`; only the encoder changes:

    now:  random embedding + 3 dilated convs -> mean-pool -> intent head
                                             -> per-token -> BIO slot head
    new:  pretrained transformer encoder     -> mean-pool -> intent head
                                             -> per-token -> BIO slot head

`dataset/tagging.py`, `reconstruct()`, `magda_dsl/` and the renderer are all
untouched — the model still does perception only, and the deterministic side
still builds the DSL, so malformed DSL stays structurally impossible.

The point of the swap is the language prior. The conv net has no way to know
"can you" is not a noun phrase, so it names tracks after whichever word sat in
the name position during training; a pretrained encoder has read English.

Still a single forward pass: no autoregression, no KV cache, no sampling.

Plugin @mentions are collapsed to opaque `<alias>` / `<alias.param>` tokens
before the model sees them, exactly as before (see `model/data.py:canon`), and
both are registered as additional special tokens so the subword tokenizer keeps
each as ONE unit rather than splitting it into `<`, `alias`, `>`.
"""
from __future__ import annotations

import torch
import torch.nn as nn
from transformers import AutoModel, AutoTokenizer

ALIAS_TOKENS = ["<alias>", "<alias.param>"]

# Candidates the issue asks to choose between, on measured accuracy.
PRESETS = {
    # English-only. The command surface is English (see findings.md: the word
    # tokenizer is ASCII-only, so the multilingual models never saw non-English
    # input anyway). These spend their parameters on English instead of on a
    # 250k multilingual vocab.
    "deberta":     "microsoft/deberta-v3-base",               # 184M, English
    "roberta":     "FacebookAI/roberta-base",                 # 125M, English
    "distilroberta": "distilbert/distilroberta-base",         #  82M, English
    # Multilingual. Kept for the #1846 question; see the multilingual section
    # in findings.md before trusting any non-English number from them.
    "xlmr":        "FacebookAI/xlm-roberta-base",             # 278M, multilingual
    "mdeberta":    "microsoft/mdeberta-v3-base",              # 278M, multilingual
    "distilmbert": "distilbert-base-multilingual-cased",      # 135M, distilled
    "minilm":      "microsoft/Multilingual-MiniLM-L12-H384",  # 117M, distilled
}


def load_tokenizer(model_name: str):
    """Fast tokenizer with the alias placeholders registered as atomic units.

    `from_slow=True` on the retry is load-bearing for the sentencepiece-based
    checkpoints (mdeberta, minilm): transformers 5.14 tries to read their
    `spm.model` / `sentencepiece.bpe.model` as a tiktoken file and dies. Going
    via the slow tokenizer converts it properly. A fast tokenizer is required
    either way — the subword/word alignment in data_encoder.py needs
    `word_ids()`, which slow tokenizers do not provide.
    """
    name = PRESETS.get(model_name, model_name)
    try:
        tok = AutoTokenizer.from_pretrained(name)
    except ValueError:
        tok = AutoTokenizer.from_pretrained(name, from_slow=True)
    tok.add_special_tokens({"additional_special_tokens": ALIAS_TOKENS})
    return tok


class EncoderIntentSlotNet(nn.Module):
    def __init__(self, model_name, n_intents, n_tags, tokenizer=None, dropout=0.1):
        super().__init__()
        name = PRESETS.get(model_name, model_name)
        # Force fp32: mdeberta-v3-base ships fp16 weights, and transformers 5.x
        # honours that, so its matmuls meet our fp32 heads and abort — as a
        # dtype mismatch on CPU, and as a Metal assertion ("Destination NDArray
        # and Accumulator NDArray cannot have different datatype") on MPS.
        self.encoder = AutoModel.from_pretrained(name, dtype=torch.float32)
        if tokenizer is not None:
            # Room for the two alias placeholders added above.
            self.encoder.resize_token_embeddings(len(tokenizer))
        hidden = self.encoder.config.hidden_size
        self.dropout = nn.Dropout(dropout)
        self.slot_head = nn.Linear(hidden, n_tags)
        self.intent_head = nn.Linear(hidden, n_intents)

    def forward(self, input_ids, attention_mask):
        h = self.encoder(input_ids=input_ids,
                         attention_mask=attention_mask).last_hidden_state
        h = self.dropout(h)                              # [B, L, H]

        slot_logits = self.slot_head(h)                  # [B, L, n_tags]

        m = attention_mask.unsqueeze(-1).float()         # [B, L, 1]
        pooled = (h * m).sum(1) / m.sum(1).clamp(min=1)  # masked mean-pool
        intent_logits = self.intent_head(pooled)         # [B, n_intents]
        return intent_logits, slot_logits

    def num_params(self):
        return sum(p.numel() for p in self.parameters())
