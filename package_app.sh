#!/bin/bash
set -e

# Wraps the already-built ./snesfox CLI binary into a double-clickable SnesFox.app bundle.
# Launched with no arguments (Finder double-click / `open SnesFox.app`), which is exactly
# the bare game-only window (see snesfox_app.cpp's argc<2 path) — no ROM loaded yet, use
# File > Open (or drag a .sfc onto the window/app icon) to pick one.

APP_NAME="SnesFox"
APP_DIR="${APP_NAME}.app"
BUNDLE_ID="org.snesfox.emulator"

./release.sh

rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS"
mkdir -p "$APP_DIR/Contents/Resources"
cp snesfox "$APP_DIR/Contents/MacOS/$APP_NAME"

# Icon: procedurally drawn (tools/make_icon.py), no external art asset — rebuilt every
# package rather than committed, same spirit as the binary itself being a build output.
ICONSET_DIR=$(mktemp -d)/AppIcon.iconset
python3 tools/make_icon.py "$ICONSET_DIR"
iconutil -c icns "$ICONSET_DIR" -o "$APP_DIR/Contents/Resources/AppIcon.icns"
rm -rf "$(dirname "$ICONSET_DIR")"

cat > "$APP_DIR/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.games</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>SNES ROM</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSHandlerRank</key>
            <string>Owner</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>org.snesfox.rom</string>
            </array>
        </dict>
    </array>
    <key>UTExportedTypeDeclarations</key>
    <array>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>org.snesfox.rom</string>
            <key>UTTypeDescription</key>
            <string>SNES ROM</string>
            <key>UTTypeConformsTo</key>
            <array>
                <string>public.data</string>
            </array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array>
                    <string>sfc</string>
                    <string>smc</string>
                </array>
            </dict>
        </dict>
    </array>
</dict>
</plist>
PLIST

# Ad-hoc codesign the whole bundle (same reasoning as release.sh's raw-binary codesign: avoids
# macOS silently killing an unsigned local build).
if command -v codesign >/dev/null 2>&1; then
  codesign --force --deep -s - "$APP_DIR" 2>/dev/null || true
fi

# Refresh Launch Services so Finder picks up the new .sfc/.smc "Open With" association
# without needing a re-login; harmless/no-op if this tool isn't present.
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [ -x "$LSREGISTER" ]; then
  "$LSREGISTER" -f "$APP_DIR" 2>/dev/null || true
fi

echo "Built $APP_DIR — open it with: open $APP_DIR"
