#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared result-file parsing/grouping helpers for report.py, compare.py,
and dashboard.py, so the JSONL schema (see SCHEMA.md) is only understood
in one place.
"""

import json
import sys


# Newest schema_version this module understands. Bump alongside
# BENCH_SCHEMA_VERSION in bench_common.c and SCHEMA_VERSION in
# bench_cli.sh - see benchmarks/SCHEMA.md.
CURRENT_SCHEMA_VERSION = 2

STAT_KEYS = ("min", "mean", "median", "p95", "max")

# (threshold in microseconds, divisor, display unit)
US_SCALES = (
    (60_000_000, 60_000_000, "min"),
    (1_000_000, 1_000_000, "s"),
    (1_000, 1_000, "ms"),
)


def warn_schema_version(meta, path):
    """Warns (doesn't raise) if 'meta' carries no schema_version, or one
    newer than this module knows about - either way, records may not be
    shaped the way this module expects them to be. An older-than-current
    version isn't warned about: schema_version was field-additive so far,
    and old records still parse fine."""
    version = meta.get("schema_version")
    if version is None:
        print(f"warning: {path}: no schema_version in meta line "
              f"(pre-versioning result file, see SCHEMA.md)", file=sys.stderr)
    elif version > CURRENT_SCHEMA_VERSION:
        print(f"warning: {path}: schema_version {version} is newer than "
              f"this tool understands ({CURRENT_SCHEMA_VERSION}), "
              f"records may not parse as expected", file=sys.stderr)


def metric_key(rec):
    """Identifies a metric within/across runs: 'name' alone, plus
    block_size/ring_depth for the block-sweep benchmarks, or plus
    'samples' for the cli_iio_rwdev_read size sweep - both reuse the same
    'name' across several sizes/ring depths per run, and would otherwise
    collapse into one bucket."""
    if "block_size" in rec:
        return (rec["name"], rec["block_size"], rec.get("ring_depth"))
    if "samples" in rec:
        return (rec["name"], rec["samples"])
    return (rec["name"],)


def display_name(rec_or_key):
    """Accepts either a record (report.py/compare.py) or a bare metric_key
    tuple (dashboard.py, which keys purely off metric_key before records
    are attached)."""
    if isinstance(rec_or_key, tuple):
        key = rec_or_key
        if len(key) == 1:
            return key[0]
        if len(key) == 2:
            # Only the "samples" case produces a 2-tuple; block_size always
            # produces a 3-tuple (block_size, ring_depth), even when
            # ring_depth is None.
            name, samples = key
            return f"{name} ({samples} samples)"
        name, block_size, ring_depth = key
        return f"{name} ({block_size}B, ring{ring_depth})"

    rec = rec_or_key
    if "block_size" in rec:
        return f"{rec['name']} ({rec['block_size']}B, ring{rec.get('ring_depth', '?')})"
    if "samples" in rec:
        return f"{rec['name']} ({rec['samples']} samples)"
    return rec["name"]


def pair_key(key):
    """enqueue/dequeue counterparts (e.g. "block_enqueue"/"block_dequeue"
    at the same block_size/ring_depth) map to the same key, so they always
    get scaled to the same unit and stay directly comparable."""
    name = key[0].replace("enqueue", "\0").replace("dequeue", "\0")
    return (name,) + key[1:]


def apply_scale(records, divisor, unit):
    scaled = []
    for r in records:
        s = dict(r)
        for key in STAT_KEYS:
            if key in s:
                s[key] = s[key] / divisor
        s["unit"] = unit
        scaled.append(s)
    return scaled


def load_run(path):
    """Returns (meta, records): meta is the run's header object (the first
    line, {"type": "meta", ...}, or {} if absent), records is every result
    line after it, in file order. Warns on a missing/future schema_version."""
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
    warn_schema_version(meta, path)
    return meta, records


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
