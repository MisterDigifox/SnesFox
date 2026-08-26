#include "../core/snesfox_app.hpp"
#include "embedded_rom.generated.hpp"

#include <filesystem>
#include <fstream>
#include <string>

// Rom/SnesFoxApp only load from a file path, so the baked-in bytes are materialized to a
// temp file once at startup and handed to the exact same bare-game-window path `snesfox
// <rom>` takes — no CLI args, no ROM picker, no debug UI.
int main(int argc, char** argv) {
    const std::filesystem::path romPath =
        std::filesystem::temp_directory_path() / "snesfoxgame_embedded.sfc";
    {
        std::ofstream out(romPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(kEmbeddedRomData),
                   static_cast<std::streamsize>(kEmbeddedRomSize));
    }

    std::string pathStr = romPath.string();
    char* fakeArgv[] = {argv[0], pathStr.data()};
    SnesFoxApp app;
    return app.run(2, fakeArgv);
}
