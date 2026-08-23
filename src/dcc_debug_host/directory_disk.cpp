#include "directory_disk.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr size_t kTracks = 77;
constexpr size_t kSectorsPerTrack = 32;
constexpr size_t kPhysicalSectorSize = 137;
constexpr size_t kLogicalSectorSize = 128;
constexpr size_t kBlockSize = 2048;
constexpr size_t kExtentSize = 16384;
constexpr size_t kDirectoryEntries = 128;
constexpr size_t kDirectoryOffset = 2 * kSectorsPerTrack * kLogicalSectorSize;
constexpr size_t kDirectorySize = kDirectoryEntries * 32;
constexpr size_t kRawSize = kTracks * kSectorsPerTrack * kPhysicalSectorSize;
constexpr size_t kLogicalSize = kTracks * kSectorsPerTrack * kLogicalSectorSize;
constexpr uint8_t kFirstDataBlock = 2;
constexpr uint8_t kLastDataBlock = 149;
constexpr std::array<uint8_t, kSectorsPerTrack> kSectorTranslation = {
    0, 8, 16, 24, 2, 10, 18, 26, 4, 12, 20, 28, 6, 14, 22, 30,
    1, 9, 17, 25, 3, 11, 19, 27, 5, 13, 21, 29, 7, 15, 23, 31};

std::string upper(std::string value)
{
    for (char &character : value)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    return value;
}

bool split_cpm_name(const fs::path &path, std::string &base, std::string &extension)
{
    std::string name = upper(path.filename().string());
    size_t dot = name.find('.');
    base = dot == std::string::npos ? name : name.substr(0, dot);
    extension = dot == std::string::npos ? "" : name.substr(dot + 1);
    if (base.empty() || base.size() > 8 || extension.size() > 3 ||
        (dot != std::string::npos && name.find('.', dot + 1) != std::string::npos))
        return false;
    for (char character : base + extension)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_')
            return false;
    return true;
}

bool read_file(const fs::path &path, std::vector<uint8_t> &data)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0)
        return false;
    data.resize(static_cast<size_t>(size));
    return input.read(reinterpret_cast<char *>(data.data()), size).good();
}

std::vector<uint8_t> text_payload(const std::vector<uint8_t> &source)
{
    std::vector<uint8_t> result;
    result.reserve(source.size() + 128);
    for (uint8_t character : source)
    {
        if (character == '\n' && (result.empty() || result.back() != '\r'))
            result.push_back('\r');
        result.push_back(character);
    }
    if (result.empty() || result.back() != 0x1a)
        result.push_back(0x1a);
    return result;
}

bool add_file(std::vector<uint8_t> &logical, const DebugFixture &file,
              size_t &directory_entry, uint8_t &next_block, std::string &error)
{
    std::string base;
    std::string extension;
    std::vector<uint8_t> source;

    if (!split_cpm_name(file.source, base, extension))
    {
        error = "file is not CP/M 8.3-compatible: " + file.source.string();
        return false;
    }
    if (!read_file(file.source, source))
    {
        error = "cannot read host file: " + file.source.string();
        return false;
    }
    if (file.text)
        source = text_payload(source);
    if (source.empty())
        source.push_back(0);

    size_t source_offset = 0;
    unsigned extent_number = 0;
    while (source_offset < source.size())
    {
        if (directory_entry >= kDirectoryEntries)
        {
            error = "directory-backed drive has no free directory entries";
            return false;
        }
        size_t extent_bytes = std::min(kExtentSize, source.size() - source_offset);
        size_t block_count = (extent_bytes + kBlockSize - 1) / kBlockSize;
        if (next_block + block_count - 1 > kLastDataBlock)
        {
            error = "directory-backed drive is full";
            return false;
        }

        size_t entry_offset = kDirectoryOffset + directory_entry * 32;
        uint8_t *entry = logical.data() + entry_offset;
        std::fill_n(entry, 32, uint8_t{0});
        entry[0] = 0;
        std::fill_n(entry + 1, 8, static_cast<uint8_t>(' '));
        std::fill_n(entry + 9, 3, static_cast<uint8_t>(' '));
        std::copy(base.begin(), base.end(), entry + 1);
        std::copy(extension.begin(), extension.end(), entry + 9);
        entry[12] = static_cast<uint8_t>(extent_number & 0x1f);
        entry[13] = 0;
        entry[14] = static_cast<uint8_t>((extent_number >> 5) & 0x3f);
        entry[15] = static_cast<uint8_t>((extent_bytes + 127) / 128);

        for (size_t block = 0; block < block_count; ++block)
        {
            uint8_t block_number = next_block++;
            entry[16 + block] = block_number;
            size_t destination = kDirectoryOffset + static_cast<size_t>(block_number) * kBlockSize;
            size_t bytes = std::min(kBlockSize, source.size() - source_offset);
            std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(source_offset),
                        bytes,
                        logical.begin() + static_cast<std::ptrdiff_t>(destination));
            source_offset += bytes;
        }
        ++directory_entry;
        ++extent_number;
    }
    return true;
}
}

