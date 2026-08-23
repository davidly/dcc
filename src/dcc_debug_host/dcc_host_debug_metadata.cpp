#include "dcc_host_debug_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <limits>

namespace
{
bool quoted_fields(const std::string &text, std::vector<std::string> &fields)
{
    size_t position = 0;

    fields.clear();
    while ((position = text.find('"', position)) != std::string::npos)
    {
        std::string value;
        bool closed = false;

        ++position;
        while (position < text.size())
        {
            char character = text[position++];
            if (character == '\\' && position < text.size())
            {
                value.push_back(text[position++]);
            }
            else if (character == '"')
            {
                closed = true;
                break;
            }
            else
            {
                value.push_back(character);
            }
        }
        if (!closed)
            return false;
        fields.push_back(value);
    }
    return true;
}

char normalized_path_character(char character)
{
    if (character == '\\')
        character = '/';
#if defined(_WIN32) || defined(__APPLE__)
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
#endif
    return character;
}

bool parse_dimensions(const std::string &text, std::vector<int> &dimensions)
{
    dimensions.clear();
    if (text.empty())
        return true;
    size_t position = 0;
    while (position < text.size() && dimensions.size() < 12)
    {
        char *end = nullptr;
        long value = std::strtol(text.c_str() + position, &end, 10);
        if (end == text.c_str() + position || value < 0 || value > std::numeric_limits<int>::max())
            return false;
        dimensions.push_back(static_cast<int>(value));
        position = static_cast<size_t>(end - text.c_str());
        if (position == text.size())
            return true;
        if (text[position++] != ',')
            return false;
        if (position == text.size())
            return false;
    }
    return position == text.size();
}
}

