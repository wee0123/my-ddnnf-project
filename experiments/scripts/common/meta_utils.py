#!/usr/bin/env python3
import csv
import json
from pathlib import Path
from typing import Dict, List, Optional

REQUIRED_COLUMNS = [
    "name",
    "input_path",
    "format",
    "n_vars",
    "n_clauses",
    "total_features",
    "size_class",
]

def load_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def _safe_int(value):
    if value is None:
        return None
    s = str(value).strip()
    if s == "":
        return None
    try:
        return int(float(s))
    except Exception:
        return None

def normalize_model_stem(name: str) -> str:
    return Path(str(name).strip()).stem

def read_selected_models(txt_path: Path) -> List[str]:
    if not txt_path.exists():
        return []
    out = []
    with open(txt_path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if s and not s.startswith("#"):
                out.append(normalize_model_stem(s))
    return out

def read_benchmark_rows(csv_path: Path,
                        name_column="name",
                        input_path_column="input_path",
                        format_column="format",
                        nvars_column="n_vars",
                        clause_column="n_clauses",
                        total_features_column="total_features",
                        size_class_column="size_class") -> List[Dict]:
    rows = []
    with open(csv_path, "r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or []
        required = [
            name_column, input_path_column, format_column,
            nvars_column, clause_column, total_features_column, size_class_column
        ]
        missing = [c for c in required if c not in fieldnames]
        if missing:
            raise RuntimeError(
                f"CSV header mismatch in {csv_path}. Missing required columns: {missing}. "
                f"Found columns: {fieldnames}"
            )

        for row in reader:
            model = normalize_model_stem(row[name_column])
            rows.append({
                "model": model,
                "input_path": str(row[input_path_column]).strip(),
                "format": str(row[format_column]).strip(),
                "nvars": _safe_int(row[nvars_column]),
                "nclauses": _safe_int(row[clause_column]),
                "total_features": _safe_int(row[total_features_column]),
                "size_class": str(row[size_class_column]).strip().lower(),
                "_raw": row,
            })
    return rows

def assign_feature_bin(total_features: Optional[int], bins: List[Dict], metadata_size_class: Optional[str] = None) -> str:
    if metadata_size_class:
        return str(metadata_size_class).strip().lower()
    if total_features is None:
        return "unknown"
    for b in bins:
        if b["min"] <= total_features <= b["max"]:
            return b["name"]
    return "unknown"

def filter_rows(rows: List[Dict],
                bins: Optional[List[Dict]] = None,
                use_selected_models: bool = False,
                selected_models: Optional[List[str]] = None,
                max_models_per_bin: Optional[int] = None,
                allowlist: Optional[List[str]] = None,
                denylist: Optional[List[str]] = None,
                prefer_metadata_size_class: bool = True) -> List[Dict]:
    selected_set = set(selected_models or [])
    allow_set = set(normalize_model_stem(x) for x in (allowlist or []))
    deny_set = set(normalize_model_stem(x) for x in (denylist or []))

    processed = []
    for r in rows:
        model = r["model"]
        if use_selected_models and selected_set and model not in selected_set:
            continue
        if allow_set and model not in allow_set:
            continue
        if deny_set and model in deny_set:
            continue

        rr = dict(r)
        if "feature_bin" not in rr or not rr["feature_bin"]:
            if bins is not None:
                rr["feature_bin"] = assign_feature_bin(
                    rr.get("total_features"),
                    bins,
                    rr.get("size_class") if prefer_metadata_size_class else None
                )
            else:
                rr["feature_bin"] = rr.get("size_class", "unknown")
        processed.append(rr)

    if not max_models_per_bin:
        return processed

    by_bin: Dict[str, List[Dict]] = {}
    for r in processed:
        by_bin.setdefault(r["feature_bin"], []).append(r)

    out = []
    for bin_name, items in by_bin.items():
        items = sorted(items, key=lambda x: (x.get("total_features") or 10**18, x["model"]))
        out.extend(items[:max_models_per_bin])
    return out

def ensure_dir(path: Path):
    path.mkdir(parents=True, exist_ok=True)

def add_feature_bins(rows: List[Dict], bins: List[Dict], prefer_metadata_size_class: bool = True) -> List[Dict]:
    out = []
    for r in rows:
        rr = dict(r)
        rr["feature_bin"] = assign_feature_bin(
            rr.get("total_features"),
            bins,
            rr.get("size_class") if prefer_metadata_size_class else None,
        )
        out.append(rr)
    return out

def resolve_cnf_path(project_root: Path, cfg_or_row, maybe_row: Optional[Dict] = None) -> Path:
    """
    Backward-compatible resolver.
    Supports:
      resolve_cnf_path(project_root, row, default_cnf_dir)
      resolve_cnf_path(project_root, cfg, row)
    """
    if maybe_row is not None and isinstance(cfg_or_row, dict) and "paths" in cfg_or_row:
        cfg = cfg_or_row
        row = maybe_row
        default_cnf_dir = project_root / cfg["paths"]["cnf_dir"]
    else:
        row = cfg_or_row
        default_cnf_dir = maybe_row if isinstance(maybe_row, Path) else (project_root / "benchmarks/cnf")

    raw_path = str(row.get("input_path", "")).strip()
    if raw_path:
        p = Path(raw_path)
        if p.is_absolute() and p.exists():
            return p
        p2 = project_root / raw_path
        if p2.exists():
            return p2
        p3 = default_cnf_dir / Path(raw_path).name
        if p3.exists():
            return p3
        return p2
    return default_cnf_dir / f"{row['model']}.cnf"
