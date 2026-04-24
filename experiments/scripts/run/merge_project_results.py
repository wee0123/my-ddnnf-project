import csv
from pathlib import Path

BASE = Path.home()

FILES = {
    "bench": BASE / "benchmarks/meta/bench_list.csv",
    "ganak_total": BASE / "results/minimal/summary/ganak_total_summary.csv",
    "d4_compile": BASE / "results/minimal/summary/d4_compile_summary.csv",
    "ddnnife_total": BASE / "results/minimal/summary/ddnnife_total_summary.csv",
    "reuse_query": BASE / "results/minimal/summary/reuse_query_summary.csv",
}

OUT_CSV = BASE / "results/minimal/timing/project_experiment_summary.csv"

def read_csv_by_name(path: Path):
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        return {row["name"]: row for row in reader}

def main():
    tables = {k: read_csv_by_name(v) for k, v in FILES.items()}
    bench = tables["bench"]
    ganak = tables["ganak"]
    ddnnife_cnf = tables["ddnnife_from_cnf_features"]
    ddnnife_d4 = tables["ddnnife_on_d4_nnf_features"]

    fieldnames = [
        "name", "input_path", "format", "n_vars", "n_clauses", "total_features", "size_class",

        "ganak_count", "ganak_real_s", "ganak_mem_kb",

        "d4_compile_real_s", "d4_compile_mem_kb", "nnf_exists",

        "ddnnife_total_count", "ddnnife_total_real_s", "ddnnife_total_mem_kb",

        "count_match_total",

        "reuse_feature_queries",
        "reuse_pc_queries",
        "ganak_reuse_total_s",
        "ddnnife_reuse_total_s",
        "reuse_speedup",
    ]

    rows = []
    for name, b in bench.items():
        g = ganak.get(name, {})
        c = ddnnife_cnf.get(name, {})
        d4 = ddnnife_d4.get(name, {})

        row = {
            "name": name,
            "input_path": b.get("input_path", ""),
            "format": b.get("format", ""),
            "n_vars": b.get("n_vars", ""),
            "n_clauses": b.get("n_clauses", ""),
            "total_features": b.get("total_features", ""),
            "size_class": b.get("size_class", ""),

            "ganak_count": g.get("ganak_count", ""),
            "ganak_real_s": g.get("real_s", ""),
            "ganak_user_s": g.get("user_s", ""),
            "ganak_sys_s": g.get("sys_s", ""),
            "ganak_mem_kb": g.get("mem_kb", ""),

            "ddnnife_from_cnf_features_real_s": c.get("ddnnife_from_cnf_features_real_s", ""),
            "ddnnife_from_cnf_features_user_s": c.get("ddnnife_from_cnf_features_user_s", ""),
            "ddnnife_from_cnf_features_sys_s": c.get("ddnnife_from_cnf_features_sys_s", ""),
            "ddnnife_from_cnf_features_mem_kb": c.get("ddnnife_from_cnf_features_mem_kb", ""),

            "ddnnife_on_d4_nnf_features_real_s": d4.get("ddnnife_on_d4_nnf_features_real_s", ""),
            "ddnnife_on_d4_nnf_features_user_s": d4.get("ddnnife_on_d4_nnf_features_user_s", ""),
            "ddnnife_on_d4_nnf_features_sys_s": d4.get("ddnnife_on_d4_nnf_features_sys_s", ""),
            "ddnnife_on_d4_nnf_features_mem_kb": d4.get("ddnnife_on_d4_nnf_features_mem_kb", ""),
        }

        rows.append(row)

    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with OUT_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Generated: {OUT_CSV}")
    print(f"Rows: {len(rows)}")

if __name__ == "__main__":
    main()