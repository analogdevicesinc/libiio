#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate a self-contained HTML dashboard trending libiio benchmark
results over time (one line chart per metric, across all recorded runs).

Usage:
    python3 benchmarks/dashboard.py [--results-dir benchmarks/results] \\
        [--output benchmarks/dashboard.html]

Then open the output file in a browser.
"""

import argparse
import glob
import json
import os


STAT_KEYS = ("min", "mean", "median", "p95", "max")

# (threshold in microseconds, divisor, display unit)
US_SCALES = (
    (60_000_000, 60_000_000, "min"),
    (1_000_000, 1_000_000, "s"),
    (1_000, 1_000, "ms"),
)


def metric_key(rec):
    """Identifies a metric across runs: 'name' alone, or 'name' plus
    block_size/ring_depth for the block-sweep benchmarks (which reuse the
    same name across several sizes/ring depths per run)."""
    if "block_size" in rec:
        return (rec["name"], rec["block_size"], rec.get("ring_depth"))
    return (rec["name"],)


def display_name(key):
    if len(key) == 1:
        return key[0]
    name, block_size, ring_depth = key
    return f"{name} ({block_size}B, ring{ring_depth})"


def pair_key(key):
    """enqueue/dequeue counterparts (e.g. "block_enqueue"/"block_dequeue"
    at the same block_size/ring_depth) map to the same key, so their
    charts always get scaled to the same unit and stay directly
    comparable."""
    name = key[0].replace("enqueue", "\0").replace("dequeue", "\0")
    return (name,) + key[1:]


def load_all_records(results_dir):
    """Returns {display_name: [record, ...]} sorted by timestamp, pooling
    every *.json file in results_dir (each is one run's header line plus
    JSONL result records)."""
    metrics = {}
    for path in sorted(glob.glob(os.path.join(results_dir, "*.json"))):
        meta = {}
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                if rec.get("type") == "meta":
                    meta = rec
                    continue
                # timestamp/git_sha live once per file (in meta); attach
                # them to each record so per-run trend points still carry
                # them.
                rec = dict(rec, timestamp=meta.get("timestamp"), git_sha=meta.get("git_sha"))
                metrics.setdefault(metric_key(rec), []).append(rec)

    for key in metrics:
        metrics[key].sort(key=lambda r: r.get("timestamp", ""))

    # Group metric keys sharing an enqueue/dequeue pair_key, so a common
    # scale can be picked from the larger of the two series.
    groups = {}
    for key, records in metrics.items():
        if records and records[0].get("unit") == "us":
            groups.setdefault(pair_key(key), []).append(key)

    for pkey, keys in groups.items():
        largest = max(
            r.get("max", r.get("mean", 0))
            for key in keys
            for r in metrics[key]
        )
        for threshold, divisor, unit in US_SCALES:
            if largest >= threshold:
                for key in keys:
                    metrics[key] = apply_scale(metrics[key], divisor, unit)
                break

    return {display_name(key): records for key, records in metrics.items()}


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


PAGE_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>libiio benchmark trends</title>
<style>
  :root {
    color-scheme: light;
    --surface-1:      #fcfcfb;
    --page-plane:     #f9f9f7;
    --text-primary:   #0b0b0b;
    --text-secondary: #52514e;
    --text-muted:     #898781;
    --gridline:       #e1e0d9;
    --baseline:       #c3c2b7;
    --border:         rgba(11,11,11,0.10);
    --series-1:       #2a78d6;
    --series-1-fill:  rgba(42,120,214,0.12);
    --good:           #006300;
    --critical:       #d03b3b;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      color-scheme: dark;
      --surface-1:      #1a1a19;
      --page-plane:     #0d0d0d;
      --text-primary:   #ffffff;
      --text-secondary: #c3c2b7;
      --text-muted:     #898781;
      --gridline:       #2c2c2a;
      --baseline:       #383835;
      --border:         rgba(255,255,255,0.10);
      --series-1:       #3987e5;
      --series-1-fill:  rgba(57,135,229,0.16);
      --good:           #0ca30c;
      --critical:       #e66767;
    }
  }
  :root[data-theme="dark"] {
    color-scheme: dark;
    --surface-1:      #1a1a19;
    --page-plane:     #0d0d0d;
    --text-primary:   #ffffff;
    --text-secondary: #c3c2b7;
    --text-muted:     #898781;
    --gridline:       #2c2c2a;
    --baseline:       #383835;
    --border:         rgba(255,255,255,0.10);
    --series-1:       #3987e5;
    --series-1-fill:  rgba(57,135,229,0.16);
    --good:           #0ca30c;
    --critical:       #e66767;
  }

  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 16px;
    background: var(--page-plane);
    color: var(--text-primary);
    font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
  }
  h1 { font-size: 20px; margin: 4px 0 2px; }
  .subtitle { color: var(--text-secondary); font-size: 13px; margin: 0 0 20px; }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 16px;
  }
  .card {
    background: var(--surface-1);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 14px 16px 10px;
    position: relative;
  }
  .card-head {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    margin-bottom: 6px;
  }
  .metric-name { font-size: 14px; font-weight: 600; }
  .metric-latest {
    font-size: 18px;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
  }
  .metric-delta { font-size: 12px; margin-left: 6px; font-variant-numeric: tabular-nums; }
  .delta-up { color: var(--critical); }
  .delta-down { color: var(--good); }
  .empty {
    color: var(--text-muted);
    font-size: 13px;
    padding: 30px 0;
    text-align: center;
  }
  svg.chart { display: block; width: 100%; height: 140px; overflow: visible; }
  .gridline { stroke: var(--gridline); stroke-width: 1; }
  .baseline { stroke: var(--baseline); stroke-width: 1; }
  .band { fill: var(--series-1-fill); }
  .line { fill: none; stroke: var(--series-1); stroke-width: 2; stroke-linecap: round; }
  .dot { fill: var(--surface-1); stroke: var(--series-1); stroke-width: 2; }
  .hit { fill: transparent; }
  .crosshair { stroke: var(--text-muted); stroke-width: 1; stroke-dasharray: 2,2; display: none; }
  .axis-label { fill: var(--text-muted); font-size: 10px; }
  .tooltip {
    position: fixed;
    pointer-events: none;
    background: var(--surface-1);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 6px 9px;
    font-size: 12px;
    color: var(--text-secondary);
    box-shadow: 0 2px 8px rgba(0,0,0,0.15);
    display: none;
    z-index: 10;
    white-space: nowrap;
  }
  .tooltip .val { color: var(--text-primary); font-weight: 600; font-variant-numeric: tabular-nums; }
  .toggle-table {
    background: none;
    border: none;
    color: var(--text-muted);
    font-size: 11px;
    cursor: pointer;
    padding: 0;
    margin-top: 4px;
  }
  .toggle-table:hover { color: var(--text-secondary); }
  table.raw {
    width: 100%;
    border-collapse: collapse;
    font-size: 11px;
    margin-top: 8px;
    display: none;
  }
  table.raw.shown { display: table; }
  table.raw th, table.raw td {
    text-align: right;
    padding: 3px 6px;
    border-bottom: 1px solid var(--gridline);
    font-variant-numeric: tabular-nums;
  }
  table.raw th:first-child, table.raw td:first-child { text-align: left; }
</style>
</head>
<body>
  <h1>libiio benchmark trends</h1>
  <p class="subtitle">__SUBTITLE__</p>
  <div class="grid" id="grid"></div>
  <div class="tooltip" id="tooltip"></div>

<script>
const DATA = __DATA_JSON__;

function fmt(v) {
  if (v >= 1000) return v.toLocaleString(undefined, {maximumFractionDigits: 0});
  return v.toLocaleString(undefined, {maximumFractionDigits: 2});
}

function buildCard(name, points) {
  const card = document.createElement('div');
  card.className = 'card';

  const unit = points[points.length - 1].unit;
  const latest = points[points.length - 1];
  const head = document.createElement('div');
  head.className = 'card-head';

  const nameEl = document.createElement('div');
  nameEl.className = 'metric-name';
  nameEl.textContent = name;
  head.appendChild(nameEl);

  const latestWrap = document.createElement('div');
  const latestVal = document.createElement('span');
  latestVal.className = 'metric-latest';
  latestVal.textContent = fmt(latest.mean) + ' ' + unit;
  latestWrap.appendChild(latestVal);

  if (points.length >= 2) {
    const prev = points[points.length - 2];
    const deltaPct = prev.mean === 0 ? 0 : ((latest.mean - prev.mean) / prev.mean) * 100;
    const higherIsWorse = !unit.endsWith('/s');
    const isRegression = higherIsWorse ? deltaPct > 0 : deltaPct < 0;
    const deltaEl = document.createElement('span');
    deltaEl.className = 'metric-delta ' + (Math.abs(deltaPct) < 0.05 ? '' : (isRegression ? 'delta-up' : 'delta-down'));
    deltaEl.textContent = (deltaPct >= 0 ? '+' : '') + deltaPct.toFixed(1) + '%';
    latestWrap.appendChild(deltaEl);
  }
  head.appendChild(latestWrap);
  card.appendChild(head);

  if (points.length < 2) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = points.length === 1
      ? 'Only one run recorded so far — need at least two to show a trend.'
      : 'No data.';
    card.appendChild(empty);
    return card;
  }

  const svgNS = 'http://www.w3.org/2000/svg';
  const W = 600, H = 140, padL = 4, padR = 4, padT = 10, padB = 18;
  const svg = document.createElementNS(svgNS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
  svg.setAttribute('class', 'chart');
  svg.setAttribute('preserveAspectRatio', 'none');

  const allMin = Math.min(...points.map(p => p.min !== undefined ? p.min : p.mean));
  const allMax = Math.max(...points.map(p => p.max !== undefined ? p.max : p.mean));
  const range = (allMax - allMin) || 1;
  const yFor = v => padT + (H - padT - padB) * (1 - (v - allMin) / range);
  const xFor = i => padL + (W - padL - padR) * (points.length === 1 ? 0.5 : i / (points.length - 1));

  // gridlines (3 horizontal)
  for (let g = 0; g <= 2; g++) {
    const y = padT + (H - padT - padB) * (g / 2);
    const line = document.createElementNS(svgNS, 'line');
    line.setAttribute('x1', padL); line.setAttribute('x2', W - padR);
    line.setAttribute('y1', y); line.setAttribute('y2', y);
    line.setAttribute('class', 'gridline');
    svg.appendChild(line);
  }

  // min/max band
  const bandTop = points.map((p, i) => `${xFor(i)},${yFor(p.max !== undefined ? p.max : p.mean)}`);
  const bandBot = points.slice().reverse().map((p, i) => {
    const idx = points.length - 1 - i;
    return `${xFor(idx)},${yFor(p.min !== undefined ? p.min : p.mean)}`;
  });
  const band = document.createElementNS(svgNS, 'polygon');
  band.setAttribute('points', bandTop.concat(bandBot).join(' '));
  band.setAttribute('class', 'band');
  svg.appendChild(band);

  // mean line
  const linePath = points.map((p, i) => `${i === 0 ? 'M' : 'L'} ${xFor(i)} ${yFor(p.mean)}`).join(' ');
  const line = document.createElementNS(svgNS, 'path');
  line.setAttribute('d', linePath);
  line.setAttribute('class', 'line');
  svg.appendChild(line);

  // crosshair
  const crosshair = document.createElementNS(svgNS, 'line');
  crosshair.setAttribute('y1', padT); crosshair.setAttribute('y2', H - padB);
  crosshair.setAttribute('class', 'crosshair');
  svg.appendChild(crosshair);

  // dots + hit targets
  points.forEach((p, i) => {
    const dot = document.createElementNS(svgNS, 'circle');
    dot.setAttribute('cx', xFor(i)); dot.setAttribute('cy', yFor(p.mean));
    dot.setAttribute('r', 3);
    dot.setAttribute('class', 'dot');
    svg.appendChild(dot);
  });

  const hit = document.createElementNS(svgNS, 'rect');
  hit.setAttribute('x', 0); hit.setAttribute('y', 0);
  hit.setAttribute('width', W); hit.setAttribute('height', H);
  hit.setAttribute('class', 'hit');
  svg.appendChild(hit);

  const tooltip = document.getElementById('tooltip');

  function showTooltip(clientX, clientY, idx) {
    const p = points[idx];
    crosshair.style.display = 'block';
    crosshair.setAttribute('x1', xFor(idx));
    crosshair.setAttribute('x2', xFor(idx));

    tooltip.innerHTML = '';
    const valLine = document.createElement('div');
    const strong = document.createElement('span');
    strong.className = 'val';
    strong.textContent = fmt(p.mean) + ' ' + p.unit + ' mean';
    valLine.appendChild(strong);
    tooltip.appendChild(valLine);

    const meta = document.createElement('div');
    meta.textContent = `min ${fmt(p.min)} / p95 ${fmt(p.p95)} / max ${fmt(p.max)}`;
    tooltip.appendChild(meta);

    const info = document.createElement('div');
    info.textContent = `${p.timestamp || ''}  ${p.git_sha || ''}`;
    tooltip.appendChild(info);

    tooltip.style.display = 'block';
    tooltip.style.left = Math.min(clientX + 12, window.innerWidth - 220) + 'px';
    tooltip.style.top = Math.max(clientY - 50, 8) + 'px';
  }

  function hideTooltip() {
    crosshair.style.display = 'none';
    tooltip.style.display = 'none';
  }

  svg.addEventListener('pointermove', (ev) => {
    const rect = svg.getBoundingClientRect();
    const relX = (ev.clientX - rect.left) / rect.width * W;
    let nearest = 0, best = Infinity;
    points.forEach((p, i) => {
      const d = Math.abs(xFor(i) - relX);
      if (d < best) { best = d; nearest = i; }
    });
    showTooltip(ev.clientX, ev.clientY, nearest);
  });
  svg.addEventListener('pointerleave', hideTooltip);

  card.appendChild(svg);

  const toggle = document.createElement('button');
  toggle.className = 'toggle-table';
  toggle.textContent = 'Show raw data';
  const table = document.createElement('table');
  table.className = 'raw';
  table.innerHTML = '<thead><tr><th>Run</th><th>Mean</th><th>Min</th><th>Max</th><th>P95</th></tr></thead>';
  const tbody = document.createElement('tbody');
  points.forEach(p => {
    const tr = document.createElement('tr');
    const cells = [
      (p.git_sha || '') + ' ' + (p.timestamp || ''),
      fmt(p.mean), fmt(p.min), fmt(p.max), fmt(p.p95),
    ];
    cells.forEach((c, i) => {
      const td = document.createElement('td');
      td.textContent = c;
      tr.appendChild(td);
    });
    tbody.appendChild(tr);
  });
  table.appendChild(tbody);
  toggle.addEventListener('click', () => {
    const shown = table.classList.toggle('shown');
    toggle.textContent = shown ? 'Hide raw data' : 'Show raw data';
  });
  card.appendChild(toggle);
  card.appendChild(table);

  return card;
}

const grid = document.getElementById('grid');
const names = Object.keys(DATA).sort();
if (names.length === 0) {
  grid.innerHTML = '<div class="empty">No benchmark results found. Run benchmarks/run_all.sh first.</div>';
} else {
  names.forEach(name => grid.appendChild(buildCard(name, DATA[name])));
}
</script>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", default=os.path.join(os.path.dirname(__file__), "results"))
    parser.add_argument("--output", default=os.path.join(os.path.dirname(__file__), "dashboard.html"))
    args = parser.parse_args()

    metrics = load_all_records(args.results_dir)

    total_runs = len({r.get("timestamp") for pts in metrics.values() for r in pts})
    subtitle = f"{len(metrics)} metrics across {total_runs} run(s), from {args.results_dir}"

    html = PAGE_TEMPLATE.replace("__DATA_JSON__", json.dumps(metrics))
    html = html.replace("__SUBTITLE__", subtitle)

    with open(args.output, "w") as f:
        f.write(html)

    print(f"Dashboard written to {args.output}")


if __name__ == "__main__":
    main()
