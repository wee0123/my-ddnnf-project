import csv
from pathlib import Path

BASE = Path.home()
BENCH_CSV = BASE / "benchmarks/meta/bench_list.csv"
GANAK_CSV = BASE / "results/timing/ganak_total_summary.csv"
DDNNIFE_CNF_CSV = BASE / "results/timing/ddnnife_from_cnf_features_summary.csv"
DDNNIFE_D4_CSV = BASE / "results/timing/ddnnife_on_d4_nnf_features_summary.csv"
OUT_CSV = BASE / "results/timing/total_experiment_summary.csv"

def read_csv_by_name(path: Path):
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        return {row["name"]: row for row in reader}

def main():
    bench = read_csv_by_name(BENCH_CSV)
    ganak = read_csv_by_name(GANAK_CSV)
    ddnnife_cnf = read_csv_by_name(DDNNIFE_CNF_CSV)
    ddnnife_d4 = read_csv_by_name(DDNNIFE_D4_CSV)

    fieldnames = [
        "name",
        "input_path",
        "format",
        "n_vars",
        "n_clauses",
        "total_features",
        "size_class",

        "ganak_count",
        "ganak_real_s",
        "ganak_user_s",
        "ganak_sys_s",
        "ganak_mem_kb",

        "ddnnife_from_cnf_features_real_s",
        "ddnnife_from_cnf_features_user_s",
        "ddnnife_from_cnf_features_sys_s",
        "ddnnife_from_cnf_features_mem_kb",

        "ddnnife_on_d4_nnf_features_real_s",
        "ddnnife_on_d4_nnf_features_user_s",
        "ddnnife_on_d4_nnf_features_sys_s",
        "ddnnife_on_d4_nnf_features_mem_kb",
    ]

    rows = []
    for name, b in bench.items():
        row = {
            "name": name,
            "input_path": b.get("input_path", ""),
            "format": b.get("format", ""),
            "n_vars": b.get("n_vars", ""),
            "n_clauses": b.get("n_clauses", ""),
            "total_features": b.get("total_features", ""),
            "size_class": b.get("size_class", ""),

            "ganak_count": ganak.get(name, {}).get("ganak_count", ""),
            "ganak_real_s": ganak.get(name, {}).get("real_s", ""),
            "ganak_user_s": ganak.get(name, {}).get("user_s", ""),
            "ganak_sys_s": ganak.get(name, {}).get("sys_s", ""),
            "ganak_mem_kb": ganak.get(name, {}).get("mem_kb", ""),

            "ddnnife_from_cnf_features_real_s": ddnnife_cnf.get(name, {}).get("ddnnife_from_cnf_features_real_s", ""),
            "ddnnife_from_cnf_features_user_s": ddnnife_cnf.get(name, {}).get("ddnnife_from_cnf_features_user_s", ""),
            "ddnnife_from_cnf_features_sys_s": ddnnife_cnf.get(name, {}).get("ddnnife_from_cnf_features_sys_s", ""),
            "ddnnife_from_cnf_features_mem_kb": ddnnife_cnf.get(name, {}).get("ddnnife_from_cnf_features_mem_kb", ""),

            "ddnnife_on_d4_nnf_features_real_s": ddnnife_d4.get(name, {}).get("ddnnife_on_d4_nnf_features_real_s", ""),
            "ddnnife_on_d4_nnf_features_user_s": ddnnife_d4.get(name, {}).get("ddnnife_on_d4_nnf_features_user_s", ""),
            "ddnnife_on_d4_nnf_features_sys_s": ddnnife_d4.get(name, {}).get("ddnnife_on_d4_nnf_features_sys_s", ""),
            "ddnnife_on_d4_nnf_features_mem_kb": ddnnife_d4.get(name, {}).get("ddnnife_on_d4_nnf_features_mem_kb", ""),
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