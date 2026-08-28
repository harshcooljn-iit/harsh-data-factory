#!/usr/bin/env python3
"""python_pipeline step 1: drop blank / malformed rows from a CSV.

usage: clean.py <input.csv> <output.csv>
"""
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: clean.py <input.csv> <output.csv>", file=sys.stderr)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    kept = 0
    with open(src, encoding="utf-8") as fin, open(dst, "w", encoding="utf-8") as fout:
        header = fin.readline()
        fout.write(header)
        for line in fin:
            parts = line.rstrip("\n").split(",")
            if len(parts) != 2:
                continue
            name, value = parts[0].strip(), parts[1].strip()
            if not name or not value.isdigit():
                continue
            fout.write(f"{name},{value}\n")
            kept += 1

    print(f"clean.py: kept {kept} valid rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
