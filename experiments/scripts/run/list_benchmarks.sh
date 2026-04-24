set -euo pipefail

CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
SIZE_FILTER="${2:-all}"   # all / small / large

if [[ ! -f "$CSV" ]]; then
  echo "CSV not found: $CSV" >&2
  exit 1
fi

tail -n +2 "$CSV" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
do
  if [[ "$SIZE_FILTER" != "all" && "$size_class" != "$SIZE_FILTER" ]]; then
    continue
  fi

  echo "$name,$input_path,$n_vars,$n_clauses,$size_class"
done