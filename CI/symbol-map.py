#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# libiio - Library for interfacing industrial I/O (IIO) devices
#
# Generate and verify libiio.map, the linker version script that defines
# libiio's exported ABI.
#
# "generate" derives the symbol list from the __api declarations found in the
# installed public headers. It is meant to be run by hand when the public API
# changes; the result is committed, reviewed like any other source file, and
# never generated at build time.
#
# "check" verifies that the committed map still agrees both with the headers
# and with a library that was just built. It is run as a ctest.

import argparse
import re
import subprocess
import sys

# A declaration is a run of text up to the next semicolon that contains the
# __api marker; the exported name is the first iio_/iiod_ identifier in it
# that is followed by an opening parenthesis.
API_RE = re.compile(r'\b__api\b')
NAME_RE = re.compile(r'\b((?:iio|iiod)_[a-z0-9_]+)\s*\(')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def strip_directives(text):
    """Drop preprocessor lines, honouring backslash continuations.

    Conditionals are deliberately *not* evaluated: a declaration inside
    #ifndef DOXYGEN or #ifndef __cplusplus is still a symbol the library
    exports, and the map has to be the same on every platform.
    """
    out, skipping = [], False
    for line in text.splitlines():
        if skipping or line.lstrip().startswith('#'):
            skipping = line.rstrip().endswith('\\')
            continue
        out.append(line)
    return '\n'.join(out)


def symbols_from_headers(headers):
    names = set()
    for header in headers:
        with open(header, encoding='utf-8') as f:
            text = strip_directives(strip_comments(f.read()))
        for decl in text.split(';'):
            if not API_RE.search(decl):
                continue
            match = NAME_RE.search(decl)
            if match:
                names.add(match.group(1))
            else:
                print('%s: cannot find a symbol name in __api declaration: %s'
                      % (header, ' '.join(decl.split())[:120]), file=sys.stderr)
                return None
    return names


def symbols_from_library(library, nm):
    argv = [nm, '--dynamic', '--defined-only', '--extern-only',
            '--format=posix', library]
    try:
        out = subprocess.run(argv, capture_output=True, text=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError) as err:
        print('failed to run %s: %s' % (' '.join(argv), err), file=sys.stderr)
        return None

    names = set()
    for line in out.splitlines():
        fields = line.split()
        # POSIX format is "name type [value [size]]". Lower-case types are
        # local symbols, which a version script is free to hide. Type "A" is
        # the pseudo-symbol the linker emits for the version node itself
        # (LIBIIO_1.0), not something the library exports.
        if len(fields) < 2 or not fields[1].isupper() or fields[1] == 'A':
            continue
        # Once the version script is in use, nm reports name@@LIBIIO_1.0.
        names.add(fields[0].split('@')[0])
    return names


def symbols_from_map(path):
    with open(path, encoding='utf-8') as f:
        text = strip_comments(f.read())

    # A map may hold more than one version node, since symbols added after
    # 1.0 belong in a node of their own, and only the first node carries
    # "local: *". Collect every "global:" section, each ending either at its
    # node's "local:" or at the node's closing brace.
    sections = re.findall(r'\bglobal\s*:(.*?)(?=\blocal\s*:|\})', text,
                          flags=re.S)
    if not sections:
        print('%s: no "global:" section found' % path, file=sys.stderr)
        return None

    names = set()
    for section in sections:
        for entry in section.split(';'):
            entry = entry.strip()
            # A hand-written node may hold a wildcard or an extern block.
            # Neither names a symbol that can be matched against a header.
            if not entry or '*' in entry or '?' in entry or '{' in entry:
                continue
            names.add(entry)
    return names


MAP_HEADER = """/* SPDX-License-Identifier: MIT */
/*
 * Linker version script describing libiio's exported ABI.
 *
 * Every symbol libiio makes available to applications is listed here, and
 * "local: *" hides everything else. This is what freezes the export surface:
 * -fvisibility=hidden already hides most internals, but it is only a compiler
 * default, and any single __attribute__ overrides it -- as the symbols of
 * deps/libini used to demonstrate.
 *
 * This file was generated from the public headers with
 *
 *     ./CI/symbol-map.py generate $(public headers) -o libiio.map
 *
 * but once a release is out, do not regenerate it over the top of this one,
 * and never remove or rename an entry: an application linked against
 * LIBIIO_1.0 records that dependency and refuses to start against a library
 * that no longer defines it. Add new symbols by hand, in a node of their own,
 *
 *     LIBIIO_1.1 { global: iio_new_thing; } LIBIIO_1.0;
 *
 * which keeps binaries built against 1.0 working, while a binary that calls
 * iio_new_thing records the stricter requirement and so fails early against
 * an older library rather than at the call.
 *
 * Not every symbol listed here exists in every build -- the iiod client, the
 * XML backend and the locking implementation are all optional -- and some
 * linkers reject a script that names a symbol they cannot find. The build
 * passes --undefined-version where it is supported so that those reduced
 * configurations keep linking.
 */
"""


def generate(args):
    names = symbols_from_headers(args.headers)
    if names is None:
        return 1

    lines = [MAP_HEADER, '%s {' % args.version, 'global:']
    lines += ['\t%s;' % name for name in sorted(names)]
    lines += ['', 'local:', '\t*;', '};', '']

    text = '\n'.join(lines)
    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


def report(what, names):
    for name in sorted(names):
        print('  %s: %s' % (what, name), file=sys.stderr)


def check(args):
    declared = symbols_from_headers(args.headers)
    mapped = symbols_from_map(args.map)
    if declared is None or mapped is None:
        return 1

    failed = False

    # The map must describe the whole public API, no more and no less. A
    # symbol declared __api but missing from the map is not exported at all
    # (the bug this file exists to prevent); a symbol in the map that no
    # header declares is either a typo or a leftover.
    missing = declared - mapped
    if missing:
        print('%s: declared __api but missing from the map:' % args.map,
              file=sys.stderr)
        report('missing', missing)
        failed = True

    extra = mapped - declared
    if extra:
        print('%s: listed in the map but not declared __api in any header:'
              % args.map, file=sys.stderr)
        report('stale', extra)
        failed = True

    if args.library:
        exported = symbols_from_library(args.library, args.nm)
        if exported is None:
            return 1

        # Only a subset needs to be present -- optional sources are not
        # built in every configuration -- but nothing may escape the map.
        leaked = exported - mapped
        if leaked:
            print('%s: exported but not listed in the map:' % args.library,
                  file=sys.stderr)
            report('leaked', leaked)
            failed = True

        if not exported:
            print('%s: no exported symbols found; is this a library?'
                  % args.library, file=sys.stderr)
            failed = True

    if failed:
        print('\nIf the public API changed on purpose, regenerate the map:',
              file=sys.stderr)
        print('  ./CI/symbol-map.py generate <public headers> -o libiio.map',
              file=sys.stderr)
        print('and read the comment at the top of libiio.map before removing '
              'anything.', file=sys.stderr)
        return 1

    print('%d symbols; map matches the headers%s.'
          % (len(mapped), ' and the built library' if args.library else ''))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)

    gen = sub.add_parser('generate', help='write a version script')
    gen.add_argument('headers', nargs='+')
    gen.add_argument('-o', '--output')
    gen.add_argument('--version', default='LIBIIO_1.0')
    gen.set_defaults(func=generate)

    chk = sub.add_parser('check', help='verify a committed version script')
    chk.add_argument('headers', nargs='+')
    chk.add_argument('-m', '--map', required=True)
    chk.add_argument('-l', '--library')
    chk.add_argument('--nm', default='nm')
    chk.set_defaults(func=check)

    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
