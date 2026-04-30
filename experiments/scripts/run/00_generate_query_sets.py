
#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
from experiments.scripts.common.meta_utils import load_json, read_benchmark_rows, read_selected_models, add_feature_bins, filter_rows, ensure_dir, resolve_cnf_path
from experiments.scripts.common.queryset_utils import parse_cnf_header, generate_random_queries, write_generic_query_file

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-root", required=True)
    ap.add_argument("--config", required=True)
    args = ap.parse_args()

    root = Path(args.project_root).resolve()
    cfg = load_json(root / args.config)

    meta_cfg = cfg["metadata"]
    csv_path = root / meta_cfg["benchmark_csv"]
    selected_path = root / meta_cfg["selected_models_txt"]
    rows = read_benchmark_rows(
        csv_path,
        name_column=meta_cfg.get("name_column", "name"),
        input_path_column=meta_cfg.get("input_path_column", "input_path"),
        format_column=meta_cfg.get("format_column", "format"),
        nvars_column=meta_cfg.get("nvars_column", "n_vars"),
        clause_column=meta_cfg.get("clause_column", "n_clauses"),
        total_features_column=meta_cfg.get("total_features_column", "total_features"),
        size_class_column=meta_cfg.get("size_class_column", "size_class"),
    )
    rows = add_feature_bins(rows, cfg["feature_bins"], meta_cfg.get("prefer_metadata_size_class", True))
    selected = read_selected_models(selected_path) if meta_cfg.get("use_selected_models", True) else []
    rows = filter_rows(
        rows,
        use_selected_models=meta_cfg.get("use_selected_models", True),
        selected_models=selected,
        allowlist=cfg["selection"].get("explicit_model_allowlist", []),
        denylist=cfg["selection"].get("explicit_model_denylist", []),
        max_models_per_bin=cfg["selection"].get("max_models_per_bin"),
    )

    out_dir = root / cfg["query_generation"]["generic_query_dir"]
    ensure_dir(out_dir)
    seed = int(cfg["random_seed"])
    qs_per_k = int(cfg["query_generation"]["queries_per_feature_count"])
    feature_counts = list(cfg["query_generation"]["feature_counts"])

    for row in rows:
        model = row["model"]
        cnf_path = resolve_cnf_path(root, cfg, row)
        if not cnf_path.exists():
            print(f"[WARN] Missing CNF for {model}: {cnf_path}")
            continue
        nvars, _ = parse_cnf_header(cnf_path)
        row["nvars"] = row.get("nvars") or nvars

        for k in feature_counts:
            if k > nvars:
                continue
            if int(k) == 1:
                # Paper-style feature cardinality: count each positively selected feature.
                queries = [[v] for v in range(1, nvars + 1)]
            else:
                q_seed = stable_seed(seed, model, k)
                queries = generate_random_queries(nvars, k, qs_per_k, q_seed)
            out_path = out_dir / f"{model}__k{k}.txt"
            if out_path.exists() and not cfg["query_generation"].get("overwrite", False):
                print(f"[SKIP] {out_path} already exists")
                continue
            write_generic_query_file(out_path, queries)
            print(f"[OK] wrote {out_path} ({len(queries)} queries)")

def stable_seed(base_seed: int, model: str, k: int) -> int:
    h = hashlib.sha256(f"{model}:{k}".encode("utf-8")).hexdigest()
    return base_seed + int(h[:8], 16)
if __name__ == "__main__":
    main()
