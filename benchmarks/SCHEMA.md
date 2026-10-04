# Benchmark result schema

Every benchmark writes newline-delimited JSON (JSONL): one optional meta
line, followed by one record per reported metric.

This file documents the *current* shape only. If you change what a line
looks like (add/remove/rename a field, or add a new record shape), bump
`schema_version` and update this file in the same commit. There is no
migration path for old files — the version field only lets readers warn
instead of silently misparsing.

## Meta line

Written once per output (first line of a file, or the first line printed
to stdout for a single run), by both the C side
(`bench_common.c:write_meta_header()`) and the shell side
(`bench_cli.sh:write_meta_header_if_needed()`), independently. **The two
implementations must be kept in sync by hand** — there's no shared code
between C and shell here (a deliberate choice, to keep `bench_cli.sh`
dependency-light), so a schema change must be applied in both places.

```json
{"type":"meta","schema_version":2,"timestamp":"2026-09-22T13:12:34Z","git_sha":"6282c1b4","host":"myhost","board":"unknown","uri":"ip:192.168.2.1"}
```

With `--tag key=value` (repeatable):

```json
{"type":"meta","schema_version":2, ..., "tags":{"protocol":"v0"}}
```

## Record line

One per reported metric:

```json
{"name":"context_create","unit":"us","count":1000,"min":95334.602,"max":115878.909,"mean":103864.280,"median":103485.946,"p95":115878.909}
```

Optional extra fields on some records:
- `"block_size"` / `"ring_depth"` — bench_block's ring-sweep records.
- `"samples"` — bench_cli.sh's `cli_iio_rwdev_read` sweep records, to
  disambiguate several records sharing the same `"name"`.

## Version history

- **2** (current) — added `"schema_version"` to the meta line. No record
  shape change.
- **1** (implicit, never actually written as a number) — the shape
  produced by the first `feature/benchmarks-suite` commits: meta line
  present, no `"schema_version"` field, `attr_write_driver` naming,
  split `context_create`/`context_destroy` metrics.
- **unversioned/pre-1** — even older local result files (not part of the
  repo) may have no meta line at all, a `"stdev"` field, and a combined
  `context_create_destroy` metric. Not readable by the current tooling;
  regenerate them instead of trying to migrate.

## Consumers

`report.py`, `compare.py`, and `dashboard.py` should warn (not crash) on
a missing or unrecognized `schema_version`, rather than silently
misparsing an old-shape file.
