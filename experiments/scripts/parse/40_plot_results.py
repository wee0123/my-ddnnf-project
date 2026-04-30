#!/usr/bin/env python3
import argparse, csv
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from experiments.scripts.common.meta_utils import load_json, ensure_dir

def read_rows(path: Path):
    if not path.exists():
        return []
    with open(path, "r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))

def _safe_num(s):
    try:
        return float(s)
    except Exception:
        return None

def plot_total_count(root: Path, cfg: dict):
    rows = read_rows(root / cfg["paths"]["merged_csv_dir"] / "total_count_aggregated.csv")
    if not rows:
        return
    tools = sorted(set(r["tool"] for r in rows))
    fig, ax = plt.subplots(figsize=(10, 6))
    for tool in tools:
        pts = []
        for r in rows:
            if r["tool"] != tool:
                continue
            x = _safe_num(r.get("total_features")) or _safe_num(r.get("nvars"))
            y = _safe_num(r.get("median_sec"))
            if x is not None and y is not None:
                pts.append((int(x), y))
        pts.sort()
        if pts:
            ax.plot([p[0] for p in pts], [p[1] for p in pts], marker="o", label=tool)
    ax.set_xlabel("Total features (fallback: n_vars)")
    ax.set_ylabel("Median runtime (s)")
    ax.set_title("Total-count runtime vs. model size")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(root / cfg["paths"]["figures_dir"] / "total_count_vs_model_size.png", dpi=180)
    plt.close(fig)

def plot_repeated_macro(root: Path, cfg: dict):
    rows = read_rows(root / cfg["paths"]["merged_csv_dir"] / "repeated_queries_macro.csv")
    if not rows:
        return
    tools = sorted(set(r["tool"] for r in rows))
    fig, ax = plt.subplots(figsize=(10, 6))
    for tool in tools:
        pts = [(int(float(r["feature_count"])), float(r["median_sec"])) for r in rows if r["tool"] == tool]
        pts.sort()
        if pts:
            ax.plot([p[0] for p in pts], [p[1] for p in pts], marker="o", label=tool)
    ax.set_xlabel("Query feature count")
    ax.set_ylabel("Median runtime per query (s)")
    ax.set_title("Repeated partial-configuration count runtime vs. query feature count")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(root / cfg["paths"]["figures_dir"] / "repeated_queries_vs_feature_count.png", dpi=180)
    plt.close(fig)

def plot_total_count_box(root: Path, cfg: dict):
    rows = read_rows(root / cfg["paths"]["merged_csv_dir"] / "total_count_aggregated.csv")
    if not rows:
        return
    tools = sorted(set(r["tool"] for r in rows))
    data = []
    labels = []
    for tool in tools:
        vals = [float(r["median_sec"]) for r in rows if r["tool"] == tool]
        if vals:
            data.append(vals)
            labels.append(tool)
    if not data:
        return
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.boxplot(data, tick_labels=labels, showfliers=True)
    ax.set_ylabel("Median runtime per model (s)")
    ax.set_title("Total-count runtime distribution across models")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(root / cfg["paths"]["figures_dir"] / "total_count_boxplot.png", dpi=180)
    plt.close(fig)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-root", required=True)
    ap.add_argument("--config", required=True)
    args = ap.parse_args()
    root = Path(args.project_root).resolve()
    cfg = load_json(root / args.config)
    ensure_dir(root / cfg["paths"]["figures_dir"])
    plot_total_count(root, cfg)
    plot_repeated_macro(root, cfg)
    plot_total_count_box(root, cfg)
    print("[OK] figures written")

if __name__ == "__main__":
    main()