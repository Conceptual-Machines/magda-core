"""Score a trained GGUF on the M1 with the SAME harness as the baseline.

Closes the loop: train on Colab -> download .gguf -> run it through the exact
fixed test set here, on the machine that approximates the <500ms latency goal.

Runs via llama-cpp-python (the same llama.cpp the app ships). Lazy import so the
rest of the harness works without it installed.

    pip install llama-cpp-python
    python -m eval.run --model path/to/command-model.gguf
"""
from __future__ import annotations

from model.format import SYSTEM_PROMPT

_LLM_CACHE = {}


def _get_llm(model_path: str):
    if model_path not in _LLM_CACHE:
        try:
            from llama_cpp import Llama
        except ImportError as e:
            raise SystemExit("llama-cpp-python not installed: pip install llama-cpp-python") from e
        _LLM_CACHE[model_path] = Llama(
            model_path=model_path,
            n_ctx=1024,
            n_gpu_layers=-1,   # Metal on Apple Silicon
            verbose=False,
        )
    return _LLM_CACHE[model_path]


def make_parser(model_path: str, max_tokens: int = 192):
    """Return a `str -> DSL str` function backed by the GGUF model."""
    llm = _get_llm(model_path)

    def parse(text: str) -> str:
        out = llm.create_chat_completion(
            messages=[
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": text},
            ],
            temperature=0.0,
            max_tokens=max_tokens,
        )
        return out["choices"][0]["message"]["content"].strip()

    return parse
