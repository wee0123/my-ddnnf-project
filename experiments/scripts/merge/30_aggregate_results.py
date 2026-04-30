#!/usr/bin/env python3
import argparse, csv, statistics
from pathlib import Path
from experiments.scripts.common.meta_utils import load_json, ensure_dir

def read_rows(path: Path):
    if not path.exists():
        return []
    with open(path, "r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))

def write_rows(path: Path, rows, fieldnames):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)

def group_stats(values):
    if not values:
        return {}
    return {
        "n": len(values),
        "mean_sec": statistics.mean(values),
        "median_sec": statistics.median(values),
        "min_sec": min(values),
        "max_sec": max(values),
        "stdev_sec": statistics.stdev(values) if len(values) >= 2 else 0.0,
    }

def valid_measured_count_row(r):
    if r.get("phase") != "measured":
        return False
    if r.get("timeout") == "1":
        return False
    if not r.get("parsed_count"):
        return False
    return True

def row_time(r):
    value = r.get("elapsed_per_query_sec") or r.get("elapsed_sec")
    return float(value)

def read_compile_time(root: Path, model: str, source: str = "d4") -> float:
    path = root / "results" / "logs" / source / f"{model}.time"
    if not path.exists():
        return 0.0
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("real="):
                try:
                    return float(line.split("=", 1)[1].strip())
                except Exception:
                    return 0.0
    return 0.0

def compile_time_for_tool(root: Path, cfg: dict, tool: str, model: str) -> float:
    tool_cfg = cfg.get("tools", {}).get(tool, {})
    if tool_cfg.get("includes_compile_time", False):
        return 0.0
    if tool_cfg.get("kind") not in {"ddnnife", "query_ddnnf"}:
        return 0.0
    source = tool_cfg.get("nnf_source", "d4")
    return read_compile_time(root, model, source)

def aggregate_total(root: Path, cfg: dict):
    out = []
    base = root / cfg["paths"]["results_total_dir"]
    if not base.exists():
        return
    for tool_dir in base.iterdir():
        csv_path = tool_dir / "raw_total_count_runs.csv"
        rows = read_rows(csv_path)
        grouped = {}
        for r in rows:
            if not valid_measured_count_row(r):
                continue
            key = (
                r["tool"], r["model"], r.get("feature_bin",""), r.get("size_class",""),
                r.get("total_features",""), r.get("nvars",""), r.get("nclauses",""), r.get("format","")
            )
            grouped.setdefault(key, []).append(float(r["elapsed_sec"]))
        for key, vals in grouped.items():
            stats = group_stats(vals)
            compile_sec = compile_time_for_tool(root, cfg, key[0], key[1])
            out.append({
                "tool": key[0],
                "model": key[1],
                "feature_bin": key[2],
                "size_class": key[3],
                "total_features": key[4],
                "nvars": key[5],
                "nclauses": key[6],
                "format": key[7],
                "compile_sec": f"{compile_sec:.6f}",
                "median_with_compile_sec": f"{(compile_sec + stats['median_sec']):.6f}",
                **{k: f"{v:.6f}" if isinstance(v, float) else v for k, v in stats.items()}
            })
    fieldnames = ["tool","model","feature_bin","size_class","total_features","nvars","nclauses","format","compile_sec","median_with_compile_sec","n","mean_sec","median_sec","min_sec","max_sec","stdev_sec"]
    write_rows(root / cfg["paths"]["merged_csv_dir"] / "total_count_aggregated.csv", out, fieldnames)

