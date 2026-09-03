#!/bin/bash
set -e

# Cross-compiles snesfox.exe (Windows x86_64) from macOS/Linux using mingw-w64.
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

MINGW_TRIPLE="x86_64-w64-mingw32"
MINGW_CXX="${MINGW_TRIPLE}-g++"

if ! command -v "$MINGW_CXX" >/dev/null 2>&1; then
  echo "error: $MINGW_CXX not found. Install it with: brew install mingw-w64" >&2
  exit 1
fi

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

rm -f snesfox.exe SDL2.dll

"$MINGW_CXX" -std=c++20 -O2 \
  src/*.cpp src/core/*.cpp src/windows/*.cpp tests/*.cpp imgui/*.cpp imgui/backends/*.cpp \
  -o snesfox.exe \
  -I. \
  -Iimgui \
  -Iimgui/backends \
  -I"${SDL2_ROOT}/include" \
  -I"${SDL2_ROOT}/include/SDL2" \
  -lmingw32 \
  "${SDL2_ROOT}/lib/libSDL2main.a" \
  "${SDL2_ROOT}/lib/libSDL2.dll.a" \
  -lcomdlg32 -lwinmm \
  -static-libgcc -static-libstdc++ -static -lpthread

# SDL2.dll is dynamically loaded at runtime — snesfox.exe won't start without it next to it.
cp "${SDL2_ROOT}/bin/SDL2.dll" .

echo "Built snesfox.exe — ship it together with SDL2.dll (copied alongside it here)."