bool DebugMetadata::load_for_program(const std::filesystem::path &program,
                                     std::string &error)
{
    struct PendingLocation
    {
        uint16_t address;
        std::string function;
        std::string name;
        int offset;
        DebugLocationKind kind;
        int64_t detail;
    };
    std::filesystem::path metadata = program;
    std::ifstream input;
    std::string text;
    std::vector<std::string> fields;
    int source_line;
    unsigned long address;
    std::vector<PendingLocation> pending_locations;

    program_ = std::filesystem::absolute(program);
    metadata.replace_extension(".DBG");
    input.open(metadata);
    if (!input)
    {
        metadata.replace_extension(".dbg");
        input.open(metadata);
    }
    if (!input)
    {
        error = "cannot open debug metadata: " + metadata.string();
        return false;
    }

    if (!std::getline(input, text) || text != "DCCDBG 2")
    {
        error = "unsupported debug metadata header in " + metadata.string();
        return false;
    }

    lines_.clear();
    variables_.clear();
    structs_.clear();
    fields_.clear();
    functions_.clear();
    while (std::getline(input, text))
    {
        if (!text.empty() && text.back() == '\r')
            text.pop_back();
        if (std::sscanf(text.c_str(), "line %lx %d", &address, &source_line) == 2)
        {
            if (address > 0xffff || !quoted_fields(text, fields) || fields.empty())
            {
                error = "malformed line record in " + metadata.string();
                return false;
            }
            lines_.push_back({static_cast<uint16_t>(address), source_line, fields[0]});
            continue;
        }
        if (std::sscanf(text.c_str(), "function-begin %lx", &address) == 1)
        {
            if (address > 0xffff || !quoted_fields(text, fields) || fields.empty())
            {
                error = "malformed function-begin record in " + metadata.string();
                return false;
            }
            functions_.push_back({static_cast<uint16_t>(address), 0xffff, fields[0],
                                  fields.size() > 1 ? fields[1] : fields[0]});
            continue;
        }
        if (std::sscanf(text.c_str(), "function-end %lx", &address) == 1)
        {
            if (address > 0xffff || !quoted_fields(text, fields) || fields.empty())
            {
                error = "malformed function-end record in " + metadata.string();
                return false;
            }
            auto function = std::find_if(functions_.rbegin(), functions_.rend(),
                                         [&](const DebugFunction &candidate)
                                         {
                                             return candidate.assembly_name == fields[0];
                                         });
            if (function == functions_.rend())
            {
                error = "function-end without function-begin in " + metadata.string();
                return false;
            }
            function->end = static_cast<uint16_t>(address);
            for (DebugVariable &variable : variables_)
                if (!variable.global && variable.end == 0xffff &&
                    variable.function == fields[0])
                    variable.end = static_cast<uint16_t>(address);
            continue;
        }
        if (text.rfind("variable ", 0) == 0)
        {
            int type, storage, offset, size, is_array, is_vla, element_size;
            int is_function_pointer = 0;
            int parsed = std::sscanf(text.c_str(),
                "variable %lx \"%*[^\"]\" \"%*[^\"]\" %d %d %d %d %d %d %d %d",
                &address, &type, &storage, &offset, &size, &is_array, &is_vla,
                &element_size, &is_function_pointer);
            if ((parsed == 9 || parsed == 8) && address <= 0xffff &&
                quoted_fields(text, fields) && fields.size() >= 3)
            {
                DebugVariable variable;
                variable.declaration = static_cast<uint16_t>(address);
                variable.function = fields[0];
                variable.name = fields[1];
                variable.type = type;
                variable.storage = storage;
                variable.offset = offset;
                variable.size = size;
                variable.is_array = is_array != 0;
                variable.is_vla = is_vla != 0;
                variable.element_size = element_size;
                variable.is_function_pointer = parsed == 9 && is_function_pointer != 0;
                if (!parse_dimensions(fields.back(), variable.dimensions))
                {
                    error = "invalid variable dimensions in " + metadata.string();
                    return false;
                }
                variables_.push_back(std::move(variable));
                continue;
            }
            error = "malformed variable record in " + metadata.string();
            return false;
        }
        if (text.rfind("variable-end ", 0) == 0)
        {
            int offset;
            if (std::sscanf(text.c_str(),
                            "variable-end %lx \"%*[^\"]\" \"%*[^\"]\" %d",
                            &address, &offset) == 2 && address <= 0xffff &&
                quoted_fields(text, fields) && fields.size() >= 2)
            {
                auto variable = std::find_if(variables_.rbegin(), variables_.rend(),
                    [&](const DebugVariable &candidate)
                    {
                        return !candidate.global && candidate.end == 0xffff &&
                               candidate.offset == offset && candidate.function == fields[0] &&
                               candidate.name == fields[1];
                    });
                if (variable != variables_.rend())
                {
                    variable->end = static_cast<uint16_t>(address);
                    continue;
                }
            }
            error = "malformed or unmatched variable-end record in " + metadata.string();
            return false;
        }
        if (text.rfind("location ", 0) == 0)
        {
            int offset;
            long long detail;
            char kind_text[32];
            DebugLocationKind kind;
            if (std::sscanf(text.c_str(),
                    "location %lx \"%*[^\"]\" \"%*[^\"]\" %d %31s %lld",
                    &address, &offset, kind_text, &detail) != 4 ||
                address > 0xffff || !quoted_fields(text, fields) || fields.size() < 2)
            {
                error = "malformed location record in " + metadata.string();
                return false;
            }
            std::string kind_name = kind_text;
            if (kind_name == "out") kind = DebugLocationKind::OptimizedOut;
            else if (kind_name == "frame") kind = DebugLocationKind::Frame;
            else if (kind_name == "hl") kind = DebugLocationKind::HL;
            else if (kind_name == "de") kind = DebugLocationKind::DE;
            else if (kind_name == "bc") kind = DebugLocationKind::BC;
            else if (kind_name == "iy") kind = DebugLocationKind::IY;
            else if (kind_name == "hl_de") kind = DebugLocationKind::HL_DE;
            else if (kind_name == "bc_iy") kind = DebugLocationKind::BC_IY;
            else if (kind_name == "const") kind = DebugLocationKind::Constant;
            else
            {
                error = "unknown optimized location kind in " + metadata.string();
                return false;
            }
            pending_locations.push_back({static_cast<uint16_t>(address), fields[0],
                                         fields[1], offset, kind, detail});
            continue;
        }
        if (text.rfind("global ", 0) == 0)
        {
            int type, size, is_array, is_vla, element_size;
            int is_function_pointer = 0;
            int parsed = std::sscanf(text.c_str(),
                "global %lx \"%*[^\"]\" \"%*[^\"]\" %d %d %d %d %d %d",
                &address, &type, &size, &is_array, &is_vla, &element_size,
                &is_function_pointer);
            if ((parsed == 7 || parsed == 6) && address <= 0xffff &&
                quoted_fields(text, fields) && fields.size() >= 3)
            {
                DebugVariable variable;
                variable.address = static_cast<uint16_t>(address);
                variable.global = true;
                variable.function = "<global>";
                variable.name = fields[1];
                variable.type = type;
                variable.storage = 1;
                variable.size = size;
                variable.is_array = is_array != 0;
                variable.is_vla = is_vla != 0;
                variable.element_size = element_size;
                variable.is_function_pointer = parsed == 7 && is_function_pointer != 0;
                if (!parse_dimensions(fields.back(), variable.dimensions))
                {
                    error = "invalid global dimensions in " + metadata.string();
                    return false;
                }
                variables_.push_back(std::move(variable));
                continue;
            }
            error = "malformed global record in " + metadata.string();
            return false;
        }
        if (text.rfind("struct ", 0) == 0)
        {
            int id, size, is_union;
            if (std::sscanf(text.c_str(), "struct %d %d %d", &id, &size, &is_union) == 3 &&
                quoted_fields(text, fields) && !fields.empty())
            {
                structs_.push_back({id, size, is_union != 0, fields[0]});
                continue;
            }
            error = "malformed struct record in " + metadata.string();
            return false;
        }
        if (text.rfind("field ", 0) == 0)
        {
            int struct_id, type, offset, size, is_array, element_size, bit_width, bit_shift;
            if (std::sscanf(text.c_str(),
                "field %d \"%*[^\"]\" %d %d %d %d %d %d %d",
                &struct_id, &type, &offset, &size, &is_array, &element_size,
                &bit_width, &bit_shift) == 8 && quoted_fields(text, fields) && fields.size() >= 2)
            {
                if (size < 0 || bit_width < 0 || bit_width > 32 || bit_shift < 0 ||
                    (bit_width > 0 && (size <= 0 || bit_width + bit_shift > size * 8)))
                {
                    error = "invalid bitfield metadata in " + metadata.string();
                    return false;
                }
                DebugField field;
                field.struct_id = struct_id;
                field.name = fields[0];
                field.type = type;
                field.offset = offset;
                field.size = size;
                field.is_array = is_array != 0;
                field.element_size = element_size;
                field.bit_width = bit_width;
                field.bit_shift = bit_shift;
                if (!parse_dimensions(fields.back(), field.dimensions))
                {
                    error = "invalid field dimensions in " + metadata.string();
                    return false;
                }
                fields_.push_back(std::move(field));
                continue;
            }
            error = "malformed field record in " + metadata.string();
            return false;
        }
    }

    if (lines_.empty() || functions_.empty())
    {
        error = "debug metadata has no source lines or functions: " + metadata.string();
        return false;
    }
    if (std::any_of(functions_.begin(), functions_.end(),
                    [](const DebugFunction &function) { return function.end == 0xffff; }))
    {
        error = "debug metadata has an unterminated function: " + metadata.string();
        return false;
    }
    for (const PendingLocation &pending : pending_locations)
    {
        auto variable = std::find_if(variables_.rbegin(), variables_.rend(),
            [&](const DebugVariable &candidate)
            {
                return !candidate.global && candidate.function == pending.function &&
                       candidate.name == pending.name && candidate.offset == pending.offset &&
                       candidate.declaration <= pending.address && pending.address < candidate.end;
            });
        if (variable != variables_.rend())
            variable->locations.push_back({pending.address, pending.kind, pending.detail});
    }
    for (DebugVariable &variable : variables_)
        std::stable_sort(variable.locations.begin(), variable.locations.end(),
                         [](const DebugLocation &left, const DebugLocation &right)
                         {
                             return left.address < right.address;
                         });
    return true;
}

