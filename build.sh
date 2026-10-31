#!/bin/zsh
# Builds DSS1.app next to this script. Run: ./build.sh
# ./build.sh test   also compiles the CRATE_* test hooks (never ship that build).
set -euo pipefail
cd "$(dirname "$0")"

# Fall back to the Command Line Tools if Xcode is installed but its license isn't accepted yet.
if ! xcrun --find swift >/dev/null 2>&1 || ! swift --version >/dev/null 2>&1; then
  export DEVELOPER_DIR=/Library/Developer/CommandLineTools
fi

FLAGS=()
[[ "${1:-}" == "test" ]] && FLAGS=(-Xswiftc -DTESTHOOKS)
swift build -c release "${FLAGS[@]}"

APP=DSS1.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp .build/release/DSS1 "$APP/Contents/MacOS/DSS1"
cp Resources/Info.plist "$APP/Contents/Info.plist"

# App icon
ICONSET=.build/AppIcon.iconset
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
swift Resources/make_icon.swift .build/icon_1024.png
for s in 16 32 128 256 512; do
  sips -z $s $s .build/icon_1024.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) .build/icon_1024.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

codesign --force --deep --sign - "$APP"
echo "Built $(pwd)/$APP"
