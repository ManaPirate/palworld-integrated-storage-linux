#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
    pwd
)"

REPO_ROOT="$(
    cd -- "$SCRIPT_DIR/.." &&
    pwd
)"

UE4SS_ROOT="${UE4SS_ROOT:-/workspace/RE-UE4SS-Linux}"
FIXTURE="$UE4SS_ROOT/validation/native/cpp-mod-loading"

BUILD_ROOT="${UE4SS_BUILD_ROOT:-/build/nullprism-linux-v0.1.0-sdk}"
BUILD_OUTPUT="${BUILD_OUTPUT:-/build/integrated-storage-linux}"
STAGE="${STAGE:-/staging/ModIntegratedStorageCpp}"

SOURCE="$REPO_ROOT/src/linux/main.cpp"
EXPORT_MAP="$REPO_ROOT/src/linux/exports.map"
FIXTURE_BUILD="$FIXTURE/build.sh"

RELEASE_HASH="26dffce875fb771fb2ac2a63325e7effb5551a03a35598810f13d2e6c854a1ff"

LOADER="$BUILD_ROOT/Game__Shipping__Linux/lib64/libUE4SS.so"

for REQUIRED in \
    "$SOURCE" \
    "$EXPORT_MAP" \
    "$FIXTURE_BUILD" \
    "$LOADER"
do
    test -f "$REQUIRED" || {
        echo "ERROR: Required file is missing:"
        echo "$REQUIRED"
        exit 1
    }
done

ACTUAL_LOADER_HASH="$(
    sha256sum "$LOADER" |
    awk '{print $1}'
)"

test "$ACTUAL_LOADER_HASH" = "$RELEASE_HASH" || {
    echo "ERROR: Loader does not match the official NullPrism release."
    echo "Expected: $RELEASE_HASH"
    echo "Actual:   $ACTUAL_LOADER_HASH"
    exit 1
}

mkdir -p "$BUILD_OUTPUT"

ARTIFACT="$BUILD_OUTPUT/main.so"
TEMP_BUILD_SCRIPT="$(mktemp /tmp/integrated-storage-build.XXXXXX.sh)"

cleanup() {
    rm -f "$TEMP_BUILD_SCRIPT"
}

trap cleanup EXIT INT TERM

python3 \
    - "$FIXTURE_BUILD" \
    "$TEMP_BUILD_SCRIPT" \
    "$SOURCE" \
    "$EXPORT_MAP" \
    "$BUILD_OUTPUT" \
    "$ARTIFACT" \
    "$RELEASE_HASH" <<'PY'
from pathlib import Path
import re
import sys

(
    source_script,
    destination_script,
    source_file,
    export_map,
    output_dir,
    output_file,
    loader_hash,
) = sys.argv[1:]

text = Path(source_script).read_text(encoding="utf-8")

replacements = (
    (
        r'^source_file=.*$',
        f'source_file="{source_file}"',
        "source_file",
    ),
    (
        r'^export_map=.*$',
        f'export_map="{export_map}"',
        "export_map",
    ),
    (
        r'^output_dir=.*$',
        f'output_dir="{output_dir}"',
        "output_dir",
    ),
    (
        r'^output_file=.*$',
        f'output_file="{output_file}"',
        "output_file",
    ),
    (
        r'^expected_loader_hash=.*$',
        f'expected_loader_hash="{loader_hash}"',
        "expected_loader_hash",
    ),
)

for pattern, replacement, label in replacements:
    text, count = re.subn(
        pattern,
        replacement,
        text,
        count=1,
        flags=re.MULTILINE,
    )

    if count != 1:
        raise SystemExit(
            f"ERROR: Could not patch fixture assignment: {label}"
        )

Path(destination_script).write_text(text, encoding="utf-8")
PY

chmod 700 "$TEMP_BUILD_SCRIPT"
rm -f "$ARTIFACT"

echo "=== BUILDING LINUX DEDICATED-SERVER MOD ==="

cd "$UE4SS_ROOT"

UE4SS_BUILD_ROOT="$BUILD_ROOT" \
    bash "$TEMP_BUILD_SCRIPT"

test -f "$ARTIFACT" || {
    echo "ERROR: Build did not produce main.so."
    exit 1
}

echo
echo "=== FILE IDENTITY ==="

file "$ARTIFACT"

echo
echo "=== LIFECYCLE EXPORTS ==="

EXPORTS="$(
    nm -D --defined-only "$ARTIFACT"
)"

printf '%s\n' "$EXPORTS"

printf '%s\n' "$EXPORTS" |
grep -qE '[[:space:]]start_mod$' || {
    echo "ERROR: start_mod is not exported."
    exit 1
}

printf '%s\n' "$EXPORTS" |
grep -qE '[[:space:]]uninstall_mod$' || {
    echo "ERROR: uninstall_mod is not exported."
    exit 1
}

echo
echo "=== ELF DEPENDENCIES ==="

readelf -d "$ARTIFACT" |
grep -E 'NEEDED|SONAME|RUNPATH'

echo
echo "=== RELOCATION CHECK ==="

LD_LIBRARY_PATH="$BUILD_ROOT/Game__Shipping__Linux/lib64" \
    ldd -r "$ARTIFACT"

echo
echo "=== STAGING PACKAGE ==="

rm -rf "$STAGE"

mkdir -p "$STAGE/dlls"

install \
    -m 755 \
    "$ARTIFACT" \
    "$STAGE/dlls/main.so"

: > "$STAGE/enabled.txt"

SOURCE_HASH="$(
    sha256sum "$SOURCE" |
    awk '{print $1}'
)"

ARTIFACT_HASH="$(
    sha256sum "$STAGE/dlls/main.so" |
    awk '{print $1}'
)"

cat > "$STAGE/BUILD-PROVENANCE.txt" <<EOF
Integrated Storage Linux dedicated-server build

Repository:
$REPO_ROOT

Branch:
$(git -C "$REPO_ROOT" branch --show-current)

Repository HEAD:
$(git -C "$REPO_ROOT" rev-parse HEAD)

NullPrism source:
$UE4SS_ROOT

NullPrism commit:
$(git -C "$UE4SS_ROOT" rev-parse HEAD)

NullPrism tag:
$(git -C "$UE4SS_ROOT" describe --tags --always)

Official loader SHA256:
$ACTUAL_LOADER_HASH

Linux source SHA256:
$SOURCE_HASH

Linux main.so SHA256:
$ARTIFACT_HASH

Compiler:
$(clang++ --version | head -n 1)

Built:
$(date --iso-8601=seconds)

Scope:
Stage 4a read-only base-camp, guild and storage discovery.
No chest traversal, cross-registration, transport or container mutation is enabled.
EOF

echo
echo "=== STAGED FILES ==="

find "$STAGE" \
    -maxdepth 3 \
    -printf '%y  %P\n' |
sort

echo
cat "$STAGE/BUILD-PROVENANCE.txt"

echo
echo "PASS: Integrated Storage Linux dedicated-server mod built and staged."
