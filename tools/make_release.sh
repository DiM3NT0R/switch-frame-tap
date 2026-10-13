#!/usr/bin/env bash
# Build every Switch-side piece and pack the SD-card zip for a release:
#
#   bash tools/make_release.sh 0.1.0      -> dist/switch-frame-tap-0.1.0-switch.zip
#
# The zip unpacks onto the root of the SD card. It carries no config.ini, so
# an update never overwrites the user's settings (the sysmodule's defaults
# are the manager's defaults), and no test arm file.
set -e
VER="${1:?usage: make_release.sh VERSION}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG=devkitpro/devkita64:latest
OUT="$REPO/dist"
STAGE="$OUT/stage-switch"

[ -f "$REPO/ref/libtesla/include/tesla.hpp" ] || { echo "ref/libtesla is missing (git clone https://github.com/WerWolv/libtesla ref/libtesla)"; exit 1; }

echo "== sysmodule"
MAKEFLAGS="${MAKEFLAGS:--j6}" bash "$REPO/tier4/applet-mitm/build.sh" >/dev/null
echo "== memory donor"
docker run --rm -u "$(id -u):$(id -g)" -v "$REPO":/w -w /w/tier4/donor "$IMG" bash -lc 'make' >/dev/null
echo "== overlay and manager"
for d in overlay manager; do
    docker run --rm -u "$(id -u):$(id -g)" -v "$REPO":/w -w "/w/$d" "$IMG" bash -lc 'make -j8' >/dev/null
done

rm -rf "$STAGE"
mkdir -p "$STAGE/atmosphere/contents/0100000000000C20/flags" "$STAGE/switch/.overlays" "$STAGE/switch/switch-frame-tap"
# AMS_TREE / DKP_IMG (tier4/applet-mitm/build.sh) pick the Atmosphere the
# sysmodule is built against - v0.7.11: ref/Atmosphere-1.12.0 for firmware 23
NSP="$REPO/tier4/applet-mitm/applet-mitm${AMS_TREE:+-$(basename "$AMS_TREE")}.nsp"
cp "$NSP" "$STAGE/atmosphere/contents/0100000000000C20/exefs.nsp"
: > "$STAGE/atmosphere/contents/0100000000000C20/flags/boot2.flag"
# lets sysmodule managers (ovl-sysmodules and the like) list it by name
cat > "$STAGE/atmosphere/contents/0100000000000C20/toolbox.json" <<JSON
{
    "name": "Switch Frame Tap",
    "tid": "0100000000000C20",
    "requires_reboot": true
}
JSON
# v0.7.10: the memory donor - no boot2 flag; the sysmodule launches it while a
# viewer is connected (tier4/donor/source/main.c)
mkdir -p "$STAGE/atmosphere/contents/0100000000000C21"
cp "$REPO/tier4/donor/sft-donor.nsp" "$STAGE/atmosphere/contents/0100000000000C21/exefs.nsp"
cp "$REPO/overlay/switch-frame-tap.ovl" "$STAGE/switch/.overlays/"
cp "$REPO/manager/switch-frame-tap.nro" "$STAGE/switch/switch-frame-tap/"
# v0.7.4: dvr-patches (exelix11, BSD-3), bundled OFF: here Atmosphere does not
# read them; "Sound in no-record games" in the manager or overlay copies them
# to atmosphere/exefs_patches/switch-frame-tap-sound
mkdir -p "$STAGE/config/switch-frame-tap/dvr-patches"
cp "$REPO"/third_party/dvr-patches/*.ips "$REPO/third_party/dvr-patches/LICENSE" "$STAGE/config/switch-frame-tap/dvr-patches/"
cp "$REPO/third_party/dvr-patches/README.md" "$STAGE/config/switch-frame-tap/dvr-patches/README.txt"

ZIP="$OUT/switch-frame-tap-$VER-switch.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr "$ZIP" atmosphere switch config)
rm -rf "$STAGE"
echo "== $ZIP"
unzip -l "$ZIP"

# the Linux viewer (v0.7.11): one binary for any distribution, FFmpeg/SDL 2/
# libusb built in, with install.sh (tools/raw-recv/build-linux-portable.sh)
echo "== linux viewer"
bash "$REPO/tools/raw-recv/build-linux-portable.sh" "$VER" | tail -1
rm -rf "$OUT/build-linux-portable"

# the Windows viewer, when its toolchain is in ref/win (tools/raw-recv/build-windows.sh)
if [ -x "$REPO/ref/win/zig/zig" ]; then
    echo "== windows viewer"
    bash "$REPO/tools/raw-recv/build-windows.sh" "$VER" | tail -1
    rm -rf "$OUT/build-windows"
else
    echo "== windows viewer skipped (no ref/win toolchain)"
fi
