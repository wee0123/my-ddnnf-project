#!/usr/bin/env bash
set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

# =========================================================
# 参数
# =========================================================
CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
MODEL_LIST="${2:-$HOME/benchmarks/meta/selected_models.txt}"
GANAK_BIN="${3:-$HOME/tools/ganak/build/ganak}"
QUERYDIR="${4:-$HOME/results/minimal/query_sets}"
OUTDIR="${5:-$HOME/results/minimal/timing/ganak_reuse_queries}"
LOGDIR="${6:-$HOME/results/minimal/logs/ganak_reuse_queries}"
TMPDIR="${7:-$HOME/results/minimal/tmp/ganak_reuse_queries}"

mkdir -p "$OUTDIR" "$LOGDIR" "$TMPDIR"

echo "CSV       : $CSV"
echo "MODEL_LIST: $MODEL_LIST"
echo "GANAK_BIN : $GANAK_BIN"
echo "QUERYDIR  : $QUERYDIR"
echo "OUTDIR    : $OUTDIR"
echo "LOGDIR    : $LOGDIR"
echo "TMPDIR    : $TMPDIR"

# =========================================================
# 基础检查
# =========================================================
if [[ ! -f "$CSV" ]]; then
  echo "bench csv not found: $CSV" >&2
  exit 1
fi

if [[ ! -f "$MODEL_LIST" ]]; then
  echo "model list not found: $MODEL_LIST" >&2
  exit 1
fi

if [[ ! -x "$GANAK_BIN" ]]; then
  echo "ganak binary not found or not executable: $GANAK_BIN" >&2
  exit 1
fi

if [[ ! -d "$QUERYDIR" ]]; then
  echo "query dir not found: $QUERYDIR" >&2
  exit 1
fi

# =========================================================
# 工具函数
# =========================================================

# 按模型名从 bench_list.csv 取一行
get_bench_row_by_name() {
  local target_name="$1"
  awk -F',' -v n="$target_name" '
    NR==1 { next }  # skip header
    $1 == n { print; found=1; exit }
    END { if (!found) exit 1 }
  ' "$CSV"
}

# 解析查询行，输出 literals（空格分隔）
# 支持:
#   f 3
#   c 1 -2 5
parse_query_literals() {
  local line="$1"

  # 去掉首尾空白
  line="$(echo "$line" | xargs)"

  # 空行 / 注释
  [[ -z "$line" ]] && return 1
  [[ "${line:0:1}" == "#" ]] && return 1

  local kind
  kind="$(echo "$line" | awk '{print $1}')"

  if [[ "$kind" == "f" ]]; then
    local var
    var="$(echo "$line" | awk '{print $2}')"
    if [[ -z "${var:-}" ]]; then
      echo "Invalid feature query: $line" >&2
      return 2
    fi
    echo "$var"
    return 0
  elif [[ "$kind" == "c" ]]; then
    local lits
    lits="$(echo "$line" | cut -d' ' -f2- | xargs)"
    if [[ -z "${lits:-}" ]]; then
      echo "Invalid config query: $line" >&2
      return 2
    fi
    echo "$lits"
    return 0
  else
    echo "Unsupported query type in line: $line" >&2
    return 2
  fi
}

# 从原始 CNF 生成一个附加单位子句后的临时 CNF
# 参数:
#   $1 原始 cnf
#   $2 输出 cnf
#   $3 n_vars
#   $4 add_clauses_count
#   $5 literals (空格分隔)
build_constrained_cnf() {
  local base_cnf="$1"
  local out_cnf="$2"
  local n_vars="$3"
  local add_count="$4"
  local literals="$5"

  # 先从原文件中提取原始 clause 数
  local old_clauses
  old_clauses="$(awk '
    /^p[[:space:]]+cnf[[:space:]]+/ { print $4; exit }
  ' "$base_cnf")"

  if [[ -z "${old_clauses:-}" ]]; then
    echo "Failed to parse p cnf header from: $base_cnf" >&2
    return 1
  fi

  local new_clauses=$((old_clauses + add_count))

  # 重写 header，保留其余内容
  awk -v nv="$n_vars" -v nc="$new_clauses" '
    BEGIN { replaced=0 }
    /^p[[:space:]]+cnf[[:space:]]+/ && replaced==0 {
      print "p cnf " nv " " nc
      replaced=1
      next
    }
    { print }
  ' "$base_cnf" > "$out_cnf"

  # 附加单位子句
  local lit
  for lit in $literals; do
    echo "$lit 0" >> "$out_cnf"
  done
}

# 从 time 文件提取 real
parse_real_time() {
  local time_file="$1"
  awk -F'=' '
    $1=="real" { print $2; exit }
  ' "$time_file"
}

# 兼容你现有 parse_ganak_results.py 的模式，尽量让 out 可解析
# 本函数目前不做解析，只是后续汇总时可直接抓最后一列
extract_count_from_out() {
  local out_file="$1"
  python3 - "$out_file" <<'PY'
import re, sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8", errors="ignore")

patterns = [
    r'Number of solutions.*?([0-9]+)',
    r'c s exact arb int\s+([0-9]+)',
    r'^s\s+([0-9]+)$',
    r'^\s*([0-9]+)\s*$',
]

lines = text.splitlines()
for line in lines:
    for pat in patterns:
        m = re.search(pat, line)
        if m:
            print(m.group(1))
            raise SystemExit(0)

for line in reversed(lines):
    nums = re.findall(r'[0-9]+', line)
    if nums:
        print(nums[-1])
        raise SystemExit(0)

print("")
PY
}

