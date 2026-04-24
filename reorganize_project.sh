#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-$(pwd)}"

echo "[INFO] Project root: $ROOT"
cd "$ROOT"

echo "[STEP 1] Create target directories..."
mkdir -p benchmarks/cnf
mkdir -p benchmarks/meta
mkdir -p compiled/{d4_nnf,dsharp_nnf,c2d_nnf,query_sets}
mkdir -p experiments/scripts/{parse,merge}
mkdir -p experiments/{configs,notes}
mkdir -p results/{total_count,repeated_queries,merged,figures,archive}
mkdir -p logs/{compile,run,debug}
mkdir -p logs/compile/{d4,dsharp,others}
mkdir -p logs/run/{countAntom,ganak,my_ddnnife,paper_ddnnife,query_ddnnf,d4_query}
mkdir -p logs/debug/{my,paper_ddnnife}
mkdir -p tmp/{failed_cases,intermediate,cache}

echo "[STEP 2] Move top-level CNF files from benchmarks/ to benchmarks/cnf/ ..."
if [ -d benchmarks ]; then
  find benchmarks -maxdepth 1 -type f -name "*.cnf" -print -exec mv {} benchmarks/cnf/ \;
fi

echo "[STEP 3] Move d4-generated NNF files out of benchmarks/ddnnf_d4 ..."
if [ -d benchmarks/ddnnf_d4 ]; then
  # 若 compiled/d4_nnf 为空，则直接整体迁移
  if [ -z "$(find compiled/d4_nnf -mindepth 1 -print -quit 2>/dev/null)" ]; then
    rm -rf compiled/d4_nnf
    mv benchmarks/ddnnf_d4 compiled/d4_nnf
  else
    find benchmarks/ddnnf_d4 -type f -name "*.nnf" -print -exec mv {} compiled/d4_nnf/ \;
    rmdir benchmarks/ddnnf_d4 2>/dev/null || true
  fi
fi

echo "[STEP 4] Keep benchmarks/meta as-is, just ensure it exists..."
mkdir -p benchmarks/meta

echo "[STEP 5] Archive old run-based result folders..."
for d in run1 run2 run3; do
  if [ -d "results/$d" ]; then
    mv "results/$d" "results/archive/"
  fi
done

echo "[STEP 6] Create clearer experiment result buckets..."
mkdir -p results/total_count/{ganak,countAntom,my_ddnnife,paper_ddnnife,d4_query,query_ddnnf}
mkdir -p results/repeated_queries/{ganak,my_ddnnife,paper_ddnnife,d4_query,query_ddnnf}
mkdir -p results/merged/{csv,summary}
mkdir -p results/figures

echo "[STEP 7] Move existing my/ debug artifacts to logs/debug/my ..."
if [ -d my ]; then
  [ -f my/build.log ] && mv my/build.log logs/debug/my/
  [ -f my/test_output.txt ] && mv my/test_output.txt logs/debug/my/
fi

echo "[STEP 8] Move paper baseline debug artifacts to logs/debug/paper_ddnnife ..."
if [ -d tools/ddnnf-reasoner-0.7.0 ]; then
  [ -f tools/ddnnf-reasoner-0.7.0/build.log ] && mv tools/ddnnf-reasoner-0.7.0/build.log logs/debug/paper_ddnnife/
  [ -f tools/ddnnf-reasoner-0.7.0/test_output.txt ] && mv tools/ddnnf-reasoner-0.7.0/test_output.txt logs/debug/paper_ddnnife/
fi

echo "[STEP 9] Add local notes to distinguish my/ and paper baseline ..."
if [ -d my ]; then
  cat > my/LOCAL_ROLE.md <<'EOF'
# my/
This is the primary development directory for my optimized ddnnife-based solution.

Rules:
- Continue code development here.
- Run cargo build / cargo test here.
- New optimization experiments should be based on this directory.
- Do not treat this directory as a general results dump.
EOF
fi

if [ -d tools/ddnnf-reasoner-0.7.0 ]; then
  cat > tools/ddnnf-reasoner-0.7.0/LOCAL_ROLE.md <<'EOF'
