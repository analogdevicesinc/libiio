# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'common' / 'pytest'))

from iiod_fixtures import iiod_context, pytest_addoption  # noqa: E402, F401