# =========================================================
# 主循环
# =========================================================
exec 3< "$MODEL_LIST"

while IFS= read -r name <&3 || [[ -n "$name" ]]; do
  name="$(echo "$name" | tr -d '\r' | xargs)"

  if [[ -z "$name" || "${name:0:1}" == "#" ]]; then
    continue
  fi

  echo
  echo "============================================================"
  echo "MODEL: $name"

  if ! row="$(get_bench_row_by_name "$name")"; then
    echo "Skip $name: not found in bench csv"
    continue
  fi

  IFS=',' read -r bench_name input_path format n_vars n_clauses total_features size_class <<< "$row"
  size_class="$(echo "$size_class" | tr -d '\r' | xargs)"
  n_vars="$(echo "$n_vars" | tr -d '\r' | xargs)"
  n_clauses="$(echo "$n_clauses" | tr -d '\r' | xargs)"

  if [[ "$format" != "cnf" ]]; then
    echo "Skip $name: format is not cnf -> $format"
    continue
  fi

  if [[ ! -f "$input_path" ]]; then
    echo "Skip $name: input not found -> $input_path"
    continue
  fi

  query_file="$QUERYDIR/${name}_queries.txt"
  if [[ ! -f "$query_file" ]]; then
    echo "Skip $name: query file not found -> $query_file"
    continue
  fi

  model_out_dir="$OUTDIR/$name"
  model_log_dir="$LOGDIR/$name"
  model_tmp_dir="$TMPDIR/$name"
  mkdir -p "$model_out_dir" "$model_log_dir" "$model_tmp_dir"

  summary_csv="$model_out_dir/${name}_query_summary.csv"
  model_total_time_file="$model_log_dir/${name}.total.time"
  model_total_log_file="$model_log_dir/${name}.total.log"

  cat > "$summary_csv" <<'EOF'
query_id,query_type,query_text,literals,temp_cnf,ganak_count,real_s,user_s,sys_s,mem_kb,status
EOF

  total_real=0
  total_ok=0
  total_fail=0
  query_id=0

  exec 4< "$query_file"
  while IFS= read -r raw_line <&4 || [[ -n "$raw_line" ]]; do
    line="$(echo "$raw_line" | tr -d '\r' | xargs)"

    if [[ -z "$line" || "${line:0:1}" == "#" ]]; then
      continue
    fi

    query_id=$((query_id + 1))
    query_type="$(echo "$line" | awk '{print $1}')"

    if ! literals="$(parse_query_literals "$line")"; then
      echo "Skip invalid query [$query_id]: $line" | tee -a "$model_total_log_file"
      total_fail=$((total_fail + 1))
      continue
    fi

    add_count="$(wc -w <<< "$literals" | xargs)"
    tmp_cnf="$model_tmp_dir/query_${query_id}.cnf"
    out_file="$model_out_dir/query_${query_id}.out"
    log_file="$model_log_dir/query_${query_id}.log"
    time_file="$model_log_dir/query_${query_id}.time"

    build_constrained_cnf "$input_path" "$tmp_cnf" "$n_vars" "$add_count" "$literals"

    if /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
        "$GANAK_BIN" "$tmp_cnf" > "$out_file" 2> "$log_file" < /dev/null; then
      ganak_count="$(extract_count_from_out "$out_file")"
      real_s="$(awk -F= '$1=="real"{print $2}' "$time_file")"
      user_s="$(awk -F= '$1=="user"{print $2}' "$time_file")"
      sys_s="$(awk -F= '$1=="sys"{print $2}' "$time_file")"
      mem_kb="$(awk -F= '$1=="mem_kb"{print $2}' "$time_file")"

      total_real="$(python3 - <<PY
a=float("${total_real}")
b=float("${real_s}")
print(a+b)
PY
)"

      echo "${query_id},${query_type},\"${line}\",\"${literals}\",\"${tmp_cnf}\",${ganak_count},${real_s},${user_s},${sys_s},${mem_kb},OK" >> "$summary_csv"
      total_ok=$((total_ok + 1))
    else
      real_s="$(awk -F= '$1=="real"{print $2}' "$time_file" 2>/dev/null || true)"
      user_s="$(awk -F= '$1=="user"{print $2}' "$time_file" 2>/dev/null || true)"
      sys_s="$(awk -F= '$1=="sys"{print $2}' "$time_file" 2>/dev/null || true)"
      mem_kb="$(awk -F= '$1=="mem_kb"{print $2}' "$time_file" 2>/dev/null || true)"
      echo "${query_id},${query_type},\"${line}\",\"${literals}\",\"${tmp_cnf}\",,${real_s:-},${user_s:-},${sys_s:-},${mem_kb:-},FAILED" >> "$summary_csv"
      total_fail=$((total_fail + 1))
    fi
  done
  exec 4<&-

  {
    echo "model=$name"
    echo "queries_ok=$total_ok"
    echo "queries_failed=$total_fail"
    echo "total_real_s=$total_real"
  } > "$model_total_time_file"

done

exec 3<&-

echo
echo "All requested models processed."