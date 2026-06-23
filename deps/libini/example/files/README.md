# INI Parser Test Files

This directory contains **positive and negative test cases** for the `libini`
parser. These files are used to verify correct parsing behavior and robustness
when handling malformed input.

The files are grouped by expected outcome:

- `passXX.ini` — **Valid INI files**
  - The parser **must accept** these files.
  - `libini_test` is expected to return exit code `0`.

- `failXX.ini` — **Invalid or malformed INI files**
  - The parser **must reject** these files.
  - `libini_test` is expected to return a **non-zero** exit code.

The numeric suffix (`XX`) is used only for ordering and uniqueness.

---

## What these tests cover

The test cases exercise edge conditions discovered through:

- Manual inspection
- Static analysis
- Clang + libFuzzer runs

Examples include:

- Leading and trailing whitespace around section names and keys
- Indented section headers
- Missing or malformed delimiters (`[`, `]`, `=`)
- Unexpected characters following section headers
- Empty section names or keys
- Unterminated lines or headers
- CR/LF variations and unusual line endings

The intent is to ensure:

- Valid files are accepted, even with flexible formatting
- Malformed files are rejected deterministically
- The parser never reads past buffer boundaries or enters ambiguous states

---

## How these files are used

These files are typically exercised manually using shell loops that inspect the
file contents and validate the parser’s exit status.

### Fail cases (must be rejected)

```sh
clear && for i in $(ls fail*) ; do
    echo $i
    ls -l $i
    cat -n -A $i
    hexdump -C $i
    echo
    ../../build/bin/libini_test $i
    if [ "$?" -eq "0" ] ; then
        echo PASS
        break
    else
        echo FAIL
    fi
    echo
done
```
A PASS here means libini incorrectly parsed the file, and did not return an error.
A FAIL here means libini correctly failed, and passed an error code back.

### Pass cases (must be accepted)

```
clear && for i in $(ls pass*) ; do
    echo $i
    ls -l $i
    cat -n -A $i
    hexdump -C $i
    echo
    ../../build/bin/libini_test $i
    if [ "$?" -eq "0" ] ; then
        echo PASS
    else
        echo FAIL
        break
    fi
    echo
done
```
A PASS here means libini correctly parsed the file.
A FAIL here means libini incorrectly failed, and passed an error code back.

###Notes
Each file is intentionally small and focused on a specific parser behavior.

The exact error reported is not validated; only success vs. failure matters.

Adding new test cases when fixing bugs or hardening the parser is encouraged.

