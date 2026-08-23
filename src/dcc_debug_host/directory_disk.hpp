#pragma once

#include "dcc_host_debug_fixture.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct DirectoryDiskFile
{
    std::string name;
    std::vector<uint8_t> data;
};

class DirectoryDisk
{
public:
    static bool build(const std::filesystem::path &raw_template,
                      const std::vector<DebugFixture> &files,
                      std::vector<uint8_t> &raw_image,
                      std::string &error);
    static bool extract(const std::vector<uint8_t> &raw_image,
                        std::vector<DirectoryDiskFile> &files,
                        std::string &error);
};
