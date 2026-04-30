
#!/usr/bin/env python3
import csv
import json
import os
import re
import shlex
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional

def shell_format(template: str, **kwargs) -> List[str]:
    rendered = template.format(**kwargs)
    return shlex.split(rendered)

def first_existing(*paths: Path) -> Optional[Path]:
    for p in paths:
        if p and p.exists():
            return p
    return None

def ensure_text(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)

def run_command(cmd: List[str], timeout_sec: int, cwd: Optional[Path] = None):
    t0 = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(cwd) if cwd else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_sec,
            check=False,
        )
        dt = time.perf_counter() - t0
        return {
            "returncode": proc.returncode,
            "stdout": ensure_text(proc.stdout),
            "stderr": ensure_text(proc.stderr),
            "elapsed_sec": dt,
            "timeout": False,
        }
    except subprocess.TimeoutExpired as e:
        dt = time.perf_counter() - t0
        return {
            "returncode": 124,
            "stdout": ensure_text(e.stdout),
            "stderr": ensure_text(e.stderr),
            "elapsed_sec": dt,
            "timeout": True,
        }

# def parse_count_from_output(tool_name: str, stdout: str, stderr: str) -> Optional[str]:
#     out = stdout or ""
#     err = stderr or ""
#     text = out + "\n" + err

#     # DEBUG START
#     if tool_name == "ganak":
#         print("=== DEBUG GANAK TOOL NAME ===")
#         print(repr(tool_name))
#         print("=== DEBUG GANAK STDOUT TAIL ===")
#         print("\n".join((out or "").splitlines()[-20:]))
#         print("=== DEBUG GANAK STDERR TAIL ===")
#         print("\n".join((err or "").splitlines()[-20:]))
#     # DEBUG END

