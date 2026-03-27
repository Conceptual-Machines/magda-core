#!/bin/bash
set -e

MODEL_32B=~/models/qwen2.5-coder-32b/Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf
MODEL_7B=~/models/qwen2.5-coder-7b/Qwen2.5-Coder-7B-Instruct-Q4_K_M.gguf
PYTHON=.venv/bin/python3

start_server() {
    local model=$1
    echo "Starting llama-server with $model..."
    llama-server -m "$model" --port 8080 -ngl 99 -c 8192 --log-disable > /tmp/llama-server.log 2>&1 &
    SERVER_PID=$!

    for i in $(seq 1 60); do
        if curl -sf http://127.0.0.1:8080/health > /dev/null 2>&1; then
            echo "Server ready."
            return 0
        fi
        sleep 1
        if [ $i -eq 60 ]; then
            echo "Server failed to start. Check /tmp/llama-server.log"
            exit 1
        fi
    done
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        echo "Stopping llama-server (pid $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
        SERVER_PID=""
    fi
}

# --- 32B ---
start_server "$MODEL_32B"
echo ""
echo ">>> 32B — unconstrained"
$PYTHON run_poc.py --32b
echo ""
echo ">>> 32B — GBNF-constrained"
$PYTHON run_poc.py --32b --grammar
stop_server

# --- 7B ---
start_server "$MODEL_7B"
echo ""
echo ">>> 7B — unconstrained"
$PYTHON run_poc.py --7b
echo ""
echo ">>> 7B — GBNF-constrained"
$PYTHON run_poc.py --7b --grammar
stop_server

# --- Summary ---
echo ""
echo "================================================================"
echo "SUMMARY"
echo "================================================================"
$PYTHON - <<'EOF'
import json
from pathlib import Path

results_file = Path("results/results.jsonl")
lines = results_file.read_text().strip().splitlines()
runs = [json.loads(l) for l in lines[-4:]]

header = f"{'Model':<40} {'Grammar':<8} {'Score':>7} {'Avg (s)':>8}  Per-case (s)"
print(header)
print("-" * 95)
for r in runs:
    per_case = "  ".join(f"{c.get('elapsed_s', 0):.2f}" for c in r["cases"])
    grammar = "yes" if r["grammar_constrained"] else "no"
    print(f"{r['model']:<40} {grammar:<8} {r['score_pct']:>6}%  {r['avg_elapsed_s']:>7.2f}s  {per_case}")
EOF
