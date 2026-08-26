#include "core/snesfox_app.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        SnesFoxApp app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
