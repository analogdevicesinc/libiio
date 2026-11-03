#!/usr/bin/env python
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
SPDX-License-Identifier: LGPL-2.1-or-later

Copyright (C) 2026 Analog Devices, Inc.

Smoke test for the Python bindings: import iio, then create and destroy a
number of contexts. Destroying a Context frees the XML string allocated by
libiio, so this fails (import error or heap corruption) if the bindings use a
different C runtime than the libiio build under test. No hardware is needed.

Usage: test_context_lifecycle.py [DIR]

DIR, if given, is the directory holding libiio1.dll and its dependencies. On
Windows, set LIBIIO_PY_CRT=msvcrt when testing a MinGW build.
"""

import os
import sys
import tempfile

ITERATIONS = 500

XML = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE context [
<!ELEMENT context (context-attribute | device)*>
<!ELEMENT context-attribute EMPTY>
<!ELEMENT device (channel | attribute | debug-attribute | buffer-attribute)*>
<!ELEMENT channel (scan-element?, attribute*)>
<!ELEMENT attribute EMPTY>
<!ELEMENT scan-element EMPTY>
<!ELEMENT debug-attribute EMPTY>
<!ELEMENT buffer-attribute EMPTY>
<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>
<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>
<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED label CDATA #IMPLIED>
<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>
<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>
<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED value CDATA #IMPLIED>
<!ATTLIST debug-attribute name CDATA #REQUIRED value CDATA #IMPLIED>
<!ATTLIST buffer-attribute name CDATA #REQUIRED value CDATA #IMPLIED>
]>
<context name="python-smoke-test" description="python bindings smoke test">
<device id="iio:device0" name="dev0">
<channel id="voltage0" type="input" name="voltage_input">
<attribute name="raw" filename="in_voltage0_raw"/>
</channel>
<attribute name="firmware_version" filename="firmware_version"/>
</device>
</context>
"""


def main():
    if len(sys.argv) > 1:
        os.environ["PATH"] = os.path.abspath(sys.argv[1]) + os.pathsep + os.environ["PATH"]

    import iio  # pylint: disable=import-outside-toplevel

    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "context.xml")
        with open(path, "w", encoding="utf-8") as f:
            f.write(XML)

        for _ in range(ITERATIONS):
            ctx = iio.Context("xml:" + path)
            assert ctx.description == "python bindings smoke test", ctx.description
            del ctx

    print(f"OK: {ITERATIONS} contexts created and destroyed "
          f"(libc: {getattr(iio, '_libc_name', 'n/a')})")


if __name__ == "__main__":
    main()