def parse_count_from_output(tool_name: str, stdout: str, stderr: str) -> Optional[str]:
    """
    Parse exact model count from solver output.

    Return:
        - decimal string for exact count, e.g. "123456789"
        - "0" for UNSAT / zero count
        - None if no exact count can be reliably extracted

    Important:
        - Never return "SATISFIABLE" / "UNSATISFIABLE" as count
        - Prefer exact model-count lines over SAT status lines
    """
    out = ensure_text(stdout)
    err = ensure_text(stderr)
    text = out + "\n" + err

    # Normalize line endings and keep both raw text + line-wise access
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]

    if tool_name == "countAntom":
        # Strict line-based parse first
        for line in lines:
            m = re.search(r"^c\s+model count\.*\s*:\s*([0-9]+)\s*$", line, re.IGNORECASE)
            if m:
                return m.group(1)

        # Slightly more permissive fallback
        m = re.search(r"c\s+model count\.*\s*:\s*([0-9]+)", text, re.IGNORECASE)
        if m:
            return m.group(1)

        # If UNSAT appears and no explicit count line exists, return 0
        if re.search(r"\bs\s+UNSATISFIABLE\b", text, re.IGNORECASE):
            return "0"

        # SAT status alone is NOT a count
        return None

    # if tool_name == "d4_query":
    #     d4_patterns = [
    #         r"^\s*s\s*+([0-9]+)\s*$",
    #         r"^\s*c?\s*s\s+exact arb int\s+([0-9]+)\s*$",
    #         r"^\s*s\s+exact arb int\s+([0-9]+)\s*$",
    #         r"^\s*c\s+exact arb int\s+([0-9]+)\s*$",
    #         r"^\s*exact arb int\s+([0-9]+)\s*$",
    #     ]
    #     for line in lines:
    #         for pat in d4_patterns:
    #             m = re.search(pat, line, re.IGNORECASE)
    #             if m:
    #                 return m.group(1)

    #     # If d4 prints a plain integer line at the end, accept it
    #     for line in reversed(lines):
    #         if re.fullmatch(r"[0-9]+", line):
    #             return line

    #     # UNSAT fallback
    #     if re.search(r"\bUNSATISFIABLE\b", text, re.IGNORECASE):
    #         return "0"

    #     return None

    if tool_name == "ganak":
        ganak_patterns = [
            r"(?im)^\s*c\s+s\s+exact arb frac\s+([0-9]+)\s*$",
            r"(?im)^\s*s\s+exact arb frac\s+([0-9]+)\s*$",
            r"(?im)^\s*c\s+s\s+exact arb int\s+([0-9]+)\s*$",
            r"(?im)^\s*s\s+exact arb int\s+([0-9]+)\s*$",
            r"(?im)^\s*s\s+mc\s+([0-9]+)\s*$",
            r"(?im)^\s*number of solutions\s*:\s*([0-9]+)\s*$",
            r"(?im)^\s*solutions\s*:\s*([0-9]+)\s*$",
            r"(?im)^\s*model count\s*:\s*([0-9]+)\s*$",
            r"(?im)^\s*count\s*:\s*([0-9]+)\s*$",
        ]

        for pat in ganak_patterns:
            m = re.search(pat, text)
            if m:
                return m.group(1)

        return None

    if tool_name == "sharpSAT":
        m = re.search(r"(?im)^\s*#\s*solutions\s*\n\s*([0-9]+)\s*$", text)
        if m:
            return m.group(1)
        m = re.search(r"(?im)^\s*Number of solutions\s*:\s*([0-9]+)\s*$", text)
        if m:
            return m.group(1)
        for i, line in enumerate(lines):
            if line.lower().startswith("# solutions") and i + 1 < len(lines):
                nxt = lines[i + 1]
                if re.fullmatch(r"[0-9]+", nxt):
                    return nxt
        if re.search(r"\bUNSATISFIABLE\b", text, re.IGNORECASE):
            return "0"
        return None

    if tool_name in ("my_ddnnife", "paper_ddnnife"):
        ddnnife_patterns = [
            r"^\s*ddnnf overall count\s*:\s*([0-9]+)\s*$",
            r"^\s*ddnnf count for query .* is\s*:\s*([0-9]+)\s*$",
            r"^\s*count\s*:\s*([0-9]+)\s*$",
            r"^\s*model count\s*:\s*([0-9]+)\s*$",
            r"^\s*result\s*:\s*([0-9]+)\s*$",
            r"^\s*cardinality\s*:\s*([0-9]+)\s*$",
            r"^\s*total count\s*:\s*([0-9]+)\s*$",
        ]
        for line in lines:
            for pat in ddnnife_patterns:
                m = re.search(pat, line, re.IGNORECASE)
                if m:
                    return m.group(1)

        # Plain integer line fallback
        for line in reversed(lines):
            if re.fullmatch(r"[0-9]+", line):
                return line

        if re.search(r"\bUNSATISFIABLE\b", text, re.IGNORECASE):
            return "0"

        return None

    if tool_name == "query_ddnnf":
        for line in reversed(lines):
            s = line
            if s.startswith(">"):
                s = s[1:].strip()
            if re.fullmatch(r"[0-9]+", s):
                return s
        return None

    generic_patterns = [
        r"^\s*model count\s*:\s*([0-9]+)\s*$",
        r"^\s*count\s*:\s*([0-9]+)\s*$",
        r"^\s*result\s*:\s*([0-9]+)\s*$",
        r"^\s*c?\s*s\s+exact arb int\s+([0-9]+)\s*$",
    ]
    for line in lines:
        for pat in generic_patterns:
            m = re.search(pat, line, re.IGNORECASE)
            if m:
                return m.group(1)

    for line in reversed(lines):
        if re.fullmatch(r"[0-9]+", line):
            return line

    if re.search(r"\bUNSATISFIABLE\b", text, re.IGNORECASE):
        return "0"

    return None

def append_csv_row(csv_path: Path, row: Dict, fieldnames: List[str]):
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    exists = csv_path.exists()
    with open(csv_path, "a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if not exists:
            writer.writeheader()
        writer.writerow(row)

def dump_log(log_path: Path, content: str):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8") as f:
        f.write(content)
