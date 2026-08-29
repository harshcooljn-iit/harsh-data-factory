#!/usr/bin/env python3
"""Combine every CSV in data/ into one file, keeping a single header."""
import csv, glob, sys
from pathlib import Path

def main() -> int:
    out_path = sys.argv[1]
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    files = sorted(glob.glob("data/*.csv"))
    if not files:
        print("merge: no CSV files in data/", file=sys.stderr)
        return 1
    rows = 0
    with open(out_path, "w", newline="", encoding="utf-8") as out:
        writer = None
        for path in files:
            with open(path, newline="", encoding="utf-8") as f:
                reader = csv.reader(f)
                header = next(reader)
                if writer is None:
                    writer = csv.writer(out)
                    writer.writerow(header)
                for row in reader:
                    writer.writerow(row)
                    rows += 1
    print(f"merge: combined {len(files)} files, {rows} rows -> {out_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())