bool DebugMetadata::path_suffix_equal(const std::string &requested,
                                      const std::string &recorded)
{
    const std::string *longer = &requested;
    const std::string *shorter = &recorded;

    if (shorter->size() > longer->size())
        std::swap(longer, shorter);
    size_t offset = longer->size() - shorter->size();
    if (offset != 0 && (*longer)[offset - 1] != '/' && (*longer)[offset - 1] != '\\')
        return false;
    for (size_t index = 0; index < shorter->size(); ++index)
        if (normalized_path_character((*longer)[offset + index]) !=
            normalized_path_character((*shorter)[index]))
            return false;
    return true;
}

const DebugLine *DebugMetadata::find_source_line(const std::string &file, int line) const
{
    const DebugLine *best = nullptr;

    for (const DebugLine &candidate : lines_)
    {
        if (!path_suffix_equal(file, candidate.file) || candidate.line < line)
            continue;
        if (!best || candidate.line < best->line ||
            (candidate.line == best->line && candidate.address < best->address))
            best = &candidate;
    }
    return best;
}

std::vector<DebugLine> DebugMetadata::source_lines(const std::string &file) const
{
    std::vector<DebugLine> result;

    for (const DebugLine &candidate : lines_)
        if (path_suffix_equal(file, candidate.file))
        {
            auto existing = std::find_if(result.begin(), result.end(),
                [&](const DebugLine &line) { return line.address == candidate.address; });
            if (existing == result.end())
                result.push_back(candidate);
            else
                *existing = candidate;
        }
    return result;
}

