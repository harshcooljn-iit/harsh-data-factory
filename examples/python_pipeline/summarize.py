#!/usr/bin/env python3
"""python_pipeline step 2: summary statistics over the cleaned CSV.

usage: summarize.py <clean.csv> <summary.txt>
"""
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: summarize.py <clean.csv> <summary.txt>", file=sys.stderr)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    values = []
    with open(src, encoding="utf-8") as fin:
        fin.readline()  # header
        for line in fin:
            parts = line.rstrip("\n").split(",")
            if len(parts) == 2 and parts[1].isdigit():
                values.append(int(parts[1]))

    if not values:
        print("summarize.py: no data", file=sys.stderr)
        return 1

    total = sum(values)
    with open(dst, "w", encoding="utf-8") as fout:
        fout.write(f"count={len(values)}\n")
        fout.write(f"sum={total}\n")
        fout.write(f"min={min(values)}\n")
        fout.write(f"max={max(values)}\n")
        fout.write(f"mean={total / len(values):.2f}\n")

    print(f"summarize.py: wrote summary for {len(values)} values")
    return 0


if __name__ == "__main__":
    sys.exit(main())
