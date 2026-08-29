#!/usr/bin/env python3
"""Drop rows whose amount is missing or non-numeric."""
import csv, sys

def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    kept = dropped = 0
    with open(src, newline="", encoding="utf-8") as fin, \
         open(dst, "w", newline="", encoding="utf-8") as fout:
        reader = csv.reader(fin)
        writer = csv.writer(fout)
        writer.writerow(next(reader))                       # header
        for row in reader:
            if len(row) != 2 or not row[1].strip().isdigit():
                dropped += 1
                continue
            writer.writerow([row[0].strip(), row[1].strip()])
            kept += 1
    print(f"clean: kept {kept}, dropped {dropped}")
    return 0

if __name__ == "__main__":
    sys.exit(main())