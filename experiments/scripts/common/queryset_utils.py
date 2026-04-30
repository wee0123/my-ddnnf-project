
#!/usr/bin/env python3
from pathlib import Path
import random
from typing import List, Tuple

def parse_cnf_header(cnf_path: Path) -> Tuple[int, int]:
    with open(cnf_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.strip()
            if s.startswith("p cnf"):
                parts = s.split()
                return int(parts[2]), int(parts[3])
    raise RuntimeError(f"Could not find DIMACS header in {cnf_path}")

def generate_random_queries(nvars: int, feature_count: int, num_queries: int, seed: int) -> List[List[int]]:
    rng = random.Random(seed)
    queries = []
    universe = list(range(1, nvars + 1))
    if feature_count > nvars:
        return queries
    for _ in range(num_queries):
        vars_sel = rng.sample(universe, feature_count)
        query = []
        for v in vars_sel:
            lit = v if rng.random() < 0.5 else -v
            query.append(lit)
        queries.append(query)
    return queries

def write_generic_query_file(path: Path, queries: List[List[int]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for q in queries:
            f.write(" ".join(map(str, q)) + "\n")

def read_generic_query_file(path: Path) -> List[List[int]]:
    out = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            out.append([int(x) for x in s.split()])
    return out

def conditioned_cnf_path(tmp_dir: Path, model: str, q_index: int, feature_count: int) -> Path:
    return tmp_dir / f"{model}__k{feature_count}__q{q_index}.cnf"

def build_conditioned_cnf(src_cnf: Path, dst_cnf: Path, lits: List[int]):
    nvars, nclauses = parse_cnf_header(src_cnf)
    unit_clauses = [" ".join([str(l), "0"]) for l in lits]
    new_clause_count = nclauses + len(unit_clauses)
    with open(src_cnf, "r", encoding="utf-8", errors="ignore") as fin, open(dst_cnf, "w", encoding="utf-8") as fout:
        wrote_header = False
        for line in fin:
            s = line.strip()
            if s.startswith("p cnf"):
                fout.write(f"p cnf {nvars} {new_clause_count}\n")
                wrote_header = True
            else:
                fout.write(line)
        if not wrote_header:
            raise RuntimeError(f"Missing CNF header in {src_cnf}")
        for clause in unit_clauses:
            fout.write(clause + "\n")

def write_query_ddnnf_cmd(path: Path, nnf_path: Path, queries: List[List[int]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"load {nnf_path}\n")
        for q in queries:
            f.write("mc " + " ".join(map(str, q)) + "\n")
        f.write("q\n")
