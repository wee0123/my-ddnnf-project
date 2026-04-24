import csv
from pathlib import Path

BENCH_ROOT = Path.home() / "/home/zansin/benchmarks"
OUT_DIR = BENCH_ROOT / "meta"

OUT_ALL = OUT_DIR / "bench_list.csv"
OUT_SMALL = OUT_DIR / "bench_list_small.csv"
OUT_LARGE = OUT_DIR / "bench_list_large.csv"

THRESHOLD = 1000


def extract_cnf_info(path: Path):
    n_vars = None
    n_clauses = None
    max_var = 0
    clause_count = 0

    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("c"):
                continue

            if s.startswith("p"):
                parts = s.split()
                if len(parts) >= 4 and parts[1].lower() == "cnf":
                    try:
                        n_vars = int(parts[2])
                        n_clauses = int(parts[3])
                    except ValueError:
                        pass
                continue

            parts = s.split()
            ints = []
            ok = True
            for x in parts:
                try:
                    ints.append(int(x))
                except ValueError:
                    ok = False
                    break

            if ok and ints:
                clause_count += 1
                for v in ints:
                    if v != 0:
                        max_var = max(max_var, abs(v))

    if n_vars is None:
        n_vars = max_var
    if n_clauses is None:
        n_clauses = clause_count

    return n_vars, n_clauses


def classify(n_vars: int) -> str:
    return "large" if n_vars >= THRESHOLD else "small"


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    cnf_files = sorted(BENCH_ROOT.rglob("*.cnf"))

    rows = []
    for path in cnf_files:
        n_vars, n_clauses = extract_cnf_info(path)
        size = classify(n_vars)

        row = {
            "name": path.stem.replace(" ", "_"),
            "input_path": str(path.resolve()),
            "format": "cnf",
            "n_vars": n_vars,
            "n_clauses": n_clauses,
            "total_features": n_vars,
            "size_class": size,
        }
        rows.append(row)

    fields = [
        "name",
        "input_path",
        "format",
        "n_vars",
        "n_clauses",
        "total_features",
        "size_class",
    ]

    with OUT_ALL.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    small_rows = [r for r in rows if r["size_class"] == "small"]
    large_rows = [r for r in rows if r["size_class"] == "large"]

    with OUT_SMALL.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(small_rows)

    with OUT_LARGE.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(large_rows)

    print(f"Generated: {OUT_ALL}")
    print(f"Generated: {OUT_SMALL}")
    print(f"Generated: {OUT_LARGE}")
    print(f"Total CNF benchmarks: {len(rows)}")
    print(f"Small (<{THRESHOLD}): {len(small_rows)}")
    print(f"Large (>={THRESHOLD}): {len(large_rows)}")


if __name__ == "__main__":
    main()