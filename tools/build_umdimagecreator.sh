#!/bin/sh
# Builds UmdImageCreator (https://github.com/saramibreak/UmdImageCreator) from
# its source into store/packages/UmdImageCreator-<version>.zip, the package the
# default store installs.
#
# The author's releases attach the builds to the release notes, and they
# couldn't be checked from where the store is maintained, so the store ships
# this build of the same tag instead. The source is unchanged; only the build
# flags differ: today's newlib has strncasecmp() where the author's SDK had
# strnicmp().
#
# Usage: tools/build_umdimagecreator.sh [tag]      (default v1.7)
# Needs the pspdev toolchain (psp-config on the PATH) and zip.

set -eu

TAG=${1:-v1.7}
VERSION=${TAG#v}
HERE=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

git clone -q --depth 1 --branch "$TAG" https://github.com/saramibreak/UmdImageCreator "$WORK/src"
COMMIT=$(git -C "$WORK/src" rev-parse --short HEAD)

# the kernel module the app loads from ms0:/seplugins/pspumdman.prx
make -s -C "$WORK/src/UmdDriver"

# the app for the Game column (define.h and makefile_define.mak already select it)
cd "$WORK/src/UmdImageCreator"
sed -i 's/^CFLAGS = -O2 -G0 -Wall -Wextra *$/& -Dstrnicmp=strncasecmp/' makefile
sed -i 's/^ASFLAGS = \$(CFLAGS)$/ASFLAGS = -O2 -G0 -Wall/' makefile
grep -q 'strnicmp=strncasecmp' makefile
make -s

PKG="$WORK/pkg"
mkdir -p "$PKG/PSP/GAME/UmdImageCreator" "$PKG/seplugins"
cp EBOOT.PBP "$PKG/PSP/GAME/UmdImageCreator/"
cp "$WORK/src/UmdDriver/pspumdman.prx" "$PKG/seplugins/"
cp "$WORK/src/LICENSE" "$PKG/PSP/GAME/UmdImageCreator/LICENSE.txt"
cat > "$PKG/PSP/GAME/UmdImageCreator/README.txt" <<EOF
UmdImageCreator $VERSION by sarami, https://github.com/saramibreak/UmdImageCreator
Licensed under the Apache License 2.0 (LICENSE.txt).

Built by FasterARK powerup from tag $TAG (commit $COMMIT) without changes to the
source, with -Dstrnicmp=strncasecmp for today's PSP toolchain.

Start it from the Game column with the UMD in the drive:
  Circle    dump the disc to ms0:/ISO/<disc ID>.iso, with logs
  Cross     write the logs only
  Triangle  quit
UMD Video discs go to ms0:/ISO/VIDEO/. The app loads ms0:/seplugins/pspumdman.prx.
EOF

# fixed dates, so the same build gives the same archive
find "$PKG" -exec touch -d '2024-06-01 00:00:00' {} +
mkdir -p "$HERE/store/packages"
OUT="$HERE/store/packages/UmdImageCreator-$VERSION.zip"
rm -f "$OUT"
(cd "$PKG" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | TZ=UTC zip -q -X -9 "$OUT" -@)
echo "$OUT"
sha256sum "$OUT"
wc -c < "$OUT"
