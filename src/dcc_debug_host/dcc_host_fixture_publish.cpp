/**
 * @file dcc_host_fixture_publish.cpp
 * @brief Publishes staged fixtures while preserving an existing destination.
 */
#include "dcc_host_fixture_publish.hpp"

namespace fs = std::filesystem;

bool publish_fixture_directory(const fs::path &staging,
                               const fs::path &destination,
                               const fs::path &backup,
                               bool destination_exists,
                               std::string &error)
{
    std::error_code code;
    if (destination_exists)
    {
        fs::rename(destination, backup, code);
        if (code)
        {
            error = "cannot preserve existing fixture directory: " + code.message();
            fs::remove_all(staging, code);
            return false;
        }
    }
    fs::rename(staging, destination, code);
    if (code)
    {
        std::string publish_error = code.message();
        if (destination_exists)
        {
            std::error_code restore_code;
            fs::rename(backup, destination, restore_code);
            if (restore_code)
                publish_error += "; cannot restore previous fixtures: " + restore_code.message();
        }
        fs::remove_all(staging, code);
        error = "cannot publish fixture directory: " + publish_error;
        return false;
    }
    if (destination_exists)
        fs::remove_all(backup, code);
    return true;
}