set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"   # all / small / large
D4_BIN="${3:-$HOME/tools/d4/d4}"
OUTDIR="${4:-$HOME/benchmarks/ddnnf_d4}"
LOGDIR="${5:-$HOME/results/run3/logs/d4}"

mkdir -p "$OUTDIR" "$LOGDIR"

echo "CSV   : $CSV"
echo "SIZE  : $SIZE_FILTER"
echo "D4_BIN: $D4_BIN"
echo "OUTDIR: $OUTDIR"
echo "LOGDIR: $LOGDIR"

if [[ ! -x "$D4_BIN" ]]; then
  echo "d4 binary not found or not executable: $D4_BIN" >&2
  exit 1
fi

iter_benchmarks "$CSV" "$SIZE_FILTER" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
do
  echo "READ: name=$name input=$input_path size=$size_class format=$format"
  [[ "$format" == "cnf" ]] || continue
  [[ -f "$input_path" ]] || { echo "Skip $name: input not found -> $input_path"; continue; }

  out_file="$OUTDIR/${name}.nnf"
  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  echo "========== d4 compiling: $name =========="
  /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
    "$D4_BIN" "$input_path" -dDNNF -out="$out_file" > "$log_file" 2>&1 || {
      echo "FAILED: $name"
      rm -f "$out_file"
      continue
    }

  [[ -s "$out_file" ]] || {
    echo "FAILED: $name (empty output)"
    rm -f "$out_file"
    continue
  }

  echo "DONE: $name"
done