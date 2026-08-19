#!/bin/sh
# Assembles a real, double-clickable Applescreen.app - release builds of the
# core dylib and the Swift app, bundled together and ad-hoc signed. Output
# goes to dist/ (gitignored, not committed).
#
# This is still local/ad-hoc signing, not Developer ID + notarization -
# fine for running on your own machine, but Gatekeeper will still complain
# on a machine that didn't build it (right-click -> Open works around
# that). See docs/RISKS.md for why ad-hoc signing is enough for how this
# app is used, and docs/ARCHITECTURE.md for what's still deferred before
# real distribution.
set -e
cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"
APP_NAME="Applescreen"
DIST_DIR="$REPO_ROOT/dist"
APP_BUNDLE="$DIST_DIR/$APP_NAME.app"

echo "==> Building core (release)"
scripts/build_core.sh Release

echo "==> Building app (release)"
(cd app && swift build -c release)

echo "==> Assembling $APP_BUNDLE"
rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS" "$APP_BUNDLE/Contents/Resources"

cp "app/.build/release/$APP_NAME" "$APP_BUNDLE/Contents/MacOS/$APP_NAME"
cp "build/core/libapplescreen_core.dylib" "$APP_BUNDLE/Contents/Resources/libapplescreen_core.dylib"
cp "app/Info.plist" "$APP_BUNDLE/Contents/Info.plist"
printf 'APPL????' > "$APP_BUNDLE/Contents/PkgInfo"

echo "==> Ad-hoc signing $APP_BUNDLE"
codesign --force --deep --sign - "$APP_BUNDLE"

echo "==> Verifying signature"
codesign -dv --verify "$APP_BUNDLE" 2>&1 | sed 's/^/    /'

if command -v hdiutil >/dev/null 2>&1; then
    DMG_PATH="$DIST_DIR/$APP_NAME.dmg"
    echo "==> Building $DMG_PATH"
    rm -f "$DMG_PATH"
    hdiutil create -volname "$APP_NAME" -srcfolder "$APP_BUNDLE" -ov -format UDZO "$DMG_PATH" >/dev/null
fi

echo
echo "Done:"
echo "  $APP_BUNDLE"
[ -f "$DMG_PATH" ] && echo "  $DMG_PATH"
echo
echo "First launch on this Mac: open \"$APP_BUNDLE\""
echo "First launch on another Mac (unsigned by a Developer ID - Gatekeeper will warn):"
echo "  right-click the app -> Open, or: xattr -d com.apple.quarantine \"$APP_BUNDLE\""
