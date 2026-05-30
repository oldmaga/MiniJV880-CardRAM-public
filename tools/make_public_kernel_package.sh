#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage:
  tools/make_public_kernel_package.sh --kernel /path/to/kernel8-rpi4.img [options]

Options:
  --out-dir DIR                 Output directory. Default: dist
  --kernel-provenance TEXT      Human-readable provenance note for the kernel image
  --allow-dirty                 Allow packaging from a dirty working tree, for local prototypes only
  -h, --help                    Show this help

Creates a kernel-only public release package:

  dist/MiniJV880-CardRAM-<tag-or-commit>-rpi4-kernel-only.zip
  dist/MiniJV880-CardRAM-<tag-or-commit>-rpi4-kernel-only.zip.sha256
  dist/MiniJV880-CardRAM-<tag-or-commit>-rpi4-kernel-only.MANIFEST.txt

The ZIP contains only:
  - the compiled kernel image;
  - README-SD-ROOT.txt;
  - SHA256SUMS.txt.

It intentionally does not include Raspberry Pi boot firmware, Roland data,
CardRAM contents, ROMs, SysEx files, logs, or personal configuration files.
EOF
}

KERNEL=""
OUT_DIR="dist"
KERNEL_PROVENANCE="Not specified. For public release, this kernel must be built from the corresponding public source tag/commit."
ALLOW_DIRTY=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --kernel)
      KERNEL="${2:-}"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="${2:-}"
      shift 2
      ;;
    --kernel-provenance)
      KERNEL_PROVENANCE="${2:-}"
      shift 2
      ;;
    --allow-dirty)
      ALLOW_DIRTY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if [ -z "$KERNEL" ]; then
  echo "ERROR: --kernel is required." >&2
  echo >&2
  usage >&2
  exit 2
fi

if [ ! -f "$KERNEL" ]; then
  echo "ERROR: kernel file not found: $KERNEL" >&2
  exit 1
fi

if [ "$ALLOW_DIRTY" -ne 1 ] && [ -n "$(git status --porcelain)" ]; then
  echo "ERROR: working tree is not clean." >&2
  echo "Use --allow-dirty only for local prototype tests." >&2
  git status --short >&2
  exit 1
fi

TAG="$(git describe --tags --exact-match 2>/dev/null || true)"
COMMIT="$(git rev-parse --short=12 HEAD)"

if [ -n "$TAG" ]; then
  VERSION="$TAG"
else
  VERSION="commit-$COMMIT"
fi

PKG_BASE="MiniJV880-CardRAM-${VERSION}-rpi4-kernel-only"
OUT_DIR_ABS="$ROOT/$OUT_DIR"
ZIP="$OUT_DIR_ABS/$PKG_BASE.zip"
MANIFEST="$OUT_DIR_ABS/$PKG_BASE.MANIFEST.txt"
ZIP_SHA="$ZIP.sha256"

mkdir -p "$OUT_DIR_ABS"

STAGING="$(mktemp -d "${TMPDIR:-/tmp}/${PKG_BASE}.XXXXXX")"
cleanup() {
  rm -rf "$STAGING"
}
trap cleanup EXIT

KERNEL_NAME="$(basename "$KERNEL")"
cp -a "$KERNEL" "$STAGING/$KERNEL_NAME"

cat > "$STAGING/README-SD-ROOT.txt" <<EOF
MiniJV880 CardRAM kernel-only package
=====================================

Package:
  $PKG_BASE

Package metadata source
-----------------------
This package was generated from the public repository state:

  repository: MiniJV880-CardRAM-public
  tag/commit: $VERSION
  commit: $COMMIT

Kernel image provenance
-----------------------
$KERNEL_PROVENANCE

For a public binary release, the kernel image should be built from the same
public source tag/commit declared above, or its exact provenance must be stated.

Contents
--------
  $KERNEL_NAME
  README-SD-ROOT.txt
  SHA256SUMS.txt

What this package contains
--------------------------
This package contains only the compiled MiniJV880 kernel image and checksum files.

What this package does NOT contain
----------------------------------
This package intentionally does not include:

  - Roland ROMs or copyrighted Roland data;
  - SR-JV80, PN-JV80, RD-500, or other proprietary content;
  - CardRAM personal contents;
  - SysEx banks or patches;
  - personal minijv880.ini configuration files;
  - Wi-Fi credentials;
  - logs;
  - Raspberry Pi boot firmware files such as start*.elf, fixup*.dat, *.dtb, overlays/.

Basic usage
-----------
Prepare a bootable Raspberry Pi 4 FAT32 boot partition using the required
Raspberry Pi boot files from an appropriate, separately obtained source.

Then copy this kernel image to the root of that FAT32 boot partition.

You will also need a suitable MiniJV880 configuration and any required runtime
files according to the public documentation. Do not use private or copyrighted
data unless you have the right to use it.

Checksum verification
---------------------
Before copying the files to the SD card, verify:

  sha256sum -c SHA256SUMS.txt

Notes
-----
This is a kernel-only convenience package. It is not a complete SD card image.
EOF

(
  cd "$STAGING"
  sha256sum "$KERNEL_NAME" README-SD-ROOT.txt > SHA256SUMS.txt
)

{
  echo "Package: $PKG_BASE"
  echo "Generated: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "Package metadata source version: $VERSION"
  echo "Package metadata source commit: $COMMIT"
  echo "Kernel image: $KERNEL_NAME"
  echo "Kernel provenance: $KERNEL_PROVENANCE"
  echo
  echo "Files:"
  (
    cd "$STAGING"
    find . -type f | sort
  )
  echo
  echo "SHA256SUMS.txt:"
  cat "$STAGING/SHA256SUMS.txt"
} > "$MANIFEST"

rm -f "$ZIP" "$ZIP_SHA"

python3 - <<PY
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

staging = Path("$STAGING")
zip_path = Path("$ZIP")

with ZipFile(zip_path, "w", ZIP_DEFLATED) as z:
    for p in sorted(staging.rglob("*")):
        if p.is_file():
            z.write(p, p.relative_to(staging))
PY

(
  cd "$OUT_DIR_ABS"
  sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP_SHA")"
)

echo "Created:"
echo "  $ZIP"
echo "  $ZIP_SHA"
echo "  $MANIFEST"
echo
echo "Package contents:"
python3 - <<PY
from zipfile import ZipFile
with ZipFile("$ZIP") as z:
    for name in z.namelist():
        print(" ", name)
PY
