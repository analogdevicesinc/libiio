# -- Import setup -------------------------------------------------------------

from os import path

# -- Project information -----------------------------------------------------

repository = 'libiio'
project = 'libiio'
copyright = '2020-2026, Analog Devices, Inc.'
author = 'Analog Devices, Inc.'

# Version will be read from git or set manually
version = '1.0'
release = '1.0'

# -- General configuration ---------------------------------------------------

extensions = [
    "sphinx.ext.todo",
    "adi_doctools",
]

needs_extensions = {
    'adi_doctools': '0.4.33'
}

exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
source_suffix = '.rst'

# -- External docs configuration ----------------------------------------------

interref_repos = [
    'doctools',
    'hdl',
    'pyadi-iio',
    'scopy',
    'linux',
    'no-OS',
    'precision-converters-firmware',
]

# -- Options for HTML output --------------------------------------------------

html_theme = 'cosmic'
html_title = f"{project} {version}"
html_favicon = path.join("sources", "icon.svg")
html_static_path = ['sources', 'html/img']
html_css_files = ['custom.css']
html_js_files = ['custom.js']
numfig = True
numfig_per_doc = True

numfig_format = {'figure': 'Figure %s',
                 'table': 'Table %s',
                 'code-block': 'Listing %s',
                 'section': 'Section %s'}

# -- Show TODOs ---------------------------------------------------------------

todo_include_todos = True

# -- Linkcheck ----------------------------------------------------------------

linkcheck_sitemaps = [
    "https://wiki.analog.com/doku.php?do=sitemap",
    "https://www.analog.com/media/en/en-pdf-sitemap.xml",
    "https://www.analog.com/media/en/en-pdp-sitemap.xml",
]
linkcheck_timeout = 5
linkcheck_request_headers = {
    "*": {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:142.0) Gecko/20100101 Firefox/142.0",
        "Accept-Language": "en-US,en;q=0.5",
    },
}
linkcheck_ignore = [
    r'https://www.digikey.com/',
]