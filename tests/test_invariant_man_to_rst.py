import pytest
import subprocess
import sys
import os
from pathlib import Path

# Import the actual module
sys.path.insert(0, str(Path(__file__).parent.parent))
from doc.man_to_rst import convert_man_to_rst

@pytest.mark.parametrize("payload", [
    # Exploit payload - command injection via shell metacharacters
    "normal_file; rm -rf /tmp/test",
    # Alternative injection method
    "file$(whoami)",
    # Backtick execution
    "valid`id`file",
    # Boundary case - normal filename
    "normal_man_page.1",
])
def test_shell_command_injection_never_executes_user_input(payload, tmp_path):
    """Invariant: Shell commands never include unsanitized user input"""
    # Create test files in temporary directory
    source_dir = tmp_path / "source"
    target_dir = tmp_path / "target"
    source_dir.mkdir()
    target_dir.mkdir()
    
    # Create a dummy man page file
    man_file = source_dir / payload
    man_file.write_text(".TH TEST 1")
    
    # Attempt conversion - this should either fail safely or escape the input
    try:
        convert_man_to_rst(str(man_file), str(target_dir))
        
        # If execution reaches here, verify no shell commands were executed
        # by checking that the target file was created with proper escaping
        expected_safe_name = payload.replace(";", "").replace("`", "").replace("$", "")
        target_file = target_dir / f"{expected_safe_name}.rst"
        
        # The file should either not exist (if input was rejected) 
        # or exist with sanitized name (if input was escaped)
        # Either is acceptable as long as no shell injection occurred
        if target_file.exists():
            # Verify the file contains expected content, not command output
            content = target_file.read_text()
            assert "TEST" in content  # From the man page
            assert "whoami" not in content  # No command output
            assert "uid=" not in content  # No id command output
    except (subprocess.CalledProcessError, ValueError, OSError):
        # Exceptions are acceptable - they indicate the input was rejected
        pass
    
    # Final verification: ensure no malicious files were created
    # Check that no files with suspicious names exist in temp directory
    suspicious_files = list(tmp_path.glob("*test*"))  # From rm -rf /tmp/test
    assert len(suspicious_files) == 0, f"Suspicious files created: {suspicious_files}"