std::vector<std::string> DebugMetadata::source_files() const
{
    std::vector<std::string> result;
    for (const DebugLine &line : lines_)
        if (std::find(result.begin(), result.end(), line.file) == result.end())
            result.push_back(line.file);
    return result;
}

const DebugFunction *DebugMetadata::find_function(uint16_t address) const
{
    const DebugFunction *best = nullptr;

    for (const DebugFunction &function : functions_)
    {
        if (address < function.start || address >= function.end)
            continue;
        if (!best || function.start > best->start)
            best = &function;
    }
    return best;
}

const DebugFunction *DebugMetadata::find_function(const std::string &name) const
{
    for (const DebugFunction &function : functions_)
        if (function.assembly_name == name || function.source_name == name)
            return &function;
    return nullptr;
}

const DebugLine *DebugMetadata::find_address_line(uint16_t address) const
{
    const DebugFunction *function = find_function(address);
    const DebugLine *best = nullptr;
    bool shared_boundary = false;

    if (!functions_.empty() && !function)
        return nullptr;
    if (function)
        for (const DebugFunction &candidate : functions_)
            if (&candidate != function && candidate.end == function->start)
            {
                shared_boundary = true;
                break;
            }

    for (const DebugLine &candidate : lines_)
    {
        if (candidate.address > address ||
            (function && candidate.address < function->start) ||
            (shared_boundary && candidate.address == function->start))
            continue;
        if (!best || candidate.address >= best->address)
            best = &candidate;
    }
    return best;
}

std::filesystem::path DebugMetadata::source_full_path(const std::string &recorded) const
{
    std::filesystem::path source(recorded);
    if (source.is_relative())
        source = program_.parent_path() / source;
    return std::filesystem::absolute(source).lexically_normal();
}

const DebugVariable *DebugMetadata::find_variable(const std::string &name, uint16_t pc) const
{
    const DebugFunction *function = find_function(pc);
    const DebugVariable *best = nullptr;
    if (function)
        for (const DebugVariable &variable : variables_)
        {
            if (variable.global || variable.function != function->assembly_name ||
                variable.name != name || variable.declaration > pc || pc >= variable.end)
                continue;
            if (!best || variable.declaration >= best->declaration)
                best = &variable;
        }
    if (best)
        return best;
    for (const DebugVariable &variable : variables_)
        if (variable.global && variable.name == name)
            return &variable;
    return nullptr;
}

const DebugLocation *DebugMetadata::find_location(const DebugVariable &variable,
                                                  uint16_t pc) const
{
    const DebugLocation *best = nullptr;
    for (const DebugLocation &location : variable.locations)
        if (location.address <= pc && (!best || location.address >= best->address))
            best = &location;
    return best;
}

const DebugStruct *DebugMetadata::find_struct(int id) const
{
    for (const DebugStruct &debug_struct : structs_)
        if (debug_struct.id == id)
            return &debug_struct;
    return nullptr;
}

const DebugField *DebugMetadata::find_field(int struct_id, const std::string &name) const
{
    for (const DebugField &field : fields_)
        if (field.struct_id == struct_id && field.name == name)
            return &field;
    return nullptr;
}

std::vector<const DebugVariable *> DebugMetadata::scoped_variables(uint16_t pc) const
{
    std::vector<const DebugVariable *> result;
    const DebugFunction *function = find_function(pc);
    if (!function)
        return result;
    for (const DebugVariable &variable : variables_)
        if (!variable.global && variable.function == function->assembly_name &&
            variable.declaration <= pc && pc < variable.end &&
            find_variable(variable.name, pc) == &variable)
            result.push_back(&variable);
    return result;
}
