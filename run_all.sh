#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
export PROJECT_ROOT="$(pwd)"
source experiments/scripts/common/env.sh

if [[ "${CLEAN_RESULTS:-0}" == "1" ]]; then
  rm -rf results/total_count results/repeated_queries results/merged/csv results/figures logs/run tmp/intermediate
fi

echo "=== selected models ==="
cat benchmarks/meta/selected_models.txt
echo

echo "=== enabled tools from config ==="
python3 - <<'PY'
import json
cfg = json.load(open("experiments/configs/experiment_config.json", "r", encoding="utf-8"))
for k, v in cfg["tools"].items():
    print(f"{k}: total={v.get('enabled_total')} repeated={v.get('enabled_repeated')}")
PY
echo

echo "=== capture machine info ==="
bash experiments/scripts/run/05_capture_machine_info.sh .
echo

echo "=== generate query sets ==="
python3 experiments/scripts/run/00_generate_query_sets.py \
  --project-root . \
  --config experiments/configs/experiment_config.json
echo

echo "=== run total count ==="
python3 experiments/scripts/run/10_run_total_count.py \
  --project-root . \
  --config experiments/configs/experiment_config.json
echo

echo "=== run repeated queries ==="
python3 experiments/scripts/run/20_run_repeated_queries.py \
  --project-root . \
  --config experiments/configs/experiment_config.json
echo

echo "=== aggregate results ==="
python3 experiments/scripts/merge/30_aggregate_results.py \
  --project-root . \
  --config experiments/configs/experiment_config.json
echo

echo "=== plot results ==="
python3 experiments/scripts/parse/40_plot_results.py \
  --project-root . \
  --config experiments/configs/experiment_config.json
echo

echo "=== pipeline finished ==="
echo "Merged CSV dir: results/merged/csv"
echo "Figures dir   : results/figures"