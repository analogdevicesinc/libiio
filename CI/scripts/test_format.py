#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise format.sh's exit status and file selection in temporary repositories."""

from contextlib import ExitStack
import json
import os
from pathlib import Path
import shutil
# Tests execute resolved local tools with argument lists, without a shell.
import subprocess  # nosec B404
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BASH = shutil.which("bash")
GIT = shutil.which("git")


class FormatScriptTests(unittest.TestCase):
    """Check formatting failures and helper isolation in disposable repositories."""

    def setUp(self):
        """Create tracked fixtures and mock formatters for an isolated test."""
        resources = ExitStack()
        self.addCleanup(resources.close)
        root = Path(resources.enter_context(
            tempfile.TemporaryDirectory(prefix="libiio-format-test-")
        ))
        self.repo = root / "repo"
        self.repo.mkdir()
        self.bin = root / "bin"
        self.bin.mkdir()
        self.log = root / "calls.jsonl"
        self.env = dict(os.environ, PATH=str(self.bin), FORMAT_LOG=str(self.log))
        for command in ("git", "mktemp", "rm"):
            (self.bin / command).symlink_to(shutil.which(command))
        for name in ("format.sh", ".clangformatignore", ".cmakeformatignore"):
            shutil.copyfile(ROOT / name, self.repo / name)
        for formatter in ("clang-format", "cmake-format"):
            path = self.bin / formatter
            path.write_text(
                f"#!{sys.executable}\n"
                "import json, os, sys\n"
                "from pathlib import Path\n"
                "with open(os.environ['FORMAT_LOG'], 'a') as log:\n"
                "    log.write(json.dumps([Path(sys.argv[0]).name, *sys.argv[1:]]) + '\\n')\n"
                "sys.exit(7 if os.environ.get('FAIL_FORMATTER') == Path(sys.argv[0]).name else 0)\n"
            )
            path.chmod(0o755)
        for name in (
            "sample.c", "space name.h", "back\\slash.c", "line\nname.c",
            "CMakeLists.txt", "nested/options.cmake", "README.md",
            "deps/ignored.c", "deps/CMakeLists.txt", "bindings/ignored.c",
            "zephyr/ignored.c", "zephyr/CMakeLists.txt",
        ):
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("test fixture\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "-c", "core.hooksPath=/dev/null", "commit", "-qm", "fixtures")

    def git(self, *args):
        """Run a Git fixture command and fail immediately if setup fails."""
        subprocess.run([GIT, *args], cwd=self.repo, env=self.env,  # nosec B603
                       check=True, capture_output=True)

    def run_script(self, **env):
        """Run format.sh and return its status, including expected failures."""
        return subprocess.run([BASH, "./format.sh"], cwd=self.repo,  # nosec B603
                              env=dict(self.env, **env), check=False,
                              capture_output=True, text=True)

    def calls(self):
        """Read the formatter invocations recorded by the mocks."""
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_success_and_file_selection(self):
        """Format selected tracked files, preserving unusual filenames."""
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertCountEqual(self.calls(), [
            ["clang-format", "-i", name]
            for name in ("sample.c", "space name.h", "back\\slash.c", "line\nname.c")
        ] + [["cmake-format", "-i", name]
             for name in ("CMakeLists.txt", "nested/options.cmake")])

    def test_missing_formatters_fail_before_formatting(self):
        """Reject missing dependencies before invoking either formatter."""
        for formatter in ("clang-format", "cmake-format"):
            with self.subTest(formatter=formatter):
                path = self.bin / formatter
                hidden = self.bin / (formatter + ".hidden")
                path.rename(hidden)
                try:
                    result = self.run_script()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(f"Required formatter not found: {formatter}", result.stderr)
                    self.assertEqual(self.calls(), [])
                finally:
                    hidden.rename(path)

    def test_formatter_errors_fail(self):
        """Propagate failures from both the C and CMake formatters."""
        for formatter in ("clang-format", "cmake-format"):
            with self.subTest(formatter=formatter):
                result = self.run_script(FAIL_FORMATTER=formatter)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"{formatter} failed for", result.stderr)

    def test_git_failure_does_not_format_files(self):
        """Reject a failed Git file listing before formatting starts."""
        (self.bin / "git").unlink()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_sourcing_has_no_side_effects(self):
        """Load helpers without formatting or changing shell options."""
        for formatter in ("clang-format", "cmake-format"):
            (self.bin / formatter).unlink()
        result = subprocess.run(  # nosec B603
            [BASH, "-c", 'before=$(set +o); source ./format.sh; '
             '[[ "$before" == "$(set +o)" ]] && declare -F format_all >/dev/null'],
            cwd=self.repo, env=self.env, check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
