#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct DebugLine
{
    uint16_t address;
    int line;
    std::string file;
};

struct DebugFunction
{
    uint16_t start;
    uint16_t end;
    std::string assembly_name;
    std::string source_name;
};

enum class DebugLocationKind
{
    OptimizedOut = 0,
    Frame = 1,
    HL = 2,
    DE = 3,
    BC = 4,
    IY = 5,
    HL_DE = 6,
    BC_IY = 7,
    Constant = 8
};

struct DebugLocation
{
    uint16_t address = 0;
    DebugLocationKind kind = DebugLocationKind::OptimizedOut;
    int64_t detail = 0;
};

struct DebugVariable
{
    uint16_t declaration = 0;
    uint16_t end = 0xffff;
    uint16_t address = 0;
    bool global = false;
    std::string function;
    std::string name;
    int type = 0;
    int storage = 0;
    int offset = 0;
    int size = 0;
    bool is_array = false;
    bool is_vla = false;
    bool is_function_pointer = false;
    int element_size = 0;
    std::vector<int> dimensions;
    std::vector<DebugLocation> locations;
};

struct DebugStruct
{
    int id = 0;
    int size = 0;
    bool is_union = false;
    std::string name;
};

struct DebugField
{
    int struct_id = 0;
    std::string name;
    int type = 0;
    int offset = 0;
    int size = 0;
    bool is_array = false;
    int element_size = 0;
    int bit_width = 0;
    int bit_shift = 0;
    std::vector<int> dimensions;
};

class DebugMetadata
{
public:
    bool load_for_program(const std::filesystem::path &program, std::string &error);

    const DebugLine *find_source_line(const std::string &file, int line) const;
    const DebugLine *find_address_line(uint16_t address) const;
    const DebugFunction *find_function(uint16_t address) const;
    const DebugFunction *find_function(const std::string &name) const;
    const DebugVariable *find_variable(const std::string &name, uint16_t pc) const;
    const DebugLocation *find_location(const DebugVariable &variable, uint16_t pc) const;
    const DebugStruct *find_struct(int id) const;
    const DebugField *find_field(int struct_id, const std::string &name) const;
    std::vector<const DebugVariable *> scoped_variables(uint16_t pc) const;
    std::vector<DebugLine> source_lines(const std::string &file) const;
    std::vector<std::string> source_files() const;
    std::filesystem::path source_full_path(const std::string &recorded) const;

    bool empty() const { return lines_.empty() && functions_.empty(); }
    const std::vector<DebugField> &fields() const { return fields_; }

private:
    static bool path_suffix_equal(const std::string &requested, const std::string &recorded);

    std::filesystem::path program_;
    std::vector<DebugLine> lines_;
    std::vector<DebugFunction> functions_;
    std::vector<DebugVariable> variables_;
    std::vector<DebugStruct> structs_;
    std::vector<DebugField> fields_;
};
