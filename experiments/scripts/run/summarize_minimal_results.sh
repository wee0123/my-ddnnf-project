#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-$HOME/results/minimal}"
OUTDIR="${2:-$BASE/summary}"

mkdir -p "$OUTDIR"

TOTAL_CSV="$OUTDIR/total_summary.csv"
REUSE_CSV="$OUTDIR/reuse_summary.csv"

echo "method,model,real_s,user_s,sys_s,mem_kb,status" > "$TOTAL_CSV"
echo "method,model,queries_ok,queries_failed,total_real_s,avg_real_s,status" > "$REUSE_CSV"

extract_kv() {
  local file="$1"
  local key="$2"
  awk -F= -v k="$key" '$1==k{print $2; exit}' "$file" 2>/dev/null || true
}

# -------------------------------------------------
# 1) 全模型计数：ganak_total
# -------------------------------------------------
if [[ -d "$BASE/logs/ganak_total" ]]; then
  while IFS= read -r -d '' f; do
    model="$(basename "$f" .time)"
    real_s="$(extract_kv "$f" real)"
    user_s="$(extract_kv "$f" user)"
    sys_s="$(extract_kv "$f" sys)"
    mem_kb="$(extract_kv "$f" mem_kb)"
    echo "ganak_total,$model,$real_s,$user_s,$sys_s,$mem_kb,OK" >> "$TOTAL_CSV"
  done < <(find "$BASE/logs/ganak_total" -maxdepth 1 -type f -name "*.time" -print0 | sort -z)
fi

# -------------------------------------------------
# 2) 全模型计数：ddnnife_total_on_d4
# -------------------------------------------------
if [[ -d "$BASE/logs/ddnnife_total_on_d4" ]]; then
  while IFS= read -r -d '' f; do
    model="$(basename "$f" .time)"
    real_s="$(extract_kv "$f" real)"
    user_s="$(extract_kv "$f" user)"
    sys_s="$(extract_kv "$f" sys)"
    mem_kb="$(extract_kv "$f" mem_kb)"
    echo "ddnnife_total_on_d4,$model,$real_s,$user_s,$sys_s,$mem_kb,OK" >> "$TOTAL_CSV"
  done < <(find "$BASE/logs/ddnnife_total_on_d4" -maxdepth 1 -type f -name "*.time" -print0 | sort -z)
fi

# -------------------------------------------------
# 3) 重复查询：ganak_reuse_queries
#    注意：真实文件在 logs/ganak_reuse_queries/<model>/<model>.total.time
# -------------------------------------------------
if [[ -d "$BASE/logs/ganak_reuse_queries" ]]; then
  while IFS= read -r -d '' f; do
    model="$(basename "$f" .total.time)"
    queries_ok="$(extract_kv "$f" queries_ok)"
    queries_failed="$(extract_kv "$f" queries_failed)"
    total_real_s="$(extract_kv "$f" total_real_s)"

    avg_real_s="$(python3 - <<PY
ok = float("${queries_ok:-0}" or 0)
tot = float("${total_real_s:-0}" or 0)
print(tot / ok if ok > 0 else "")
PY
)"
    echo "ganak_reuse_queries,$model,$queries_ok,$queries_failed,$total_real_s,$avg_real_s,OK" >> "$REUSE_CSV"
  done < <(find "$BASE/logs/ganak_reuse_queries" -type f -name "*.total.time" -print0 | sort -z)
fi

# -------------------------------------------------
# 4) 重复查询：ddnnife_reuse_queries
#    文件仍在顶层 logs/ddnnife_reuse_queries/*.time
# -------------------------------------------------
if [[ -d "$BASE/logs/ddnnife_reuse_queries" ]]; then
  while IFS= read -r -d '' f; do
    model="$(basename "$f" .time)"
    real_s="$(extract_kv "$f" real)"
    echo "ddnnife_reuse_queries,$model,,,$real_s,,OK" >> "$REUSE_CSV"
  done < <(find "$BASE/logs/ddnnife_reuse_queries" -maxdepth 1 -type f -name "*.time" -print0 | sort -z)
fi

echo "Done."
echo "Total summary : $TOTAL_CSV"
echo "Reuse summary : $REUSE_CSV"
