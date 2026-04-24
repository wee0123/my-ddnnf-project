import csv
import sys
from pathlib import Path

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
    if len(sys.argv) < 3:
        print("Usage: parse_run_times.py <tool_name> <time_dir> [out_csv]")
        sys.exit(1)

    tool_name = sys.argv[1]
    time_dir = Path(sys.argv[2])

    if len(sys.argv) >= 4:
        out_csv = Path(sys.argv[3])
    else:
        out_csv = Path.home() / f"results/timing/{tool_name}_summary.csv"

    rows = []
    for time_file in sorted(time_dir.glob("*.time")):
        name = time_file.stem
        info = parse_time_file(time_file)
        rows.append({
            "name": name,
            f"{tool_name}_real_s": info["real"],
            f"{tool_name}_user_s": info["user"],
            f"{tool_name}_sys_s": info["sys"],
            f"{tool_name}_mem_kb": info["mem_kb"],
        })

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "name",
        f"{tool_name}_real_s",
        f"{tool_name}_user_s",
        f"{tool_name}_sys_s",
        f"{tool_name}_mem_kb",
    ]

    with out_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Generated: {out_csv}")
    print(f"Parsed results: {len(rows)}")

if __name__ == "__main__":
    main()