rm snesfox
rm output.asm
rm out.sfc

clang++ -std=c++20 src/*.cpp src/core/*.cpp src/macOS/*.mm tests/*.cpp imgui/*.cpp imgui/backends/*.cpp -o snesfox \
  -I. \
  -Iimgui \
  -Iimgui/backends \
  -I/opt/homebrew/opt/sdl2/include \
  -I/opt/homebrew/opt/sdl2/include/SDL2 \
  -L/opt/homebrew/opt/sdl2/lib \
  -lSDL2 \
  -framework Cocoa \
  -framework UniformTypeIdentifiers

# macOS: ad-hoc codesign avoids some machines killing unsigned local binaries (symptom: zsh: killed).
if [ "$(uname -s)" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
  codesign --force -s - ./snesfox 2>/dev/null || true
fi
