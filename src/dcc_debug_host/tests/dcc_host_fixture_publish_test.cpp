#include "dcc_host_fixture_publish.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

void write_file(const fs::path &path, const std::string &contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    assert(output.good());
}

std::string read_file(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

int main()
{
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path() /
                    ("dcc-debug-fixture-publish-test-" + std::to_string(stamp));
    fs::path destination = root / "fixtures";
    fs::path staging = root / ".fixtures.tmp";
    fs::path backup = root / ".fixtures.old";
    std::string error;

    write_file(staging / "FIRST.DAT", "first");
    assert(publish_fixture_directory(staging, destination, backup, false, error));
    assert(read_file(destination / "FIRST.DAT") == "first");
    assert(!fs::exists(staging) && !fs::exists(backup));

    write_file(staging / "SECOND.DAT", "second");
    assert(publish_fixture_directory(staging, destination, backup, true, error));
    assert(!fs::exists(destination / "FIRST.DAT"));
    assert(read_file(destination / "SECOND.DAT") == "second");
    assert(!fs::exists(staging) && !fs::exists(backup));

    error.clear();
    assert(!publish_fixture_directory(staging, destination, backup, true, error));
    assert(error.find("cannot publish fixture directory") != std::string::npos);
    assert(read_file(destination / "SECOND.DAT") == "second");
    assert(!fs::exists(staging) && !fs::exists(backup));

    fs::remove_all(root);
    return 0;
}