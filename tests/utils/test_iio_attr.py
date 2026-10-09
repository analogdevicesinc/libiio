#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check payload integrity and observable CLI failures with a mocked writer."""
import os
from pathlib import Path
# Required to exercise the validated built CLI; subprocess calls never use a shell.
import subprocess  # nosec B404
import sys
import tempfile
import unittest

# CTest supplies the built test target. Reject missing files and unexpected programs.
executable_path = Path(sys.argv[1]).resolve(strict=True)
if executable_path.name not in ("test_iio_attr_cli", "test_iio_attr_cli.exe"):
    raise ValueError("Expected the built test_iio_attr_cli executable")
if not executable_path.is_file() or not os.access(executable_path, os.X_OK):
    raise ValueError("Test target is not an executable file")
EXECUTABLE = str(executable_path)
checks = unittest.TestCase()
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    fixture = Path(__file__).with_name("iio_attr.xml")
    capture = root / "capture.bin"
    payload_file = root / "filter.ftr"
    routes = [
        ["-d", "test-device", "filter_fir_config"],
        ["-c", "test-device", "voltage0", "filter_fir_config"],
        ["-B", "-b", "0", "test-device", "filter_fir_config"],
    ]
    cases = 0
    for route in routes:
        # Cover the old filename-length boundary and realistically sized FIR text.
        for size in (0, 254, 255, 256, 257, 4096, 16385):
            value = ("RX 3 GAIN 0 DEC 1\n" + "-12345,23456\n" * 1400)[:size]
            command = [
                EXECUTABLE, "full", str(capture), "-u", f"xml:{fixture}", "-w", "-q", *route, value
            ]
            # Validated CTest target and literal argument list; no shell interpretation.
            completed = subprocess.run(  # nosec B603
                command, check=False, shell=False, capture_output=True
            )
            checks.assertEqual(completed.returncode, 0, (command, completed.stderr))
            checks.assertEqual(capture.read_bytes(), value.encode() + b"\0", (route, size))
            cases += 1
        for from_file in (False, True):
            value = "RX 3 GAIN 0 DEC 1\n" + "-12345,23456\n" * 32
            payload_file.write_text(value)
            command = [EXECUTABLE, "full", str(capture), "-u", f"xml:{fixture}", "-w", "-q"]
            if from_file:
                command.append("-f")
            command.extend([*route, str(payload_file) if from_file else value])
            expected = value.encode() + (b"" if from_file else b"\0")
            for result in ("full", "short", "zero", "error"):
                command[1] = result
                # Redirect stdout as in a pipeline: errors must remain on stderr.
                with (root / "stdout.txt").open("wb") as output:
                    # Same validated test target; check=False lets us inspect expected failures.
                    completed = subprocess.run(  # nosec B603
                        command, check=False, shell=False, stdout=output, stderr=subprocess.PIPE
                    )
                checks.assertEqual(capture.read_bytes(), expected, (route, from_file, result))
                if result == "full":
                    checks.assertEqual(completed.returncode, 0, completed.stderr)
                    checks.assertEqual(completed.stderr, b"", completed.stderr)
                else:
                    checks.assertNotEqual(completed.returncode, 0, (route, from_file, result))
                    checks.assertIn(b"filter_fir_config", completed.stderr)
                    if result in ("short", "zero"):
                        accepted = len(expected) - 1 if result == "short" else 0
                        detail = f"{accepted} of {len(expected)} bytes accepted".encode()
                        checks.assertIn(detail, completed.stderr)
                    else:
                        checks.assertIn(b"Unable to write attribute", completed.stderr)
                cases += 1
    print(f"Passed {cases} CLI write checks (device, channel, buffer; string and file).")
