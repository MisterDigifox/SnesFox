#pragma once

class SnesFoxApp {
public:
    /// Parses CLI and dispatches subcommands. Returns process exit code (0 = success).
    int run(int argc, char** argv);
};
