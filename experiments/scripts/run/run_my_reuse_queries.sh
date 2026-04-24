#!/usr/bin/env bash
set -euo pipefail

source "$HOME/experiments/scripts/common/bench_utils.sh"

# =========================
# 参数
# =========================
CSV="${1:-$HOME/benchmarks/meta/bench_list.csv}"
MODEL_LIST="${2:-$HOME/benchmarks/meta/selected_models.txt}"
DDNNIFE_BIN="${3:-/home/zansin/my/target/debug/ddnnife}"
INDIR="${4:-$HOME/benchmarks/ddnnf_d4}"
QUERYDIR="${5:-$HOME/results/minimal/query_sets}"
OUTDIR="${6:-$HOME/results/minimal/timing/ddnnife_reuse_queries}"
LOGDIR="${7:-$HOME/results/minimal/logs/ddnnife_reuse_queries}"

mkdir -p "$OUTDIR" "$LOGDIR"

echo "CSV       : $CSV"
echo "MODEL_LIST: $MODEL_LIST"
echo "DDNNIFE   : $DDNNIFE_BIN"
echo "INDIR     : $INDIR"
echo "QUERYDIR  : $QUERYDIR"
echo "OUTDIR    : $OUTDIR"
echo "LOGDIR    : $LOGDIR"

# =========================
# 基础检查
# =========================
if [[ ! -f "$CSV" ]]; then
  echo "bench csv not found: $CSV" >&2
  exit 1
fi

if [[ ! -f "$MODEL_LIST" ]]; then
  echo "model list not found: $MODEL_LIST" >&2
  exit 1
fi

if [[ ! -x "$DDNNIFE_BIN" ]]; then
  echo "ddnnife binary not found or not executable: $DDNNIFE_BIN" >&2
  exit 1
fi

if [[ ! -d "$QUERYDIR" ]]; then
  echo "query dir not found: $QUERYDIR" >&2
  exit 1
fi

# =========================
# 辅助函数：按名字从 bench_list.csv 取一行
# 输出整行 csv
# =========================
get_bench_row_by_name() {
  local target_name="$1"
  awk -F',' -v n="$target_name" '
    NR==1 { next }   # skip header
    $1 == n { print; found=1; exit }
    END { if (!found) exit 1 }
  ' "$CSV"
}

# =========================
# 可选：检查 ddnnife 是否支持 stream
# =========================
echo "Checking ddnnife help..."
set +e
HELP_TEXT="$("$DDNNIFE_BIN" -h 2>&1)"
HELP_STATUS=$?
set -e

if [[ $HELP_STATUS -ne 0 ]]; then
  echo "WARNING: unable to read ddnnife help, continue anyway"
else
  echo "ddnnife help captured"
  if ! echo "$HELP_TEXT" | grep -qi "stream"; then
    echo "WARNING: 'stream' not found in ddnnife help output"
  fi
fi

# =========================
# 主循环：模型列表使用 fd 3，避免被子进程吞掉
# =========================
exec 3< "$MODEL_LIST"

while IFS= read -r name <&3 || [[ -n "$name" ]]; do
  name="$(echo "$name" | tr -d '\r' | xargs)"

  # 跳过空行和注释
  if [[ -z "$name" || "${name:0:1}" == "#" ]]; then
    continue
  fi

  echo
  echo "============================================================"
  echo "MODEL: $name"

  # 从 bench_list.csv 查元信息
  if ! row="$(get_bench_row_by_name "$name")"; then
    echo "Skip $name: not found in bench csv"
    continue
  fi

  IFS=',' read -r bench_name input_path format n_vars n_clauses total_features size_class <<< "$row"

  size_class="$(echo "$size_class" | tr -d '\r' | xargs)"
  total_features="$(echo "$total_features" | tr -d '\r' | xargs)"

  in_file="$INDIR/${name}.nnf"
  query_file="$QUERYDIR/${name}_queries.stream"
  out_file="$OUTDIR/${name}.out"
  log_file="$LOGDIR/${name}.log"
  time_file="$LOGDIR/${name}.time"

  if [[ ! -f "$in_file" ]]; then
    echo "Skip $name: nnf not found -> $in_file"
    continue
  fi

  if [[ ! -f "$query_file" ]]; then
    echo "Skip $name: query file not found -> $query_file"
    continue
  fi

  if [[ -z "$total_features" ]]; then
    echo "Skip $name: total_features is empty"
    continue
  fi

  echo "NNF           : $in_file"
  echo "Query file    : $query_file"
  echo "Total features: $total_features"
  echo "Output        : $out_file"

  # 可选：简单检查查询文件里是否包含 exit
  if ! grep -Eq '^[[:space:]]*exit[[:space:]]*$' "$query_file"; then
    echo "WARNING: query file does not contain 'exit' -> $query_file"
  fi

  # README 对应做法：
  #   ddnnife model.nnf -t TOTAL stream
  # 然后通过 stdin 输入 stream API 命令
  if /usr/bin/time -f "real=%e\nuser=%U\nsys=%S\nmem_kb=%M" -o "$time_file" \
      bash -c 'cat "$1" | "$2" "$3" -t "$4" stream' _ \
      "$query_file" "$DDNNIFE_BIN" "$in_file" "$total_features" \
      > "$out_file" 2> "$log_file"; then
    echo "DONE: $name"
  else
    echo "FAILED (stream mode): $name"
    echo "Check: $log_file"
    rm -f "$out_file"
    continue
  fi
done

exec 3<&-

echo
echo "All requested models processed."