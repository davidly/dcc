#include "dcc_host_full_cpm.hpp"
#include "dcc_host_mi_server.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#ifndef DCC_DEBUG_HOST_ASSETS_DIR
#define DCC_DEBUG_HOST_ASSETS_DIR "assets"
#endif

#ifndef DCC_DEBUG_HOST_ENV_FILE
#define DCC_DEBUG_HOST_ENV_FILE "dcc_debug_env.txt"
#endif

namespace fs = std::filesystem;

namespace
{
FullCpmOptions default_options()
{
    FullCpmOptions options;
    options.drive_a = DCC_DEBUG_HOST_ASSETS_DIR "/disks/cpm63k.dsk";
    options.drive_c = DCC_DEBUG_HOST_ASSETS_DIR "/disks/disk_c_blank.dsk";
    options.drive_d = DCC_DEBUG_HOST_ASSETS_DIR "/disks/disk_d_blank.dsk";
    options.environment_file = DCC_DEBUG_HOST_ENV_FILE;
    return options;
}

bool parse_options(int argc, char **argv, FullCpmOptions &options)
{
    for (int index = 1; index < argc; ++index)
    {
        std::string argument = argv[index];
        auto require_path = [&](fs::path &destination)
        {
            if (++index >= argc)
                return false;
            destination = fs::absolute(argv[index]);
            return true;
        };

        if (argument == "--drive-a")
        {
            if (!require_path(options.drive_a)) return false;
        }
        else if (argument == "--drive-c")
        {
            if (!require_path(options.drive_c)) return false;
        }
        else if (argument == "--drive-d")
        {
            if (!require_path(options.drive_d)) return false;
        }
        else if (argument == "--env-file")
        {
            if (!require_path(options.environment_file)) return false;
        }
        else if (argument == "--io-adapter")
        {
            if (!require_path(options.io_adapter)) return false;
        }
        else if (argument == "--terminal-endpoint-file")
        {
            if (!require_path(options.terminal_endpoint_file)) return false;
        }
        else if (argument == "--save-fixtures")
        {
            if (!require_path(options.save_fixtures_directory)) return false;
        }
        else if (argument == "--fixture" || argument == "--text-fixture")
        {
            if (++index >= argc)
                return false;
            options.fixtures.push_back({fs::absolute(argv[index]), argument == "--text-fixture"});
        }
        else if (argument != "--interpreter=mi" && argument != "-interpreter=mi")
        {
            std::cerr << "unknown option: " << argument << '\n';
            return false;
        }
    }
    return true;
}
}

int main(int argc, char **argv)
{
    FullCpmOptions options = default_options();
    std::string error;

    if (!parse_options(argc, argv, options))
    {
        std::cerr << "usage: dcc-debug-host --interpreter=mi\n"
                     "  [--fixture FILE] [--text-fixture FILE]\n"
                     "  [--drive-a IMAGE] [--drive-c IMAGE] [--drive-d IMAGE]\n"
                     "  [--io-adapter LIBRARY]\n"
                     "  [--env-file FILE]\n"
                     "  [--save-fixtures DIR]\n"
                     "  [--terminal-endpoint-file FILE]\n";
        return 2;
    }

    FullCpmHost host(std::move(options));
    if (!host.initialize(error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    MiServer server(host);
    return server.run();
}
