#!/usr/bin/env bash
# The macOS release as Mac users install it: "Silent Hill PC.app" in a disk image, dragged to Applications. Made
# from the checked tar.gz package of tools/ci/package.sh (so check_package.py has seen every file), never from a
# build folder.
#
#   Contents/MacOS/      SilentHillPC, sh-import, sh-upscale, the FFmpeg dylibs, libMoltenVK.dylib
#   Contents/Resources/  everything else of the package (assets/, gamedata/, fonts/, licenses/, config.cfg, ...)
#
# A bundle cannot be written to: started from it, the game keeps its root in ~/Library/Application Support/
# Silent Hill PC/ (src/engine/app/app_bundle.h) and imports the disc image there.
#
# Signing: with SH_MACOS_SIGN_IDENTITY (a "Developer ID Application: ..." identity in the keychain) the code is
# signed with the hardened runtime; with SH_NOTARY_APPLE_ID, SH_NOTARY_TEAM_ID and SH_NOTARY_PASSWORD (an
# app-specific password) the image is also notarized and stapled, and opens without a warning. Without them the
# app is signed ad hoc: macOS then asks once to allow it (System Settings > Privacy & Security > Open Anyway).
#
# Usage: tools/ci/package_dmg.sh <SilentHillPC-<version>-macos-<cpu>.tar.gz> [<out-dir>]
#   writes <out-dir>/SilentHillPC-<version>-macos-<cpu>.dmg
set -euo pipefail

[[ "$(uname -s)" == Darwin ]] || { echo "package_dmg: hdiutil and codesign are macOS tools" >&2; exit 2; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARCHIVE="$(cd "$(dirname "${1:?package archive}")" && pwd)/$(basename "$1")"
OUT="${2:-$(dirname "$ARCHIVE")}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
NAME="$(basename "$ARCHIVE" .tar.gz)"
[[ "$NAME" == SilentHillPC-*-macos-* ]] || { echo "package_dmg: not a macOS package: $ARCHIVE" >&2; exit 2; }
VERSION="${NAME#SilentHillPC-}"
VERSION="${VERSION%-macos-*}"
ENGINE="$(head -n 1 "$ROOT/VERSION" | tr -d '[:space:]')"
APP_NAME="Silent Hill PC"
BUNDLE_ID="io.github.keklick1337.silent-hill-pc"
MIN_MACOS="${MACOSX_DEPLOYMENT_TARGET:-13.3}"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/sh-dmg.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

tar -xzf "$ARCHIVE" -C "$WORK"
PKG="$WORK/$NAME"
[[ -x "$PKG/SilentHillPC" ]] || { echo "package_dmg: $ARCHIVE holds no $NAME/SilentHillPC" >&2; exit 1; }

STAGE="$WORK/stage"
APP="$STAGE/$APP_NAME.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# Code into MacOS/, the rest into Resources/.
shopt -s nullglob
for f in "$PKG/SilentHillPC" "$PKG/sh-import" "$PKG/sh-upscale" "$PKG"/*.dylib; do
    mv "$f" "$APP/Contents/MacOS/"
done
shopt -u nullglob
(cd "$PKG" && tar -cf - .) | (cd "$APP/Contents/Resources" && tar -xf -)

# The icon, from the project's one master picture (assets/icon/).
ICONSET="$WORK/AppIcon.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$ROOT/assets/icon/master.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    [[ $double -le 512 ]] && sips -z "$double" "$double" "$ROOT/assets/icon/master.png" \
        --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

cat >"$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleDisplayName</key><string>$APP_NAME</string>
    <key>CFBundleExecutable</key><string>SilentHillPC</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>$APP_NAME</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$ENGINE</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.adventure-games</string>
    <key>LSMinimumSystemVersion</key><string>$MIN_MACOS</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
    <key>NSHumanReadableCopyright</key><string>GPL-3.0-or-later. Plays your own disc image of Silent Hill; no game files included.</string>
</dict>
</plist>
EOF

# Inside out: every Mach-O file first, the bundle last. With a Developer ID the hardened runtime is on; the user's
# own Vulkan SDK or FFmpeg are other teams' libraries, so library validation is off.
IDENTITY="${SH_MACOS_SIGN_IDENTITY:-}"
ENTITLEMENTS="$WORK/entitlements.plist"
cat >"$ENTITLEMENTS" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict>
</plist>
EOF
sign() {
    if [[ -n "$IDENTITY" ]]; then
        codesign --force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$@"
    else
        codesign --force --sign - "$@"
    fi
}
for f in "$APP/Contents/MacOS"/*; do
    sign "$f" 2>/dev/null
done
sign "$APP" 2>/dev/null
codesign --verify --strict --deep "$APP"
echo "package_dmg: signed ${IDENTITY:-ad hoc}"

ln -s /Applications "$STAGE/Applications"
DMG="$OUT/$NAME.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "$APP_NAME" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DMG"

if [[ -n "$IDENTITY" ]]; then
    codesign --force --timestamp --sign "$IDENTITY" "$DMG"
    if [[ -n "${SH_NOTARY_APPLE_ID:-}" && -n "${SH_NOTARY_TEAM_ID:-}" && -n "${SH_NOTARY_PASSWORD:-}" ]]; then
        xcrun notarytool submit "$DMG" --apple-id "$SH_NOTARY_APPLE_ID" --team-id "$SH_NOTARY_TEAM_ID" \
            --password "$SH_NOTARY_PASSWORD" --wait
        xcrun stapler staple "$DMG"
        echo "package_dmg: notarized"
    fi
fi
echo "package_dmg: $DMG"
