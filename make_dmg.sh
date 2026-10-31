#!/bin/zsh
# Builds a universal (Apple Silicon + Intel) DSS1.app and packages it as dist/DSS1-<version>.dmg
# for sharing. Run: ./make_dmg.sh
set -euo pipefail
cd "$(dirname "$0")"

swift build -c release --arch arm64 --arch x86_64

VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" Resources/Info.plist)
STAGE=.build/dmg
APP="$STAGE/DSS1.app"
rm -rf "$STAGE" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" dist
cp .build/apple/Products/Release/DSS1 "$APP/Contents/MacOS/DSS1"
cp Resources/Info.plist "$APP/Contents/Info.plist"

# Same icon as build.sh
ICONSET=.build/AppIcon.iconset
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
swift Resources/make_icon.swift .build/icon_1024.png
for s in 16 32 128 256 512; do
  sips -z $s $s .build/icon_1024.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) .build/icon_1024.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
codesign --force --deep --sign - "$APP"

ln -s /Applications "$STAGE/Applications"
cp Resources/READ_ME_FIRST.txt "$STAGE/READ ME FIRST.txt"

DMG="dist/DSS1-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "DSS1" -srcfolder "$STAGE" -format UDZO -ov "$DMG" >/dev/null
echo "Built $(pwd)/$DMG"
