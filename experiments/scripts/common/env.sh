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
