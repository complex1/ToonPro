#!/bin/sh
set -eu
# Official Filament 1.78.0 macOS arm64 SDK. Never installs system-wide.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=1.78.0
EXPECTED=dc2fbc7681998842e85ccc211ed1292b15631a97731fa49ef07c2698870ce9c5
DEST="$ROOT/.deps/filament"
ARCHIVE="$DEST/filament-v$VERSION-mac.tgz"
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
    echo 'This pinned SDK setup supports macOS arm64. Other platforms require a matching Filament SDK and material build.' >&2
    exit 1
fi
mkdir -p "$DEST"
if [ ! -f "$ARCHIVE" ]; then
    curl --fail --location "https://github.com/google/filament/releases/download/v$VERSION/filament-v$VERSION-mac.tgz" -o "$ARCHIVE.download"
    mv "$ARCHIVE.download" "$ARCHIVE"
fi
ACTUAL=$(shasum -a 256 "$ARCHIVE" | cut -d ' ' -f 1)
if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo 'Filament archive checksum mismatch; remove the archive and retry.' >&2
    exit 1
fi
tar xzf "$ARCHIVE" -C "$DEST"
"$DEST/filament/bin/matc" -p desktop -a metal -o "$ROOT/resources/scene3d/reference.filamat" "$ROOT/resources/scene3d/reference.mat"
printf 'Filament %s installed in %s\n' "$VERSION" "$DEST/filament"
