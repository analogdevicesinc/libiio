/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_CONFIG_H
#define IIO_CONFIG_H

/* Only define the IF_ENABLED macro that cannot be defined via CMake */
/* All other configuration macros are defined in CMakeLists.txt */
#define IIO_IF_ENABLED(cfg, ptr) ((cfg) ? (ptr) : NULL)

#endif /* IIO_CONFIG_H */
