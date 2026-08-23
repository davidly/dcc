#pragma once

#include "dcc_host_debug_metadata.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct DebugRegisterValues
{
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t iy = 0;
};

struct DebugValue
{
    uint16_t address = 0;
    int type = 0;
    int size = 0;
    bool immediate = false;
    uint32_t immediate_value = 0;
    bool optimized_out = false;
    DebugLocationKind location_kind = DebugLocationKind::OptimizedOut;
    bool is_array = false;
    bool is_function_pointer = false;
    int element_size = 0;
    std::vector<int> dimensions;
    int bit_width = 0;
    int bit_shift = 0;
};

class DebugEvaluator
{
public:
    using RegisterWriter = std::function<bool(DebugLocationKind, uint32_t)>;

    DebugEvaluator(const DebugMetadata &metadata, uint16_t pc, uint16_t ix,
                   DebugRegisterValues registers = {},
                   bool registers_available = false,
                   RegisterWriter register_writer = {});

    bool resolve(const std::string &expression, DebugValue &value) const;
    bool evaluate(const std::string &expression, DebugValue &value) const;
    bool evaluate_integer(const std::string &expression, uint32_t &result,
                          int *size = nullptr, bool *is_unsigned = nullptr) const;
    bool write(const DebugValue &value, uint32_t new_value) const;

    std::string format(const DebugValue &value) const;
    std::string type_name(const DebugValue &value) const;
    std::string variable_type_name(const DebugVariable &variable) const;
    DebugValue value_from_variable(const DebugVariable &variable) const;
    int child_count(const DebugValue &value) const;
    bool child_expression(const DebugValue &parent, const std::string &parent_expression,
                          int child_index, std::string &expression,
                          std::string &display_name) const;
    bool writable(const DebugValue &value) const;

private:
    friend class DebugExpressionParser;

    uint16_t read_word(uint16_t address) const;
    bool range_valid(uint16_t address, int size) const;
    int type_size(int type) const;
    int decay_pointer_type(int type) const;
    uint32_t integer(const DebugValue &value) const;
    std::string type_name(int type, bool is_array, const std::vector<int> &dimensions,
                          bool is_function_pointer) const;

    const DebugMetadata &metadata_;
    uint16_t pc_;
    uint16_t ix_;
    DebugRegisterValues registers_;
    bool registers_available_;
    RegisterWriter register_writer_;
};