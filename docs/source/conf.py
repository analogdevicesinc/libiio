import datetime
import os

# -- Pre-build tasks ----------------------------------------------------------
# Build doxygen XML documentation

doxyfolder = os.path.join(os.path.dirname(__file__), "..", "doxygen")
cwd = os.getcwd()
os.chdir(doxyfolder)
os.system("doxygen doxyfile.in")
os.chdir(cwd)

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
    'sphinxcontrib.matlab', 'sphinx.ext.autodoc',
]

needs_extensions = {
    'adi_doctools': '0.3.6'
}

myst_enable_extensions = ["colon_fence"]

breathe_default_project = "libiio"
breathe_projects = {"libiio": os.path.join(doxyfolder, "generated", "xml")}

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
source_suffix = ['.rst', '.md']

matlab_src_dir = os.path.join(os.path.dirname(__file__), "..", '..')
# matlab_keep_package_prefix = False
matlab_short_links = True

# -- External docs configuration ----------------------------------------------

interref_repos = ['doctools']

# -- Custom extensions configuration ------------------------------------------

hide_collapsible_content = True
validate_links = False

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