#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pretty-print a benchmark run's JSONL output as an aligned table.

Usage:
    python3 benchmarks/report.py benchmarks/results/<run>.json
"""

import argparse
import sys

from bench_results import display_name, load_run, scale_records


COLUMNS = [
    ("display_name", "Metric"),
    ("unit", "Unit"),
    ("count", "N"),
    ("min", "Min"),
    ("mean", "Mean"),
    ("median", "Median"),
    ("p95", "P95"),
    ("max", "Max"),
]


def fmt(value):
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result", help="Path to a benchmark results .json (JSONL) file")
    args = parser.parse_args()

    try:
        meta, records = load_run(args.result)
    except FileNotFoundError:
        print(f"error: no such file: {args.result}", file=sys.stderr)
        sys.exit(1)

    if not records:
        print("No records found.")
        return

    records = scale_records(records)
    for rec in records:
        rec["display_name"] = display_name(rec)
    rows = [[fmt(rec.get(key, "")) for key, _ in COLUMNS] for rec in records]
    headers = [label for _, label in COLUMNS]
    widths = [max(len(headers[i]), *(len(row[i]) for row in rows)) for i in range(len(COLUMNS))]

    header = (f"host: {meta.get('host', '?')}   uri: {meta.get('uri', '?')}   "
              f"git_sha: {meta.get('git_sha', '?')}   timestamp: {meta.get('timestamp', '?')}")
    tags = meta.get("tags")
    if tags:
        header += "   tags: " + ", ".join(f"{k}={v}" for k, v in tags.items())
    print(header)
    print()

    def print_row(cells):
        print("  ".join(cell.ljust(widths[i]) for i, cell in enumerate(cells)))

    print_row(headers)
    print_row(["-" * w for w in widths])
    for row in rows:
        print_row(row)


if __name__ == "__main__":
    main()
