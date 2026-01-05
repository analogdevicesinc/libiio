# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

zephyr_syscall_header(${ZEPHYR_LIBIIO_MODULE_DIR}/zephyr/include/iio_device.h)

zephyr_linker_sources(SECTIONS iio-device-rom.ld)
zephyr_iterable_section(NAME iio_device_info KVMA RAM_REGION GROUP RODATA_REGION)
