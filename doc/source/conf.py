import datetime
import os
import re

# -- Pre-build tasks ----------------------------------------------------------
# Build doxygen XML documentation

doxyfolder = os.path.join(os.path.dirname(__file__), "..", "doxygen")
cwd = os.getcwd()
os.chdir(doxyfolder)
os.system("doxygen doxyfile.in")
os.chdir(cwd)

# Build C# doxygen XML documentation
csharp_doxyfolder = os.path.join(os.path.dirname(__file__), "..", "doxygen", "csharp")
if os.path.exists(csharp_doxyfolder):
    os.chdir(csharp_doxyfolder)
    os.system("doxygen Doxyfile")
    os.chdir(cwd)

# Add bindings and examples to path
import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "bindings", "python")))
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "bindings", "examples")))

import iio

# -- Project information -----------------------------------------------------
project = 'libiio'
year = datetime.datetime.now().year

copyright = f'2015-{year}, Analog Devices, Inc.'
author = 'Analog Devices, Inc.'
release = 'v1.0'

# -- General configuration ---------------------------------------------------

extensions = [
    "sphinx.ext.todo",
    "adi_doctools",
    "myst_parser",
    "breathe",
    "sphinx_csharp",
    'sphinxcontrib.matlab', 'sphinx.ext.autodoc',
    "sphinx_inline_tabs",
]

needs_extensions = {
    'adi_doctools': '0.4.33'
}

myst_enable_extensions = ["colon_fence", "attrs_inline"]

breathe_default_project = "libiio"
breathe_projects = {
    "libiio": os.path.join(doxyfolder, "generated", "xml"),
    "libiio-csharp": os.path.join(doxyfolder, "generated", "csharp", "xml")
}

# Suppress known warnings
suppress_warnings = [
    'duplicate_declaration.cpp',
    'duplicate_c_declaration',
    'myst.domains',  # C# domain doesn't implement resolve_any_xref
]

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
source_suffix = ['.rst', '.md']

matlab_src_dir = os.path.join(os.path.dirname(__file__), "..", '..')
# matlab_keep_package_prefix = False
matlab_short_links = True

# -- External docs configuration ----------------------------------------------

interref_repos = [
    'doctools',
    'documentation',
    'hdl',
    'pyadi-iio',
    'scopy',
    'linux',
    'no-OS',
    'precision-converters-firmware',
]

# -- Intersphinx configuration ------------------------------------------------
# Add upstream kernel.org documentation for linking to official kernel docs
intersphinx_mapping = {
    'kernel': ('https://www.kernel.org/doc/html/latest/', None),
}

# -- Custom extensions configuration ------------------------------------------

hide_collapsible_content = True
validate_links = False

# -- Linkcheck configuration --------------------------------------------------

# Don't treat timeouts as errors - they're often transient network issues
linkcheck_timeout = 15  # Increase timeout to 15 seconds
linkcheck_report_timeouts_as_broken = False  # Report timeouts as warnings, not errors

# Ignore links that frequently timeout or redirect
linkcheck_ignore = [
    # Analog.com product pages frequently timeout in CI
    r'https://www\.analog\.com/.*',
    # GitHub releases/latest intentionally redirects to actual version
    r'https://github\.com/.*/releases/latest',
]

# -- todo configuration -------------------------------------------------------

todo_include_todos = True
todo_emit_warnings = True

# -- Options for HTML output --------------------------------------------------

html_theme = 'cosmic'
html_static_path = ['_static']
html_css_files = ["custom.css"]
# html_favicon = path.join("sources", "icon.svg")

# html_theme_options = {
#     "light_logo": "HDL_logo_cropped.svg",
#     "dark_logo": "HDL_logo_w_cropped.svg",
# }
# -- Custom warning filter for C# Breathe xref warnings ----------------------

import logging as py_logging

# Patterns for C# xref warnings to suppress (unavoidable .NET system types)
csharp_xref_suppress_patterns = [
    re.compile(r"Failed to find xref for: IntPtr"),  # .NET system type
    re.compile(r"Failed to find xref for: >"),        # Generic type parsing artifact
]

class CSharpXrefWarningFilter(py_logging.Filter):
    """Filter out unavoidable C# xref warnings for .NET system types"""

    def filter(self, record):
        msg = record.getMessage()
        for pattern in csharp_xref_suppress_patterns:
            if pattern.search(msg):
                return False  # Suppress this warning
        return True  # Allow other warnings

def setup(app):
    """Register custom warning filter for Sphinx logger"""
    # Add filter to the Sphinx warning logger
    sphinx_logger = py_logging.getLogger('sphinx')
    sphinx_logger.addFilter(CSharpXrefWarningFilter())
