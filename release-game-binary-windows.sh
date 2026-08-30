#!/bin/bash
set -e

# Cross-compiles a standalone Windows x86_64 kiosk-mode game binary (ROM embedded,
# see release-game-binary.sh for the macOS equivalent) from macOS/Linux using mingw-w64.
#
# One-time setup:
#   brew install mingw-w64
#
# SDL2 isn't a homebrew package for the mingw target, so this script downloads the
# official "mingw devel" tarball from SDL's GitHub releases the first time it runs
# and caches it under .cache/SDL2-mingw/ (gitignored).
#
# Windows has no NSOpenPanel/Cocoa menu bar equivalent to src/macOS/native_file_dialog.mm,
# so this links src/windows/native_file_dialog.cpp instead (native GetOpenFileName dialog,
# no-op menu-bar hooks) against the same shared native_file_dialog.hpp interface.

if [ -z "$1" ]; then
  echo "Erreur: ROM_SRC requis en paramètre." >&2
  echo "Usage: $0 <ROM_SRC>" >&2
  exit 1
fi

ROM_SRC="$1"
ROM_NAME="$(basename "$ROM_SRC" .sfc)"
GEN_HEADER="src/game/embedded_rom.generated.hpp"

MINGW_TRIPLE="x86_64-w64-mingw32"
MINGW_CXX="${MINGW_TRIPLE}-g++"

if ! command -v "$MINGW_CXX" >/dev/null 2>&1; then
  echo "error: $MINGW_CXX not found. Install it with: brew install mingw-w64" >&2
  exit 1
fi

echo "Downloading SDL2 for Windows"
SDL2_VERSION="2.30.9"
SDL2_CACHE_DIR=".cache/SDL2-mingw"
SDL2_TARBALL="SDL2-devel-${SDL2_VERSION}-mingw.tar.gz"
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/${SDL2_TARBALL}"
SDL2_ROOT="${SDL2_CACHE_DIR}/SDL2-${SDL2_VERSION}/${MINGW_TRIPLE}"

if [ ! -d "$SDL2_ROOT" ]; then
  echo "Fetching SDL2 ${SDL2_VERSION} mingw devel package..."
  mkdir -p "$SDL2_CACHE_DIR"
  curl -fL -o "${SDL2_CACHE_DIR}/${SDL2_TARBALL}" "$SDL2_URL"
  tar -xzf "${SDL2_CACHE_DIR}/${SDL2_TARBALL}" -C "$SDL2_CACHE_DIR"
fi

xxd -i "$ROM_SRC" \
  | sed -e 's/unsigned char [A-Za-z0-9_]*\[\]/unsigned char kEmbeddedRomData[]/' \
        -e 's/unsigned int [A-Za-z0-9_]*_len/unsigned int kEmbeddedRomSize/' \
  > "$GEN_HEADER"

rm -f "$ROM_NAME.exe" SDL2.dll

echo "Compiling"
"$MINGW_CXX" -std=c++20 -O2 -DSNESFOX_KIOSK_MODE=1 "-DSNESFOX_APP_NAME=\"$ROM_NAME\"" \
  src/game/main_game.cpp src/core/*.cpp src/windows/*.cpp tests/*.cpp imgui/*.cpp imgui/backends/*.cpp \
  -o "$ROM_NAME.exe" \
  -I. \
  -Iimgui \
  -Iimgui/backends \
  -I"${SDL2_ROOT}/include" \
  -I"${SDL2_ROOT}/include/SDL2" \
  -lmingw32 \
  "${SDL2_ROOT}/lib/libSDL2main.a" \
  "${SDL2_ROOT}/lib/libSDL2.dll.a" \
  -lcomdlg32 \
  -static-libgcc -static-libstdc++ -static -lpthread

echo "Removing previous Game directory"
rm -rf Game

echo "Creating Game directory"
mkdir -p Game

# SDL2.dll is dynamically loaded at runtime — $ROM_NAME.exe won't start without it next to it.
cp "${SDL2_ROOT}/bin/SDL2.dll" Game/

mv "${ROM_NAME}.exe" Game/

echo "Built $ROM_NAME.exe — ship it together with SDL2.dll (copied alongside it here)."