bool DirectoryDisk::build(const fs::path &raw_template,
                          const std::vector<DebugFixture> &files,
                          std::vector<uint8_t> &raw_image,
                          std::string &error)
{
    std::vector<uint8_t> raw;
    std::vector<uint8_t> logical(kLogicalSize, 0);
    size_t directory_entry = 0;
    uint8_t next_block = kFirstDataBlock;

    if (!read_file(raw_template, raw) || raw.size() < kRawSize)
    {
        error = "cannot read 88-DCDD template: " + raw_template.string();
        return false;
    }
    std::fill(logical.begin() + static_cast<std::ptrdiff_t>(kDirectoryOffset),
              logical.begin() + static_cast<std::ptrdiff_t>(kDirectoryOffset + kDirectorySize),
              uint8_t{0xe5});
    std::fill(logical.begin() + static_cast<std::ptrdiff_t>(kDirectoryOffset + kDirectorySize),
              logical.end(), uint8_t{0});

    std::set<std::string> names;
    for (const DebugFixture &file : files)
    {
        std::string base;
        std::string extension;
        if (!split_cpm_name(file.source, base, extension))
        {
            error = "file is not CP/M 8.3-compatible: " + file.source.string();
            return false;
        }
        std::string name = base + (extension.empty() ? "" : "." + extension);
        if (!names.insert(name).second)
        {
            error = "duplicate CP/M filename: " + name;
            return false;
        }
        if (!add_file(logical, file, directory_entry, next_block, error))
            return false;
    }

    std::vector<uint8_t> wrapped(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(kRawSize));
    for (size_t track = 2; track < kTracks; ++track)
    {
        for (size_t logical_sector = 0; logical_sector < kSectorsPerTrack; ++logical_sector)
        {
            size_t raw_sector = kSectorTranslation[logical_sector];
            if (track >= 6)
                raw_sector = (raw_sector * 17) & 0x1f;
            size_t physical = (track * kSectorsPerTrack + raw_sector) * kPhysicalSectorSize;
            size_t source = (track * kSectorsPerTrack + logical_sector) * kLogicalSectorSize;
            size_t data_offset = track < 6 ? 3 : 7;
            std::copy_n(logical.begin() + static_cast<std::ptrdiff_t>(source),
                        kLogicalSectorSize,
                        wrapped.begin() + static_cast<std::ptrdiff_t>(physical + data_offset));
            unsigned checksum = 0;
            for (size_t index = 0; index < kLogicalSectorSize; ++index)
                checksum += logical[source + index];
            if (track < 6)
            {
                wrapped[physical + 131] = 0xff;
                wrapped[physical + 132] = static_cast<uint8_t>(checksum);
            }
            else
            {
                checksum += wrapped[physical + 2];
                checksum += wrapped[physical + 3];
                checksum += wrapped[physical + 5];
                checksum += wrapped[physical + 6];
                wrapped[physical + 4] = static_cast<uint8_t>(checksum);
                wrapped[physical + 135] = 0xff;
                wrapped[physical + 136] = 0x00;
            }
        }
    }
    raw_image.swap(wrapped);
    return true;
}

