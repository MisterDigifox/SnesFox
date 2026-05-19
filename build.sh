rm snesfox
rm output.asm
rm out.sfc

clang++ -std=c++20 *.cpp -o snesfox \
  -I/opt/homebrew/opt/sdl2/include \
  -I/opt/homebrew/opt/sdl2/include/SDL2 \
  -I/opt/homebrew/opt/sdl2_ttf/include \
  -L/opt/homebrew/opt/sdl2/lib \
  -L/opt/homebrew/opt/sdl2_ttf/lib \
  -lSDL2 -lSDL2_ttf

# macOS: ad-hoc codesign avoids some machines killing unsigned local binaries (symptom: zsh: killed).
if [ "$(uname -s)" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
  codesign --force -s - ./snesfox 2>/dev/null || true
fi
