/* SPDX-License-Identifier: MIT */
/*
 * bench_common - v1-API-specific discovery helpers for the libiio benchmark
 * suite (context/board detection, device/attr lookup). Re-exports the
 * iio-agnostic timing/stats/JSON core from bench_measure.h so existing
 * callers only need one #include.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include "bench_measure.h"

struct iio_context;
struct iio_device;
struct iio_channels_mask;
struct iio_attr;

/* Fills opts->board from "hw_carrier"/"hw_model" context attrs, falling
 * back to the context description or "unknown". Call once after creating
 * the context, before any bench_report() calls. */
void bench_detect_board(struct iio_context *ctx, struct bench_opts *opts);

/* Finds the first device with input scan-element channels and a buffer,
 * enabling those channels in a freshly-allocated channels mask (returned
 * via 'mask_out', caller-owned). Returns NULL if none found. Same heuristic
 * as scratch/fast_checks.c's find_input_device(), so benchmarks work
 * against whatever board is plugged in. */
struct iio_device *bench_find_input_device(struct iio_context *ctx,
					    struct iio_channels_mask **mask_out);

/* Finds the first attribute named 'name', checking each device's own attrs
 * before its channels' attrs. Returns NULL if none found. Centralizes the
 * "search everything for a name" heuristic previously duplicated across
 * bench_attr.c and bench_streaming.c. */
const struct iio_attr *bench_find_attr_by_name(struct iio_context *ctx, const char *name);

#endif /* BENCH_COMMON_H */
