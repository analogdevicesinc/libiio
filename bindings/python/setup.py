#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2014-2026 Analog Devices, Inc.
#
# Standalone packaging setup for the libiio Python bindings.
# Used by the Debian packaging (packaging/debian) to build python3-libiio
# independently of the CMake build. The CMake build still uses
# setup.py.cmakein.

from setuptools import setup

with open("README.md", "r") as fh:
    long_description = fh.read()

setup(
    name="pylibiio",
    version="0.26",
    maintainer="Analog Devices, Inc",
    description="Library for interfacing with Linux IIO devices",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/analogdevicesinc/libiio",
    py_modules=["iio"],
    license="LGPL-2.1-or-later",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: GNU Lesser General Public License v2 or later (LGPLv2+)",
        "Operating System :: OS Independent",
    ],
)
