set -euo pipefail

trim_field() {
  local s="$1"
  s="${s//$'\r'/}"   # 去掉 CR
  s="$(echo "$s" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  printf '%s' "$s"
}

iter_benchmarks() {
  local csv="$1"
  local size_filter="${2:-all}"   # all / small / large

  if [[ ! -f "$csv" ]]; then
    echo "CSV not found: $csv" >&2
    return 1
  fi

  tail -n +2 "$csv" | while IFS=, read -r name input_path format n_vars n_clauses total_features size_class
  do
    name="$(trim_field "$name")"
    input_path="$(trim_field "$input_path")"
    format="$(trim_field "$format")"
    n_vars="$(trim_field "$n_vars")"
    n_clauses="$(trim_field "$n_clauses")"
    total_features="$(trim_field "$total_features")"
    size_class="$(trim_field "$size_class")"

    if [[ "$size_filter" != "all" && "$size_class" != "$size_filter" ]]; then
      continue
    fi

    echo "$name,$input_path,$format,$n_vars,$n_clauses,$total_features,$size_class"
  done
}