/**
 * @file dcc_host_fixture_publish.hpp
 * @brief Atomic fixture-directory publication boundary.
 */
#pragma once

#include <filesystem>
#include <string>

bool publish_fixture_directory(const std::filesystem::path &staging,
                               const std::filesystem::path &destination,
                               const std::filesystem::path &backup,
                               bool destination_exists,
                               std::string &error);