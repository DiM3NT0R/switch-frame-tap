#!/bin/bash
# v0.7.11: the viewer for macOS, as an app (Switch Frame Tap.app), built on a
# Mac or by .github/workflows/viewer-macos.yml on GitHub's macOS runners.
#
# Built in: FFmpeg 8.1 with only what the viewer uses (H.264 decoder, AAC
# encoder, MP4 muxer, as for Windows and Linux), SDL 2 and libusb (Homebrew's
# static libraries). It then needs only macOS's own frameworks - no Homebrew
# on the user's Mac. Thanks to proconsule54 (the first macOS build) and
# DiM3NT0R (macOS fixes, PR #12).
#
# Needs: Xcode command line tools, Homebrew: brew install sdl2 libusb pkg-config
# (nasm too on Intel). Downloads ffmpeg-8.1.tar.xz from ffmpeg.org if ref/
# does not have it.
#
#   bash tools/raw-recv/build-macos.sh 0.7.11
#     -> dist/switch-frame-tap-0.7.11-macos-<arm64|x86_64>.zip
set -euo pipefail
VER="${1:?usage: build-macos.sh VERSION}"
REPO=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$REPO/tools/raw-recv"
ARCH=$(uname -m)
M="$REPO/ref/macos-$ARCH"
mkdir -p "$M" "$REPO/dist"

# ---- FFmpeg, static, only the viewer's pieces
FF="$M/ffmpeg-min"
if [ ! -f "$FF/lib/libavcodec.a" ]; then
    echo "== FFmpeg 8.1 (minimal, static, $ARCH)"
    [ -f "$REPO/ref/ffmpeg-8.1.tar.xz" ] || curl -sSfL -o "$REPO/ref/ffmpeg-8.1.tar.xz" https://ffmpeg.org/releases/ffmpeg-8.1.tar.xz
    rm -rf "$M/ffmpeg-8.1"; tar -xJf "$REPO/ref/ffmpeg-8.1.tar.xz" -C "$M"
    B="$M/ffmpeg-8.1/build"
    mkdir -p "$B"; cd "$B"
    ASM=--enable-asm; [ "$ARCH" = x86_64 ] && ASM=--enable-x86asm
    ../configure \
        --prefix="$FF" --enable-static --disable-shared --enable-pic \
        --disable-everything --disable-autodetect --disable-programs --disable-doc \
        --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
        --enable-decoder=h264 --enable-parser=h264 \
        --enable-encoder=aac \
        --enable-muxer=mp4 --enable-protocol=file \
        $ASM > configure.log 2>&1 || { tail -30 configure.log; exit 1; }
    make -j"$(sysctl -n hw.ncpu)" > make.log 2>&1 || { tail -40 make.log; exit 1; }
    make install > install.log 2>&1
    cp ../COPYING.LGPLv2.1 "$FF/"
fi

# ---- the viewer, statically linked against SDL 2 and libusb
echo "== raw-view ($ARCH)"
SDLP=$(brew --prefix sdl2)
USBP=$(brew --prefix libusb)
OUT="$REPO/dist/build-macos-$ARCH"
rm -rf "$OUT"; mkdir -p "$OUT"
# sdl2-config --static-libs, without -lSDL2 (the .a below instead of the dylib)
SDL_STATIC=$("$SDLP/bin/sdl2-config" --static-libs | sed -e 's/-lSDL2main//' -e 's/-lSDL2//' -e "s#-L[^ ]*##g")
cd "$HERE"
clang -O2 -Wall -DSFT_H264 -DSFT_MP4 \
    -I"$FF/include" $("$SDLP/bin/sdl2-config" --cflags) -I"$USBP/include/libusb-1.0" \
    -o "$OUT/switch-frame-tap-viewer" raw-view.c \
    "$FF/lib/libavformat.a" "$FF/lib/libavcodec.a" "$FF/lib/libavutil.a" \
    "$SDLP/lib/libSDL2.a" $SDL_STATIC \
    "$USBP/lib/libusb-1.0.a" -framework IOKit -framework CoreFoundation -framework Security \
    -liconv -lm
strip "$OUT/switch-frame-tap-viewer"
echo "   links to: $(otool -L "$OUT/switch-frame-tap-viewer" | tail -n +2 | awk '{print $1}' | tr '\n' ' ')"
if otool -L "$OUT/switch-frame-tap-viewer" | grep -q "/opt/homebrew\|/usr/local/"; then
    echo "   it still links to a Homebrew library - not portable"; exit 1
fi

# ---- the app
APP="$OUT/Switch Frame Tap.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$OUT/switch-frame-tap-viewer" "$APP/Contents/MacOS/"
# the app opens the viewer's menu (--app), as the Windows and Linux launchers do
cat > "$APP/Contents/MacOS/SwitchFrameTap" <<'SH'
#!/bin/sh
exec "$(dirname "$0")/switch-frame-tap-viewer" --app "$@"
SH
chmod +x "$APP/Contents/MacOS/SwitchFrameTap"
MINOS=$(otool -l "$OUT/switch-frame-tap-viewer" | awk '/minos/{print $2; exit}')
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Switch Frame Tap</string>
    <key>CFBundleDisplayName</key><string>Switch Frame Tap</string>
    <key>CFBundleIdentifier</key><string>io.github.tomasuribe.switchframetap</string>
    <key>CFBundleVersion</key><string>$VER</string>
    <key>CFBundleShortVersionString</key><string>$VER</string>
    <key>CFBundleExecutable</key><string>SwitchFrameTap</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>LSMinimumSystemVersion</key><string>${MINOS:-11.0}</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
mkdir -p "$OUT/licenses"
cp "$REPO/LICENSE" "$OUT/licenses/switch-frame-tap-GPL-2.0.txt"
cp "$FF/COPYING.LGPLv2.1" "$OUT/licenses/FFmpeg-LGPL-2.1.txt"
cp "$SDLP/share/licenses/sdl2/LICENSE.txt" "$OUT/licenses/SDL2-zlib.txt" 2>/dev/null || true
cp "$USBP/COPYING" "$OUT/licenses/libusb-LGPL-2.1.txt" 2>/dev/null || true
cat > "$OUT/README.txt" <<TXT
Switch Frame Tap $VER - the viewer for macOS ($ARCH, macOS ${MINOS:-11.0} or newer)

Drag "Switch Frame Tap.app" to Applications. It is not signed by Apple: the
first time, right-click it and choose Open (or run
  xattr -dr com.apple.quarantine "/Applications/Switch Frame Tap.app"
in Terminal). No driver is needed for USB on macOS.

FFmpeg (H.264, AAC, MP4), SDL 2 and libusb are built in.
https://github.com/TomasUribe/switch-frame-tap
TXT
ZIP="$REPO/dist/switch-frame-tap-$VER-macos-$ARCH.zip"
rm -f "$ZIP"
(cd "$OUT" && ditto -c -k --sequesterRsrc . "$ZIP")
echo "== $ZIP"
