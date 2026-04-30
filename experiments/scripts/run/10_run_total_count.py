
#!/usr/bin/env python3
import argparse, os
from pathlib import Path
from experiments.scripts.common.meta_utils import load_json, read_benchmark_rows, read_selected_models, add_feature_bins, filter_rows, ensure_dir, resolve_cnf_path
from experiments.scripts.common.runner_common import shell_format, run_command, parse_count_from_output, append_csv_row, dump_log
from experiments.scripts.common.queryset_utils import write_query_ddnnf_cmd

FIELDNAMES = [
    "experiment","tool","model","feature_bin","size_class","format","total_features","nvars","nclauses","run_index","phase",
    "command","returncode","timeout","elapsed_sec","parsed_count","stdout_log","stderr_log"
]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-root", required=True)
    ap.add_argument("--config", required=True)
    args = ap.parse_args()

    root = Path(args.project_root).resolve()
    cfg = load_json(root / args.config)

    meta_cfg = cfg["metadata"]
    rows = read_benchmark_rows(
        root / meta_cfg["benchmark_csv"],
        name_column=meta_cfg.get("name_column", "name"),
        input_path_column=meta_cfg.get("input_path_column", "input_path"),
        format_column=meta_cfg.get("format_column", "format"),
        nvars_column=meta_cfg.get("nvars_column", "n_vars"),
        clause_column=meta_cfg.get("clause_column", "n_clauses"),
        total_features_column=meta_cfg.get("total_features_column", "total_features"),
        size_class_column=meta_cfg.get("size_class_column", "size_class"),
    )
    rows = add_feature_bins(rows, cfg["feature_bins"], meta_cfg.get("prefer_metadata_size_class", True))
    selected = read_selected_models(root / meta_cfg["selected_models_txt"]) if meta_cfg.get("use_selected_models", True) else []
    # print("=== DEBUG selected_models ===")
    # print(selected)
    # print("=== DEBUG rows before filter ===")
    # print([r["model"] for r in rows[:20]])
    rows = filter_rows(
        rows,
        use_selected_models=meta_cfg.get("use_selected_models", True),
        selected_models=selected,
        allowlist=cfg["selection"].get("explicit_model_allowlist", []),
        denylist=cfg["selection"].get("explicit_model_denylist", []),
        max_models_per_bin=cfg["selection"].get("max_models_per_bin"),
    )

    warmup_runs = int(cfg["warmup_runs"])
    measured_runs = int(cfg["measured_runs"])
    total_runs = warmup_runs + measured_runs
    timeout_sec = int(cfg["timeout_sec"])

    d4_nnf_dir = root / cfg["paths"]["d4_nnf_dir"]
    c2d_nnf_dir = root / cfg["paths"]["c2d_nnf_dir"]
    tmp_dir = root / cfg["paths"]["tmp_dir"]
    ensure_dir(tmp_dir)

    # print("=== DEBUG rows after filter ===")
    # print([r["model"] for r in rows])

    for tool_name, tool in cfg["tools"].items():
        if not tool.get("enabled_total", False):
            continue
        out_dir = root / cfg["paths"]["results_total_dir"] / tool_name
        log_dir = root / cfg["paths"]["log_run_dir"] / tool_name / "total_count"
        ensure_dir(out_dir)
        ensure_dir(log_dir)
        result_csv = out_dir / "raw_total_count_runs.csv"

        bin_path = root / tool["binary"]
        if not bin_path.exists():
            print(f"[WARN] skip {tool_name}: missing binary {bin_path}")
            continue

        for row in rows:
            model = row["model"]
            cnf = resolve_cnf_path(root, cfg, row)
            if not cnf.exists():
                print(f"[WARN] missing cnf for {model}")
                continue

            nnf = None
            if tool.get("kind") == "query_ddnnf":
                source = tool.get("nnf_source", "c2d")
                if source == "c2d":
                    nnf = c2d_nnf_dir / f"{model}.nnf"
                else:
                    nnf = d4_nnf_dir / f"{model}.nnf"
                if not nnf.exists():
                    print(f"[WARN] skip total {tool_name}/{model}: missing nnf {nnf}")
                    continue

            for run_index in range(total_runs):
                phase = "warmup" if run_index < warmup_runs else "measured"

                if tool.get("kind") == "query_ddnnf":
                    cmd_file = tmp_dir / f"{model}__{tool_name}__total.cmd"
                    write_query_ddnnf_cmd(cmd_file, nnf, [[]])
                    cmd = [str(bin_path), "-cmd", str(cmd_file)]
                else:
                    cmd = shell_format(
                        tool["total_cmd_template"],
                        bin=str(bin_path),
                        cnf=str(cnf),
                        nnf=str(nnf) if nnf else "",
                        nvars=row.get("nvars") or "",
                        total_features=row["total_features"],
                        model=model,
                        input_nnf=str(nnf) if nnf else str(root / "compiled" / "d4_nnf" / f"{model}.nnf")
                    )

                res = run_command(cmd, timeout_sec)
                parsed = parse_count_from_output(
                    tool_name,
                    res.get("stdout", "") or "",
                    res.get("stderr", "") or "",
                )

                stdout_log = log_dir / f"{model}__run{run_index}__stdout.log"
                stderr_log = log_dir / f"{model}__run{run_index}__stderr.log"
                dump_log(stdout_log, res["stdout"])
                dump_log(stderr_log, res["stderr"])

                append_csv_row(result_csv, {
                    "experiment": "total_count",
                    "tool": tool_name,
                    "model": model,
                    "feature_bin": row.get("feature_bin"),
                    "size_class": row.get("size_class"),
                    "format": row.get("format"),
                    "total_features": row.get("total_features"),
                    "nvars": row.get("nvars"),
                    "nclauses": row.get("nclauses"),
                    "run_index": run_index,
                    "phase": phase,
                    "command": " ".join(cmd),
                    "returncode": res["returncode"],
                    "timeout": int(bool(res["timeout"])),
                    "elapsed_sec": f"{res['elapsed_sec']:.6f}",
                    "parsed_count": parsed or "",
                    "stdout_log": str(stdout_log.relative_to(root)),
                    "stderr_log": str(stderr_log.relative_to(root)),
                }, FIELDNAMES)

                print(f"[{tool_name}] total_count {model} run={run_index} phase={phase} elapsed={res['elapsed_sec']:.3f}s count={parsed}")
if __name__ == "__main__":
    main()
