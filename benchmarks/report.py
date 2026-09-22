#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pretty-print a benchmark run's JSONL output as an aligned table.

Usage:
    python3 benchmarks/report.py benchmarks/results/<run>.json
"""

import argparse
import json
import sys


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

STAT_KEYS = ("min", "mean", "median", "p95", "max")

# (threshold in microseconds, divisor, display unit)
US_SCALES = (
    (60_000_000, 60_000_000, "min"),
    (1_000_000, 1_000_000, "s"),
    (1_000, 1_000, "ms"),
)


def metric_key(rec):
    """Identifies a metric within a run: 'name' alone, or 'name' plus
    block_size/ring_depth for the block-sweep benchmarks (which reuse the
    same name across several sizes/ring depths per run)."""
    if "block_size" in rec:
        return (rec["name"], rec["block_size"], rec.get("ring_depth"))
    return (rec["name"],)


def pair_key(key):
    """enqueue/dequeue counterparts (e.g. "block_enqueue"/"block_dequeue"
    at the same block_size/ring_depth) map to the same key, so they always
    get scaled to the same unit and stay directly comparable in the
    table."""
    name = key[0].replace("enqueue", "\0").replace("dequeue", "\0")
    return (name,) + key[1:]


def display_name(rec):
    if "block_size" in rec:
        return f"{rec['name']} ({rec['block_size']}B, ring{rec.get('ring_depth', '?')})"
    return rec["name"]


def scale_records(records):
    """Rewrites each "us"-unit record's stats + unit to the most readable
    scale (min/s/ms/us). enqueue/dequeue pairs are scaled together, using
    whichever of the two has the larger value, so they never end up shown
    in different units."""
    groups = {}
    for rec in records:
        if rec.get("unit") == "us":
            groups.setdefault(pair_key(metric_key(rec)), []).append(rec)

    scale_for = {}
    for key, group in groups.items():
        largest = max(r.get("max", r.get("mean", 0)) for r in group)
        for threshold, divisor, unit in US_SCALES:
            if largest >= threshold:
                scale_for[key] = (divisor, unit)
                break

    scaled = []
    for rec in records:
        key = pair_key(metric_key(rec)) if rec.get("unit") == "us" else None
        if key in scale_for:
            divisor, unit = scale_for[key]
            rec = dict(rec)
            for stat_key in STAT_KEYS:
                if stat_key in rec:
                    rec[stat_key] = rec[stat_key] / divisor
            rec["unit"] = unit
        scaled.append(rec)
    return scaled


def load_run(path):
    """Returns (meta, records): meta is the run's header object (the first
    line, {"type": "meta", ...}), records is every result line after it."""
    meta = {}
    records = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("type") == "meta":
                meta = rec
            else:
                records.append(rec)
    return meta, records


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

    print(f"host: {meta.get('host', '?')}   uri: {meta.get('uri', '?')}   "
          f"git_sha: {meta.get('git_sha', '?')}   timestamp: {meta.get('timestamp', '?')}")
    print()

    def print_row(cells):
        print("  ".join(cell.ljust(widths[i]) for i, cell in enumerate(cells)))

    print_row(headers)
    print_row(["-" * w for w in widths])
    for row in rows:
        print_row(row)


if __name__ == "__main__":
    main()
