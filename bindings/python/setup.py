#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2026 Analog Devices, Inc.
#
# Standalone packaging setup for the libiio Python bindings.
# Used by the Debian packaging (packaging/debian) to build python3-libiio
# independently of the CMake build. The CMake build still uses
# setup.py.cmakein.

import re

from setuptools import setup

with open("README.md", "r") as fh:
    long_description = fh.read()

with open("../../CMakeLists.txt", "r") as fh:
    cmake = fh.read()
version = ".".join(
    re.search(r"LIBIIO_VERSION_%s (\d+)" % k, cmake).group(1) for k in ("MAJOR", "MINOR")
)

setup(
    name="pylibiio",
    version=version,
    maintainer="Analog Devices, Inc",
    maintainer_email="travis.collins@analog.com",
    description="Library for interfacing with Linux IIO devices",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/analogdevicesinc/libiio",
    py_modules=["iio"],
    license="LGPL-2.1-or-later",
    classifiers=[
        "Programming Language :: Python :: 3",
        "Operating System :: OS Independent",
    ],
)
