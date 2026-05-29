# Contributing to libiio Documentation

Thank you for contributing to the libiio documentation!

## Building the Documentation

### Prerequisites

Install documentation dependencies:

```bash
pip install -r doc/requirements_doc.txt
```

### Building HTML Documentation

```bash
cd doc
make html
```

The generated documentation will be in `doc/build/html/`. Open `doc/build/html/index.html` in your browser.

## Quality Checks (Recommended)

Before submitting a pull request, it's good practice to run these optional quality checks:

### Check for Broken Links

Checks all external links in the documentation:

```bash
cd doc
make linkcheck
```

**Note:** Some external links may timeout or be temporarily unavailable. This is not a blocking issue - the CI will report link check results but won't fail the build.

### Check for Spelling and Style

Uses Vale to check for spelling errors, style issues, and terminology:

```bash
vale --config=.github/doc/styles/config/.vale.ini doc/source
```

Vale checks for:
- Spelling errors
- Terminology consistency (e.g., "IIO" vs "iio")
- Style guide compliance
- Common grammar issues

**Installing Vale:**

```bash
# On Ubuntu/Debian
sudo apt-get install vale

# On macOS
brew install vale

# Or download from https://vale.sh/
```

## Documentation Structure

- `doc/source/` - RST and Markdown source files
- `doc/source/api/` - API documentation for bindings (C, C++, C#, Python)
- `doc/doxygen/` - Doxygen configuration for C/C++/C# bindings
- `doc/source/tools/` - Command-line tools documentation

## Writing Guidelines

1. **Use clear, concise language** - Assume the reader is unfamiliar with the codebase
2. **Include examples** - Code examples help readers understand usage
3. **Check links** - Run `make linkcheck` before submitting
4. **Test code examples** - Ensure example code actually works
5. **Follow existing style** - Match the tone and structure of existing docs

## Pull Request Checklist

- [ ] Documentation builds without errors (`make html`)
- [ ] Links are valid (run `make linkcheck`)
- [ ] Spelling and style checked (run `vale`)
- [ ] Examples are tested and work
- [ ] Signed-off-by in commit message

For general contribution guidelines, see [CONTRIBUTING.md](../CONTRIBUTING.md) in the root directory.