def aggregate_repeated(root: Path, cfg: dict):
    out = []
    base = root / cfg["paths"]["results_repeated_dir"]
    if not base.exists():
        return
    for tool_dir in base.iterdir():
        csv_path = tool_dir / "raw_repeated_query_runs.csv"
        rows = read_rows(csv_path)
        grouped = {}
        for r in rows:
            if not valid_measured_count_row(r):
                continue
            key = (
                r["tool"], r["model"], r.get("feature_bin",""), r.get("size_class",""),
                r["feature_count"], r.get("total_features",""), r.get("nvars",""), r.get("nclauses",""), r.get("format","")
            )
            grouped.setdefault(key, []).append(row_time(r))
        for key, vals in grouped.items():
            stats = group_stats(vals)
            compile_sec = compile_time_for_tool(root, cfg, key[0], key[1])
            query_count = int(float(key[4])) if str(key[4]) == "1" else int(cfg.get("query_generation", {}).get("queries_per_feature_count", 1))
            if str(key[4]) == "1":
                try:
                    query_count = int(float(key[6]))
                except Exception:
                    query_count = int(cfg.get("query_generation", {}).get("queries_per_feature_count", 1))
            compile_per_query = compile_sec / query_count if query_count else 0.0
            out.append({
                "tool": key[0],
                "model": key[1],
                "feature_bin": key[2],
                "size_class": key[3],
                "feature_count": key[4],
                "total_features": key[5],
                "nvars": key[6],
                "nclauses": key[7],
                "format": key[8],
                "compile_sec": f"{compile_sec:.6f}",
                "compile_per_query_sec": f"{compile_per_query:.6f}",
                "median_with_compile_sec": f"{(compile_per_query + stats['median_sec']):.6f}",
                **{k: f"{v:.6f}" if isinstance(v, float) else v for k, v in stats.items()}
            })
    fieldnames = ["tool","model","feature_bin","size_class","feature_count","total_features","nvars","nclauses","format","compile_sec","compile_per_query_sec","median_with_compile_sec","n","mean_sec","median_sec","min_sec","max_sec","stdev_sec"]
    write_rows(root / cfg["paths"]["merged_csv_dir"] / "repeated_queries_aggregated.csv", out, fieldnames)

def aggregate_repeated_correctness(root: Path, cfg: dict):
    out = []
    base = root / cfg["paths"]["results_repeated_dir"]
    if not base.exists():
        return
    grouped = {}
    for tool_dir in base.iterdir():
        csv_path = tool_dir / "raw_repeated_query_runs.csv"
        for r in read_rows(csv_path):
            if not valid_measured_count_row(r):
                continue
            if r.get("query_index", "-1") == "-1":
                continue
            key = (r["model"], r["feature_count"], r["query_index"])
            grouped.setdefault(key, {}).setdefault(r["tool"], set()).add(r["parsed_count"])

    for key, tool_counts in grouped.items():
        stable_items = []
        for tool, counts in sorted(tool_counts.items()):
            stable_items.append((tool, "|".join(sorted(counts))))
        reference_tool, reference_count = stable_items[0]
        for tool, counts in stable_items:
            out.append({
                "model": key[0],
                "feature_count": key[1],
                "query_index": key[2],
                "tool": tool,
                "counts_seen": counts,
                "reference_tool": reference_tool,
                "reference_count": reference_count,
                "match_reference": int(counts == reference_count),
            })

    fieldnames = ["model","feature_count","query_index","tool","counts_seen","reference_tool","reference_count","match_reference"]
    write_rows(root / cfg["paths"]["merged_csv_dir"] / "repeated_query_count_check.csv", out, fieldnames)

def aggregate_repeated_macro(root: Path, cfg: dict):
    src = root / cfg["paths"]["merged_csv_dir"] / "repeated_queries_aggregated.csv"
    rows = read_rows(src)
    grouped = {}
    for r in rows:
        key = (r["tool"], r["feature_count"])
        grouped.setdefault(key, []).append(float(r["median_sec"]))
    out = []
    for key, vals in grouped.items():
        stats = group_stats(vals)
        out.append({
            "tool": key[0],
            "feature_count": key[1],
            **{k: f"{v:.6f}" if isinstance(v, float) else v for k, v in stats.items()}
        })
    fieldnames = ["tool","feature_count","n","mean_sec","median_sec","min_sec","max_sec","stdev_sec"]
    write_rows(root / cfg["paths"]["merged_csv_dir"] / "repeated_queries_macro.csv", out, fieldnames)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-root", required=True)
    ap.add_argument("--config", required=True)
    args = ap.parse_args()
    root = Path(args.project_root).resolve()
    cfg = load_json(root / args.config)
    ensure_dir(root / cfg["paths"]["merged_csv_dir"])
    aggregate_total(root, cfg)
    aggregate_repeated(root, cfg)
    aggregate_repeated_correctness(root, cfg)
    aggregate_repeated_macro(root, cfg)
    print("[OK] aggregated CSV files written")

if __name__ == "__main__":
    main()
