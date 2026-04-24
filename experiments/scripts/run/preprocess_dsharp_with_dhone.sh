set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"
DHONE_BIN="${3:-/home/zansin/my/target/release/dhone}"
IN_DIR="${4:-$HOME/benchmarks/ddnnf_dsharp}"
OUT_DIR="${5:-$HOME/benchmarks/ddnnf_dsharp_prepo}"
LOGDIR="${6:-$HOME/results/logs/dhone}"

mkdir -p "$OUT_DIR" "$LOGDIR"

if [[ ! -x "$DHONE_BIN" ]]; then
  echo "dhone binary not found or not executable: $DHONE_BIN" >&2
  exit 1
fi

iter_benchmarks "$CSV" "$SIZE_FILTER" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
do
  in_file="$IN_DIR/${name}.nnf"
  out_file="$OUT_DIR/${name}.nnf"
  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  [[ -f "$in_file" ]] || { echo "Skip $name: dsharp nnf not found -> $in_file"; continue; }

  echo "========== dhone preprocessing: $name =========="
  /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
    "$DHONE_BIN" "$in_file" -s "$out_file" > "$log_file" 2>&1 || {
      echo "FAILED: $name"
      rm -f "$out_file"
      continue
    }

  echo "DONE: $name"
done