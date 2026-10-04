#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare two benchmark result JSON files (from run_all.sh) and report
per-metric deltas, flagging regressions past a threshold.

Usage:
    python3 benchmarks/compare.py before.json after.json [--threshold 10]
"""

import argparse
import json
import sys


def metric_key(rec):
    """Identifies a metric within a run: 'name' alone, or 'name' plus
    block_size/ring_depth for the block-sweep benchmarks (which reuse the
    same name across several sizes/ring depths per run)."""
    if "block_size" in rec:
        return (rec["name"], rec["block_size"], rec.get("ring_depth"))
    return (rec["name"],)


def display_name(rec):
    if "block_size" in rec:
        return f"{rec['name']} ({rec['block_size']}B, ring{rec.get('ring_depth', '?')})"
    return rec["name"]


def load_records(path):
    records = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("type") == "meta":
                continue
            records[metric_key(rec)] = rec
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", help="Baseline result JSON (one record per line)")
    parser.add_argument("after", help="New result JSON (one record per line)")
    parser.add_argument("--threshold", type=float, default=10.0,
                         help="Percent delta considered a regression (default: 10)")
    args = parser.parse_args()

    before = load_records(args.before)
    after = load_records(args.after)

    keys = sorted(set(before) | set(after))
    any_regression = False

    header = f"{'metric':<30}{'unit':<8}{'before':>12}{'after':>12}{'delta %':>10}"
    print(header)
    print("-" * len(header))

    for key in keys:
        b = before.get(key)
        a = after.get(key)
        name = display_name(b or a)
        if not b or not a:
            print(f"{name:<30}{'':<8}{'missing':>12}{'':>12}{'':>10}")
            continue

        # Lower is better for latency (us); higher is better for throughput.
        is_throughput = a["unit"].endswith("/s")
        b_mean, a_mean = b["mean"], a["mean"]

        if b_mean == 0:
            delta_pct = float("inf") if a_mean != 0 else 0.0
        else:
            delta_pct = (a_mean - b_mean) / b_mean * 100.0

        regressed = (delta_pct > args.threshold) if not is_throughput \
            else (delta_pct < -args.threshold)
        flag = " REGRESSION" if regressed else ""
        if regressed:
            any_regression = True

        print(f"{name:<30}{a['unit']:<8}{b_mean:>12.3f}{a_mean:>12.3f}"
              f"{delta_pct:>9.1f}%{flag}")

    sys.exit(1 if any_regression else 0)


if __name__ == "__main__":
    main()
