#pragma once

#include <filesystem>

struct DebugFixture
{
    std::filesystem::path source;
    bool text = false;
};