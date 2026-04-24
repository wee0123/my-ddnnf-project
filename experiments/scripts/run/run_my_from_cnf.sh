set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"   # all / small / large
DDNNIFE_BIN="${3:-/home/zansin/my/target/debug/ddnnife}"
OUTDIR="${4:-$HOME/results/ddnnife_from_cnf_features}"
LOGDIR="${5:-$HOME/results/logs/ddnnife_from_cnf_features}"

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

  workdir="$OUTDIR/$name"
  mkdir -p "$workdir"

  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  echo "========== ddnnife from cnf: $name =========="
  echo "Input : $input_path"
  echo "Outdir: $workdir"

  pushd "$workdir" >/dev/null
  /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
    "$DDNNIFE_BIN" "$input_path" -c > "$log_file" 2>&1 || {
      echo "FAILED: $name"
      popd >/dev/null
      continue
    }
  popd >/dev/null

  echo "DONE: $name"
done