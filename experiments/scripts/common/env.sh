
#!/usr/bin/env bash
export PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
export PYTHONPATH="${PROJECT_ROOT}:${PYTHONPATH:-}"
export D4_BIN="$PROJECT_ROOT/tools/d4/d4"
export MY_DDNNIFE_BIN="$PROJECT_ROOT/my/target/release/ddnnife"
export PAPER_DDNNIFE_BIN="$PROJECT_ROOT/tools/ddnnf-reasoner-0.7.0/target/release/ddnnife"
export SHARPSAT_BIN="$PROJECT_ROOT/tools/sharpSAT/build/sharpSAT"
export QUERY_DDNNF_BIN="$PROJECT_ROOT/tools/query-ddnnf/query-dnnf-0.4.180625/src/query-dnnf"
export LD_LIBRARY_PATH="$PROJECT_ROOT/tools/ganak/build/lib:$PROJECT_ROOT/tools/ganak/build/_deps:$PROJECT_ROOT/tools/ganak/build/_deps/arjun-build/lib:$PROJECT_ROOT/tools/ganak/build/_deps/approxmc-build/lib:$PROJECT_ROOT/tools/ganak/build/_deps/cryptominisat5-build/lib:$PROJECT_ROOT/tools/ganak/build/_deps/sbva-build/lib:$PROJECT_ROOT/tools/ganak/build/_deps/treedecomp-build/lib:${LD_LIBRARY_PATH:-}"