bool DirectoryDisk::extract(const std::vector<uint8_t> &raw_image,
                            std::vector<DirectoryDiskFile> &files,
                            std::string &error)
{
    std::vector<uint8_t> logical(kLogicalSize, 0);
    std::map<std::string, std::map<unsigned, std::vector<uint8_t>>> extents;

    files.clear();
    if (raw_image.size() < kRawSize)
    {
        error = "88-DCDD image is too small";
        return false;
    }
    for (size_t track = 0; track < kTracks; ++track)
        for (size_t logical_sector = 0; logical_sector < kSectorsPerTrack; ++logical_sector)
        {
            size_t raw_sector = kSectorTranslation[logical_sector];
            if (track >= 6)
                raw_sector = (raw_sector * 17) & 0x1f;
            size_t physical = (track * kSectorsPerTrack + raw_sector) * kPhysicalSectorSize;
            size_t destination = (track * kSectorsPerTrack + logical_sector) * kLogicalSectorSize;
            size_t data_offset = track < 6 ? 3 : 7;
            std::copy_n(raw_image.begin() + static_cast<std::ptrdiff_t>(physical + data_offset),
                        kLogicalSectorSize,
                        logical.begin() + static_cast<std::ptrdiff_t>(destination));
        }

    for (size_t directory_entry = 0; directory_entry < kDirectoryEntries; ++directory_entry)
    {
        const uint8_t *entry = logical.data() + kDirectoryOffset + directory_entry * 32;
        if (entry[0] == 0xe5 || entry[0] != 0)
            continue;
        std::string base;
        std::string extension;
        for (size_t index = 1; index < 9 && (entry[index] & 0x7f) != ' '; ++index)
            base.push_back(static_cast<char>(entry[index] & 0x7f));
        for (size_t index = 9; index < 12 && (entry[index] & 0x7f) != ' '; ++index)
            extension.push_back(static_cast<char>(entry[index] & 0x7f));
        if (base.empty())
        {
            error = "malformed CP/M directory entry";
            return false;
        }
        std::string name = base + (extension.empty() ? "" : "." + extension);
        unsigned extent = (entry[12] & 0x1f) | (static_cast<unsigned>(entry[14] & 0x3f) << 5);
        size_t remaining = static_cast<size_t>(entry[15]) * kLogicalSectorSize;
        std::vector<uint8_t> data;
        data.reserve(remaining);
        for (size_t index = 16; index < 32 && remaining > 0; ++index)
        {
            uint8_t block = entry[index];
            if (block < kFirstDataBlock || block > kLastDataBlock)
            {
                error = "invalid allocation block for " + name;
                return false;
            }
            size_t source = kDirectoryOffset + static_cast<size_t>(block) * kBlockSize;
            size_t bytes = std::min(kBlockSize, remaining);
            if (source + bytes > logical.size())
            {
                error = "allocation block exceeds disk for " + name;
                return false;
            }
            data.insert(data.end(), logical.begin() + static_cast<std::ptrdiff_t>(source),
                        logical.begin() + static_cast<std::ptrdiff_t>(source + bytes));
            remaining -= bytes;
        }
        if (remaining != 0 || !extents[name].emplace(extent, std::move(data)).second)
        {
            error = "incomplete or duplicate extent for " + name;
            return false;
        }
    }

    for (auto &file : extents)
    {
        DirectoryDiskFile result;
        result.name = file.first;
        for (auto &extent : file.second)
            result.data.insert(result.data.end(), extent.second.begin(), extent.second.end());
        files.push_back(std::move(result));
    }
    return true;
}
