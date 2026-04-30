
#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="${1:-$(pwd)}"
OUT="${PROJECT_ROOT}/results/merged/summary/machine_info.txt"
mkdir -p "$(dirname "$OUT")"
{
  echo "date: $(date -Iseconds)"
  echo "hostname: $(hostname)"
  echo "uname: $(uname -a)"
  echo
  echo "[lscpu]"
  lscpu || true
  echo
  echo "[meminfo]"
  free -h || true
} > "$OUT"
echo "[OK] wrote $OUT"
