#!/usr/bin/env bash
#
# Build and run every example against the *installed* library.
#
# These programs are documentation, and documentation that does not compile is
# worse than none — so they are built the way a reader would build them, from an
# install prefix rather than from the build tree, with nothing from this
# repository on the include path.
#
# Usage: bash packages/c/examples/run-all.sh [install-prefix]
#        (default prefix: ~/.cache/policybook/c-install)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${1:-$HOME/.cache/policybook/c-install}"
CC_BIN="${CC:-cc}"

if [ ! -d "$PREFIX/include/policybook" ]; then
    echo "no install found at $PREFIX" >&2
    echo "run: cmake --install <build-dir>" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass() { printf '  \033[32mok\033[39m   %s\n' "$1"; }
fail() { printf '  \033[31mFAIL\033[39m %s\n' "$1"; exit 1; }

echo "examples: building against $PREFIX"
for source in "$HERE"/*.c; do
    name="$(basename "$source" .c)"

    # The single-header check is not an example and does not link the library —
    # it has its own step below.
    [ "$name" = "single-header-check" ] && continue

    if ! "$CC_BIN" -std=c99 -Wall -Wextra -Wpedantic -Werror -ffp-contract=off \
            -I "$PREFIX/include" "$source" -L "$PREFIX/lib" -lpolicybook -lm \
            -o "$WORK/$name" 2>&1; then
        fail "$name does not compile against the installed library"
    fi
    pass "$name compiles"
done

echo
echo "examples: running"
for source in "$HERE"/*.c; do
    name="$(basename "$source" .c)"
    [ "$name" = "single-header-check" ] && continue

    if output="$("$WORK/$name" 2>&1)"; then
        printf '  \033[32mok\033[39m   %s\n' "$name"
        printf '       %s\n' "$output" | sed 's/^       $//'
    else
        printf '%s\n' "$output"
        fail "$name exited non-zero"
    fi
done

echo
echo "examples: every program builds from an install and runs."
