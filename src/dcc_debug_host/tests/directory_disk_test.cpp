#include "directory_disk.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C"
{
#include "universal_88dcdd.h"
}

namespace
{
namespace fs = std::filesystem;

void write_file(const fs::path &path, size_t size, uint8_t value)
{
    fs::create_directories(path.parent_path());
    std::vector<uint8_t> data(size, value);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output.write(reinterpret_cast<const char *>(data.data()),
                        static_cast<std::streamsize>(data.size())).good());
}

void assert_blank_directory(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    assert(image.size() >= 77 * 32 * 137);
    for (size_t sector = 0; sector < 32; ++sector)
    {
        size_t data = (2 * 32 + sector) * 137 + 3;
        for (size_t index = 0; index < 128; ++index)
            assert(image[data + index] == 0xe5);
    }
}

bool build(const fs::path &template_path, const std::vector<DebugFixture> &files,
           std::string &error)
{
    std::vector<uint8_t> image;
    bool result = DirectoryDisk::build(template_path, files, image, error);
    if (result)
        assert(image.size() == 77 * 32 * 137);
    return result;
}
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    fs::path template_path = argv[1];
    assert_blank_directory(argv[2]);
    assert_blank_directory(argv[3]);
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path() /
                    ("dcc-debug-host-directory-disk-test-" + std::to_string(stamp));
    std::string error;

    fs::path exact = root / "files" / "EXACT.COM";
    fs::path next = root / "files" / "NEXT.COM";
    fs::path empty = root / "files" / "EMPTY.DAT";
    fs::path text = root / "files" / "TEXT.DAT";
    write_file(exact, 16384, 0x11);
    write_file(next, 16385, 0x22);
    write_file(empty, 0, 0);
    fs::create_directories(text.parent_path());
    {
        std::ofstream output(text, std::ios::binary);
        output << "one\ntwo\r\n";
    }
    std::vector<uint8_t> round_trip_image;
    assert(DirectoryDisk::build(template_path,
                                {{exact, false}, {next, false}, {empty, false}, {text, true}},
                                round_trip_image, error));
    std::vector<DirectoryDiskFile> extracted;
    assert(DirectoryDisk::extract(round_trip_image, extracted, error));
    auto extracted_file = [&](const std::string &name) -> const DirectoryDiskFile &
    {
        auto found = std::find_if(extracted.begin(), extracted.end(),
            [&](const DirectoryDiskFile &file) { return file.name == name; });
        assert(found != extracted.end());
        return *found;
    };
    const DirectoryDiskFile &extracted_exact = extracted_file("EXACT.COM");
    const DirectoryDiskFile &extracted_next = extracted_file("NEXT.COM");
    const DirectoryDiskFile &extracted_empty = extracted_file("EMPTY.DAT");
    const DirectoryDiskFile &extracted_text = extracted_file("TEXT.DAT");
    assert(extracted_exact.data.size() == 16384);
    assert(std::all_of(extracted_exact.data.begin(), extracted_exact.data.end(),
                       [](uint8_t value) { return value == 0x11; }));
    assert(extracted_next.data.size() == 16512);
    assert(std::all_of(extracted_next.data.begin(), extracted_next.data.begin() + 16385,
                       [](uint8_t value) { return value == 0x22; }));
    assert(extracted_empty.data.size() == 128 && extracted_empty.data[0] == 0);
    const std::string expected_text = "one\r\ntwo\r\n\x1a";
    assert(extracted_text.data.size() == 128);
    assert(std::equal(expected_text.begin(), expected_text.end(), extracted_text.data.begin()));

    std::vector<uint8_t> memory_drive;
    error.clear();
    assert(DirectoryDisk::build(template_path, {{exact, false}}, memory_drive, error));
    fs::path drive_a = root / "drive-a.dsk";
    fs::path drive_c = root / "drive-c.dsk";
    fs::path drive_d = root / "drive-d.dsk";
    write_file(drive_a, memory_drive.size(), 0);
    write_file(drive_c, memory_drive.size(), 0);
    write_file(drive_d, memory_drive.size(), 0);
    assert(host_disk_init_memory_b(drive_a.string().c_str(), memory_drive.data(),
                                   memory_drive.size(), drive_c.string().c_str(),
                                   drive_d.string().c_str()));
    disk_controller_t controller = host_disk_controller();
    controller.disk_select(1);
    controller.sector();
    for (size_t index = 0; index < 137; ++index)
        assert(controller.read() == memory_drive[index]);
    controller.sector();
    for (size_t index = 0; index < 138; ++index)
        controller.write(0xa5);
    for (size_t index = 137; index < 274; ++index)
        assert(memory_drive[index] == 0xa5);
    host_disk_close();

    fs::path full = root / "files" / "FULL.BIN";
    write_file(full, 148 * 2048, 0x33);
    error.clear();
    assert(build(template_path, {{full, false}}, error));

    fs::path overflow = root / "files" / "OVER.BIN";
    write_file(overflow, 148 * 2048 + 1, 0x44);
    error.clear();
    assert(!build(template_path, {{overflow, false}}, error));
    assert(error.find("full") != std::string::npos);

    fs::path upper = root / "one" / "SAME.DAT";
    fs::path lower = root / "two" / "same.dat";
    write_file(upper, 1, 0x55);
    write_file(lower, 1, 0x66);
    error.clear();
    assert(!build(template_path, {{upper, false}, {lower, false}}, error));
    assert(error.find("duplicate CP/M filename") != std::string::npos);

    std::vector<DebugFixture> directory_files;
    for (int index = 0; index < 129; ++index)
    {
        char name[13];
        std::snprintf(name, sizeof(name), "F%03d.DAT", index);
        fs::path path = root / "many" / name;
        write_file(path, 1, static_cast<uint8_t>(index));
        directory_files.push_back({path, false});
    }
    std::vector<DebugFixture> exact_directory(directory_files.begin(), directory_files.begin() + 128);
    error.clear();
    assert(build(template_path, exact_directory, error));
    error.clear();
    assert(!build(template_path, directory_files, error));
    assert(error.find("directory entries") != std::string::npos);
    assert(!fs::exists(root / "host-directory-88dcdd.dsk"));

    fs::remove_all(root);
    return 0;
}