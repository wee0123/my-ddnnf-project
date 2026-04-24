set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"   # all / small / large
GANAK_BIN="${3:-$HOME/tools/ganak/build/ganak}"
OUTDIR="${4:-$HOME/results/minimal/timing/ganak_total}"
LOGDIR="${5:-$HOME/results/minimal/logs/ganak_total}"
WHITELIST="${6:-}"

mkdir -p "$OUTDIR" "$LOGDIR"

echo "CSV   : $CSV"
echo "SIZE  : $SIZE_FILTER"
echo "OUTDIR: $OUTDIR"
echo "LOGDIR: $LOGDIR"

if [[ ! -x "$GANAK_BIN" ]]; then
  echo "ganak binary not found or not executable: $GANAK_BIN" >&2
  exit 1
fi

iter_benchmarks "$CSV" "$SIZE_FILTER" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
do
  size_class="$(echo "$size_class" | tr -d '\r' | xargs)"

  if [[ "$SIZE_FILTER" != "all" && "$size_class" != "$SIZE_FILTER" ]]; then
    continue
  fi

  if [[ "$format" != "cnf" ]]; then
    echo "Skip $name: format is not cnf -> $format"
    continue
  fi

  if [[ ! -f "$input_path" ]]; then
    echo "Skip $name: input not found -> $input_path"
    continue
  fi

  if [[ -n "$WHITELIST" && -f "$WHITELIST" ]]; then
    if ! grep -Fxq "$name" "$WHITELIST"; then
      continue
    fi
  fi

  out_file="$OUTDIR/${name}.out"
  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  echo "========== ganak: $name =========="
  echo "Input : $input_path"
  echo "Output: $out_file"

  /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
    "$GANAK_BIN" "$input_path" > "$out_file" 2> "$log_file" || {
      echo "FAILED: $name"
      rm -f "$out_file"
      continue
    }

  echo "DONE: $name"
done