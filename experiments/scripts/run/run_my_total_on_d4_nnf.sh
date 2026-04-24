set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"
DDNNIFE_BIN="${3:-/home/zansin/my/target/debug/ddnnife}"
INDIR="${4:-$HOME/benchmarks/ddnnf_d4}"
OUTDIR="${5:-$HOME/results/minimal/timing/ddnnife_total_on_d4}"
LOGDIR="${6:-$HOME/results/minimal/logs/ddnnife_total_on_d4}"
WHITELIST="${7:-}"

mkdir -p "$OUTDIR" "$LOGDIR"

echo "CSV   : $CSV"
echo "SIZE  : $SIZE_FILTER"
echo "OUTDIR: $OUTDIR"
echo "LOGDIR: $LOGDIR"

if [[ ! -x "$DDNNIFE_BIN" ]]; then
  echo "ddnnife binary not found or not executable: $DDNNIFE_BIN" >&2
  exit 1
fi

iter_benchmarks "$CSV" "$SIZE_FILTER" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
do
  size_class="$(echo "$size_class" | tr -d '\r' | xargs)"
  total_features="$(echo "$total_features" | tr -d '\r' | xargs)"

  if [[ "$SIZE_FILTER" != "all" && "$size_class" != "$SIZE_FILTER" ]]; then
    continue
  fi

  in_file="$INDIR/${name}.nnf"
  workdir="$OUTDIR/$name"
  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  if [[ ! -f "$in_file" ]]; then
    echo "Skip $name: input not found -> $in_file"
    continue
  fi

  if [[ -z "$total_features" ]]; then
    echo "Skip $name: total_features is empty"
    continue
  fi

  if [[ -n "$WHITELIST" && -f "$WHITELIST" ]]; then
    if ! grep -Fxq "$name" "$WHITELIST"; then
      continue
    fi
  fi

  mkdir -p "$workdir"

  echo "========== ddnnife on d4 nnf: $name =========="
  echo "Input         : $in_file"
  echo "Total features: $total_features"
  echo "Outdir        : $workdir"

  pushd "$workdir" >/dev/null
  /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
    "$DDNNIFE_BIN" "$in_file" -t "$total_features" \
    > "$log_file" 2>&1 || {
      echo "FAILED: $name"
      popd >/dev/null
      continue
    }
  popd >/dev/null

  echo "DONE: $name"
done