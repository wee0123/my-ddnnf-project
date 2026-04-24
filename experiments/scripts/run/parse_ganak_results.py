import csv
import re
from pathlib import Path

RESULT_DIR = Path.home() / "results/minimal/timing/ganak_total"
TIME_DIR = Path.home() / "results/minimal/logs/ganak_total"
OUT_CSV = Path.home() / "results/minimal/summary/ganak_total_summary.csv"

def parse_count(text: str):
    patterns = [
        r'Number of solutions.*?([0-9]+)',
        r'c s exact arb int\s+([0-9]+)',
        r'^s\s+([0-9]+)$',
        r'^\s*([0-9]+)\s*$',
    ]
    lines = text.splitlines()

    for line in lines:
        for pat in patterns:
            m = re.search(pat, line)
            if m:
                return m.group(1)

    for line in reversed(lines):
        nums = re.findall(r'[0-9]+', line)
        if nums:
            return nums[-1]

    return ""

def parse_time_file(path: Path):
    result = {"real": "", "user": "", "sys": "", "mem_kb": ""}
    if not path.exists():
        return result
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            result[k.strip()] = v.strip()
    return result

def main():
    rows = []
    for out_file in sorted(RESULT_DIR.glob("*.out")):
        name = out_file.stem
        text = out_file.read_text(encoding="utf-8", errors="ignore")
        count = parse_count(text)
        time_info = parse_time_file(TIME_DIR / f"{name}.time")

        rows.append({
            "name": name,
            "ganak_count": count,
            "real_s": time_info["real"],
            "user_s": time_info["user"],
            "sys_s": time_info["sys"],
            "mem_kb": time_info["mem_kb"],
        })

    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with OUT_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["name", "ganak_count", "real_s", "user_s", "sys_s", "mem_kb"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"Generated: {OUT_CSV}")
    print(f"Parsed results: {len(rows)}")

if __name__ == "__main__":
    main()