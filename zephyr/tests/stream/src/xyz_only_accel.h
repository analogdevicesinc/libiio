/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XYZ_ONLY_ACCEL_H_
#define XYZ_ONLY_ACCEL_H_

#include <zephyr/dsp/types.h>
#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

/* Frames per capture; values are distinct per frame and axis. */
#define XYZ_ONLY_FRAMES 3
#define XYZ_ONLY_SHIFT  5
#define XYZ_ONLY_XYZ(frame, axis) \
	((q31_t)(0x10000000 + (frame) * 0x01000000 + (axis) * 0x00040000))

/* XYZ_ONLY_XYZ() in milli-units, computed independently of the backend. */
#define XYZ_ONLY_MILLI(frame, axis)                                             \
	((int32_t)((((int64_t)XYZ_ONLY_XYZ(frame, axis) * 1000LL) +             \
		    (INT64_C(1) << ((31 - XYZ_ONLY_SHIFT) - 1))) >>              \
		   (31 - XYZ_ONLY_SHIFT)))

/**
 * @brief Delay every subsequent capture by @p ms before completing it.
 *
 * Zero, the default, completes inside submit(). A non-zero delay lets a test
 * keep a dequeue blocked on a pending completion, e.g. to cancel it.
 */
void xyz_only_set_capture_delay(uint32_t ms);

/** @brief True while a delayed capture has not completed. */
bool xyz_only_capture_pending(void);

/** @brief Fail a delayed capture now, so it cannot complete in a later test. */
void xyz_only_flush(void);

/**
 * @brief Wait until a delayed capture has been submitted.
 *
 * @return 0, or -EAGAIN if none was submitted within @p timeout
 */
int xyz_only_await_submit(k_timeout_t timeout);

#endif /* XYZ_ONLY_ACCEL_H_ */
