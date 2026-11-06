#!/bin/sh
# Assemble out/ports/gta3/ from the build (build/) and glos/, the driver
# set from goodluckOS-mali-driver: copy out/ports/gta3/ over
# /home/player/ports/gta3/ (game files in game/ stay there) and
# out/.local/share/applications/gta3.puppy next to the other launcher
# entries.
set -eu
cd "$(dirname "$0")"
GLOS=../goodluckOS-mali-driver/glos
[ -d "$GLOS" ] || { echo "package.sh: $GLOS missing (goodluckOS-mali-driver/assemble-glos.sh)" >&2; exit 1; }
out=out/ports/gta3
rm -rf out/ports out/.local
mkdir -p $out/lib $out/game $out/glos/lib out/.local/share/applications
docker run --rm -v "$PWD":/w -w /w gta3-builder:bookworm sh -c "
	cp build/gta3 $out/ && arm-linux-gnueabihf-strip $out/gta3 &&
	for l in libopenal.so.1 libsndio.so.7.0 libbsd.so.0 libmd.so.0; do cp -L /usr/lib/arm-linux-gnueabihf/\$l $out/lib/; done"
cp start.sh alsoft.conf $out/ && cp gta3.puppy out/.local/share/applications/
[ -f icon.png ] && cp icon.png $out/
echo "Game files go here: see README.md." > $out/game/place-game-files-here.txt
cp "$GLOS"/glos-gpu "$GLOS"/glos-gpu-switch "$GLOS"/mali.ko "$GLOS"/glos_fbfix.ko $out/glos/ && cp "$GLOS"/lib/* $out/glos/lib/
mkdir -p $out/licenses && cp licenses/* $out/licenses/
ls -la $out $out/lib $out/glos
