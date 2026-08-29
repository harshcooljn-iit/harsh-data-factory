#!/usr/bin/env python3
"""Combine the cleaned data and the stats into a readable report."""
import csv, sys
from pathlib import Path

def main() -> int:
    clean_csv, stats_txt, out_txt = sys.argv[1], sys.argv[2], sys.argv[3]
    Path(out_txt).parent.mkdir(parents=True, exist_ok=True)
    by_region = {}
    with open(clean_csv, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        next(reader)
        for region, amount in reader:
            by_region[region] = by_region.get(region, 0) + int(amount)
    with open(out_txt, "w", encoding="utf-8") as out:
        out.write("SALES REPORT\n============\n\nBy region:\n")
        for region in sorted(by_region):
            out.write(f"  {region:6s} {by_region[region]}\n")
        out.write("\nOverall:\n")
        out.write(Path(stats_txt).read_text())
    print(f"report: wrote {out_txt}")
    return 0

if __name__ == "__main__":
    sys.exit(main())