# tools/ddnnf-reasoner-0.7.0/
This directory stores the original paper tool as baseline reference.

Rules:
- Keep source code here for comparison.
- Do not continue main development here.
- Use ../../my as the primary optimized implementation.
EOF
fi

echo "[STEP 10] Prepare common environment file for scripts ..."
mkdir -p experiments/scripts/common
cat > experiments/scripts/common/env.sh <<'EOF'
#!/usr/bin/env bash

export PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

export BENCH_DIR="$PROJECT_ROOT/benchmarks"
export BENCH_CNF_DIR="$PROJECT_ROOT/benchmarks/cnf"
export META_DIR="$PROJECT_ROOT/benchmarks/meta"

export COMPILED_DIR="$PROJECT_ROOT/compiled"
export D4_NNF_DIR="$PROJECT_ROOT/compiled/d4_nnf"
export DSHARP_NNF_DIR="$PROJECT_ROOT/compiled/dsharp_nnf"
export C2D_NNF_DIR="$PROJECT_ROOT/compiled/c2d_nnf"
export QUERY_SET_DIR="$PROJECT_ROOT/compiled/query_sets"

export TOOLS_DIR="$PROJECT_ROOT/tools"
export MY_DIR="$PROJECT_ROOT/my"
export PAPER_DDNNIFE_DIR="$PROJECT_ROOT/tools/ddnnf-reasoner-0.7.0"

export RESULTS_DIR="$PROJECT_ROOT/results"
export LOGS_DIR="$PROJECT_ROOT/logs"
export TMP_DIR="$PROJECT_ROOT/tmp"

export D4_BIN="$TOOLS_DIR/d4/d4"
export GANAK_BIN="$TOOLS_DIR/ganak/build/ganak"
export SHARPSAT_BIN="$TOOLS_DIR/sharpSAT/build/sharpSAT"
export DSHARP_BIN="$TOOLS_DIR/dsharp/dsharp"

export MY_DDNNIFE_BIN="$MY_DIR/target/release/ddnnife"
export PAPER_DDNNIFE_BIN="$PAPER_DDNNIFE_DIR/target/release/ddnnife"
EOF
chmod +x experiments/scripts/common/env.sh

echo "[STEP 11] Create a result layout README ..."
cat > results/README_LAYOUT.md <<'EOF'
# Results layout

## total_count/
Stores outputs for whole-model counting experiments.

## repeated_queries/
Stores outputs for repeated-query / reuse-query experiments.

## merged/
Stores merged CSV and summary files.

## figures/
Stores plots for papers and reports.

## archive/
Stores old run-based folders such as run1/run2/run3.
These are preserved for traceability but should not be used as the main structure anymore.
EOF

echo "[STEP 12] Create a benchmark layout README ..."
cat > benchmarks/README_LAYOUT.md <<'EOF'
# Benchmarks layout

## cnf/
Original CNF benchmark inputs.

## meta/
Benchmark metadata such as:
- bench_list.csv
- bench_list_small.csv
- bench_list_large.csv
- selected_models.txt

Do not place compiled .nnf artifacts here.
EOF

echo "[STEP 13] Create a compiled layout README ..."
cat > compiled/README_LAYOUT.md <<'EOF'
# Compiled artifacts layout

## d4_nnf/
NNF files compiled by d4.

## dsharp_nnf/
NNF files compiled by dsharp.

## c2d_nnf/
NNF files compiled by c2d.

## query_sets/
Generated repeated-query sets and condition files.
EOF

echo "[STEP 14] Optional: move existing minimal query_sets into compiled/query_sets ..."
if [ -d results/minimal/query_sets ]; then
  find results/minimal/query_sets -type f -print -exec cp {} compiled/query_sets/ \; || true
fi

echo "[STEP 15] Show final important directories ..."
echo
echo "=== benchmarks ==="
find benchmarks -maxdepth 2 -type d | sort
echo
echo "=== compiled ==="
find compiled -maxdepth 2 -type d | sort
echo
echo "=== results ==="
find results -maxdepth 2 -type d | sort
echo
echo "=== logs ==="
find logs -maxdepth 2 -type d | sort
echo
echo "[DONE] Reorganization completed."