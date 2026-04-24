set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"   # all / small / large
DSHARP_BIN="${3:-$HOME/tools/dsharp/dsharp}"
OUTDIR="${4:-$HOME/benchmarks/ddnnf_dsharp}"
LOGDIR="${5:-$HOME/results/logs/dsharp}"

mkdir -p "$OUTDIR" "$LOGDIR"

if [[ ! -x "$DSHARP_BIN" ]]; then
  echo "dsharp binary not found or not executable: $DSHARP_BIN" >&2
  exit 1
fi

iter_benchmarks "$CSV" "$SIZE_FILTER" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
do
  [[ "$format" == "cnf" ]] || continue
  [[ -f "$input_path" ]] || { echo "Skip $name: input not found -> $input_path"; continue; }

  out_file="$OUTDIR/${name}.nnf"
  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  echo "========== dSharp compiling: $name =========="
  echo "Input : $input_path"
  echo "Output: $out_file"

  # 默认形式：dsharp input.cnf > output.nnf
  # 如果你本地 dSharp 参数不同，只改这一行即可
  /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
    "$DSHARP_BIN" "$input_path" > "$out_file" 2> "$log_file" || {
      echo "FAILED: $name"
      rm -f "$out_file"
      continue
    }

  echo "DONE: $name"
done