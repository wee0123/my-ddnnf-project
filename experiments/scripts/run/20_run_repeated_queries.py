
#!/usr/bin/env python3
import argparse
import csv
import time
from pathlib import Path
from experiments.scripts.common.meta_utils import load_json, read_benchmark_rows, read_selected_models, add_feature_bins, filter_rows, ensure_dir, resolve_cnf_path
from experiments.scripts.common.runner_common import shell_format, run_command, parse_count_from_output, append_csv_row, dump_log
from experiments.scripts.common.queryset_utils import (
    read_generic_query_file,
    conditioned_cnf_path,
    build_conditioned_cnf,
    write_query_ddnnf_cmd,
)

FIELDNAMES = [
    "experiment","tool","model","feature_bin","size_class","format","feature_count","query_index","query_literals","total_features","nvars","nclauses","run_index","phase",
    "command","returncode","timeout","elapsed_sec","query_count","elapsed_per_query_sec","parsed_count","stdout_log","stderr_log"
]

def read_ddnnife_query_counts(path: Path):
    if not path.exists():
        return []
    out = []
    with open(path, "r", encoding="utf-8", newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            out.append(row[-1].strip())
    return out

def parse_query_ddnnf_counts(stdout: str):
    out = []
    for line in (stdout or "").splitlines():
        s = line.strip()
        if s.startswith(">"):
            s = s[1:].strip()
        if s.isdigit():
            out.append(s)
    return out

def format_lits(lits):
    return " ".join(map(str, lits))

def remaining_timeout(timeout_sec, group_timeout_sec, group_start):
    if group_timeout_sec is None:
        return timeout_sec
    remaining = group_timeout_sec - (time.perf_counter() - group_start)
    if remaining <= 0:
        return None
    return min(timeout_sec, max(1, int(remaining)))

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
    generic_q_dir = root / cfg["query_generation"]["generic_query_dir"]
    tmp_dir = root / cfg["paths"]["tmp_dir"]
    ensure_dir(tmp_dir)

    for tool_name, tool in cfg["tools"].items():
        if not tool.get("enabled_repeated", False):
            continue
        out_dir = root / cfg["paths"]["results_repeated_dir"] / tool_name
        log_dir = root / cfg["paths"]["log_run_dir"] / tool_name / "repeated_queries"
        ensure_dir(out_dir)
        ensure_dir(log_dir)
        result_csv = out_dir / "raw_repeated_query_runs.csv"

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

            for k in cfg["query_generation"]["feature_counts"]:
                group_timeout_sec = cfg.get("repeated_group_timeout_sec")
                group_start = time.perf_counter()
                group_timed_out = False
                query_file = generic_q_dir / f"{model}__k{k}.txt"
                if not query_file.exists():
                    print(f"[WARN] missing query file {query_file}")
                    continue
                queries = read_generic_query_file(query_file)

                nnf = None
                if tool.get("kind") == "query_ddnnf":
                    source = tool.get("nnf_source", "d4")
                    if source == "c2d":
                        nnf = c2d_nnf_dir / f"{model}.nnf"
                    else:
                        nnf = d4_nnf_dir / f"{model}.nnf"
                    if not nnf.exists():
                        print(f"[WARN] skip repeated {tool_name}/{model}: missing nnf {nnf}")
                        continue

                if tool.get("kind") == "ddnnife":
                    for run_index in range(total_runs):
                        phase = "warmup" if run_index < warmup_runs else "measured"
                        output_prefix = tmp_dir / f"{model}__k{k}__{tool_name}__run{run_index}"
                        output_csv = Path(f"{output_prefix}-queries.csv")
                        if output_csv.exists():
                            output_csv.unlink()
                        cmd = shell_format(
                            tool["repeated_cmd_template"],
                            bin=str(bin_path),
                            cnf=str(cnf),
                            nnf=str(nnf),
                            nvars=row.get("nvars") or "",
                            total_features=row.get("total_features") or "",
                            query_file=str(query_file),
                            output_prefix=str(output_prefix),
                        )
                        cmd_timeout = timeout_sec
                        cmd_timeout = remaining_timeout(timeout_sec, group_timeout_sec, group_start)
                        if cmd_timeout is None:
                            print(f"[TIMEOUT] skip rest: {tool_name}/{model}/k={k}")
                            group_timed_out = True
                            break
                        res = run_command(cmd, cmd_timeout)
                        counts = read_ddnnife_query_counts(output_csv)
                        query_count = len(queries)
                        elapsed_per_query = res["elapsed_sec"] / query_count if query_count else res["elapsed_sec"]
                        stdout_log = log_dir / f"{model}__k{k}__run{run_index}__stdout.log"
                        stderr_log = log_dir / f"{model}__k{k}__run{run_index}__stderr.log"
                        dump_log(stdout_log, res["stdout"])
                        dump_log(stderr_log, res["stderr"])
                        for q_idx in range(query_count):
                            parsed = counts[q_idx] if q_idx < len(counts) else ""
                            append_csv_row(result_csv, {
                                "experiment": "repeated_queries",
                                "tool": tool_name,
                                "model": model,
                                "feature_bin": row.get("feature_bin"),
                                "size_class": row.get("size_class"),
                                "format": row.get("format"),
                                "feature_count": k,
                                "query_index": q_idx,
                                "query_literals": format_lits(queries[q_idx]),
                                "total_features": row.get("total_features"),
                                "nvars": row.get("nvars"),
                                "nclauses": row.get("nclauses"),
                                "run_index": run_index,
                                "phase": phase,
                                "command": " ".join(cmd),
                                "returncode": res["returncode"],
                                "timeout": int(bool(res["timeout"])),
                                "elapsed_sec": f"{res['elapsed_sec']:.6f}",
                                "query_count": query_count,
                                "elapsed_per_query_sec": f"{elapsed_per_query:.6f}",
                                "parsed_count": parsed,
                                "stdout_log": str(stdout_log.relative_to(root)),
                                "stderr_log": str(stderr_log.relative_to(root)),
                            }, FIELDNAMES)
                            if group_timed_out:
                                break
                        print(f"[{tool_name}] repeated(batch) {model} k={k} run={run_index} phase={phase} elapsed={res['elapsed_sec']:.3f}s")
                        if group_timed_out:
                            break
                    continue

                if tool.get("kind") == "query_ddnnf":
                    for run_index in range(total_runs):
                        phase = "warmup" if run_index < warmup_runs else "measured"
                        cmd_file = tmp_dir / f"{model}__k{k}__{tool_name}__run{run_index}.cmd"
                        write_query_ddnnf_cmd(cmd_file, nnf, queries)
                        cmd = [str(bin_path), "-cmd", str(cmd_file)]
                        res = run_command(cmd, timeout_sec)
                        counts = parse_query_ddnnf_counts(res.get("stdout", "") or "")
                        query_count = len(queries)
                        elapsed_per_query = res["elapsed_sec"] / query_count if query_count else res["elapsed_sec"]
                        stdout_log = log_dir / f"{model}__k{k}__run{run_index}__stdout.log"
                        stderr_log = log_dir / f"{model}__k{k}__run{run_index}__stderr.log"
                        dump_log(stdout_log, res["stdout"])
                        dump_log(stderr_log, res["stderr"])
                        for q_idx in range(query_count):
                            parsed = counts[q_idx] if q_idx < len(counts) else ""
                            append_csv_row(result_csv, {
                                "experiment": "repeated_queries",
                                "tool": tool_name,
                                "model": model,
                                "feature_bin": row.get("feature_bin"),
                                "size_class": row.get("size_class"),
                                "format": row.get("format"),
                                "feature_count": k,
                                "query_index": q_idx,
                                "query_literals": format_lits(queries[q_idx]),
                                "total_features": row.get("total_features"),
                                "nvars": row.get("nvars"),
                                "nclauses": row.get("nclauses"),
                                "run_index": run_index,
                                "phase": phase,
                                "command": " ".join(cmd),
                                "returncode": res["returncode"],
                                "timeout": int(bool(res["timeout"])),
                                "elapsed_sec": f"{res['elapsed_sec']:.6f}",
                                "query_count": query_count,
                                "elapsed_per_query_sec": f"{elapsed_per_query:.6f}",
                                "parsed_count": parsed,
                                "stdout_log": str(stdout_log.relative_to(root)),
                                "stderr_log": str(stderr_log.relative_to(root)),
                            }, FIELDNAMES)
                        print(f"[{tool_name}] repeated(batch) {model} k={k} run={run_index} phase={phase} elapsed={res['elapsed_sec']:.3f}s")
                    continue

                # direct-counter style: one conditioned CNF per query
                for run_index in range(total_runs):
                    phase = "warmup" if run_index < warmup_runs else "measured"
                    if group_timed_out:
                        break
                    for q_idx, lits in enumerate(queries):
                        cmd_timeout = remaining_timeout(timeout_sec, group_timeout_sec, group_start)
                        if cmd_timeout is None:
                            print(f"[TIMEOUT] skip rest: {tool_name}/{model}/k={k}")
                            group_timed_out = True
                            break
                        cond_cnf = conditioned_cnf_path(tmp_dir, model, q_idx, k)
                        build_conditioned_cnf(cnf, cond_cnf, lits)
                        cmd = shell_format(
                            tool["repeated_cmd_template"],
                            bin=str(bin_path),
                            cnf=str(cnf),
                            conditioned_cnf=str(cond_cnf),
                            nnf=str(nnf) if nnf else "",
                            nvars=row.get("nvars") or "",
                            query_file=str(query_file),
                        )
                        res = run_command(cmd, cmd_timeout)
                        parsed = parse_count_from_output(
                            tool_name,
                            res.get("stdout", "") or "",
                            res.get("stderr", "") or "",
                        )
                        stdout_log = log_dir / f"{model}__k{k}__q{q_idx}__run{run_index}__stdout.log"
                        stderr_log = log_dir / f"{model}__k{k}__q{q_idx}__run{run_index}__stderr.log"
                        dump_log(stdout_log, res["stdout"])
                        dump_log(stderr_log, res["stderr"])
                        append_csv_row(result_csv, {
                            "experiment": "repeated_queries",
                            "tool": tool_name,
                            "model": model,
                            "feature_bin": row.get("feature_bin"),
                            "size_class": row.get("size_class"),
                            "format": row.get("format"),
                            "feature_count": k,
                            "query_index": q_idx,
                            "query_literals": format_lits(lits),
                            "total_features": row.get("total_features"),
                            "nvars": row.get("nvars"),
                            "nclauses": row.get("nclauses"),
                            "run_index": run_index,
                            "phase": phase,
                            "command": " ".join(cmd),
                            "returncode": res["returncode"],
                            "timeout": int(bool(res["timeout"])),
                            "elapsed_sec": f"{res['elapsed_sec']:.6f}",
                            "query_count": 1,
                            "elapsed_per_query_sec": f"{res['elapsed_sec']:.6f}",
                            "parsed_count": parsed or "",
                            "stdout_log": str(stdout_log.relative_to(root)),
                            "stderr_log": str(stderr_log.relative_to(root)),
                        }, FIELDNAMES)
                    if group_timed_out:
                        break
                    print(f"[{tool_name}] repeated(loop) {model} k={k} run={run_index} phase={phase}")
if __name__ == "__main__":
    main()
