#include "dcc_host_debug_evaluator.hpp"

extern "C"
{
#include "memory.h"
extern uint8_t memory[64 * 1024];
}

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
uint32_t value_mask(int size)
{
    return size == 1 ? 0xffU : (size == 2 ? 0xffffU : 0xffffffffU);
}

uint32_t normalize(uint32_t value, int size, bool is_unsigned)
{
    value &= value_mask(size);
    if (is_unsigned)
        return value;
    if (size == 1)
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value)));
    if (size == 2)
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
    return value;
}

int64_t signed_value(uint32_t value, int size)
{
    if (size == 1)
        return static_cast<int8_t>(value);
    if (size == 2)
        return static_cast<int16_t>(value);
    return static_cast<int32_t>(value);
}

void promote(int &size, bool &is_unsigned)
{
    if (size < 2)
    {
        size = 2;
        is_unsigned = false;
    }
}

void common_type(int left_size, bool left_unsigned, int right_size, bool right_unsigned,
                 int &size, bool &is_unsigned)
{
    promote(left_size, left_unsigned);
    promote(right_size, right_unsigned);
    if (left_size == right_size)
    {
        size = left_size;
        is_unsigned = left_unsigned || right_unsigned;
    }
    else if (left_size > right_size)
    {
        size = left_size;
        is_unsigned = left_unsigned;
    }
    else
    {
        size = right_size;
        is_unsigned = right_unsigned;
    }
}

std::string trim(const std::string &text)
{
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}
}

class DebugExpressionParser
{
public:
    DebugExpressionParser(const DebugEvaluator &evaluator, const std::string &expression)
        : evaluator_(evaluator), storage_(expression), text_(storage_.c_str()) {}

    bool parse(uint32_t &result, int &size, bool &is_unsigned)
    {
        result = parse_comma();
        space();
        size = size_;
        is_unsigned = unsigned_;
        return ok_ && *text_ == 0;
    }

private:
    void space()
    {
        while (std::isspace(static_cast<unsigned char>(*text_)))
            ++text_;
    }

    void set_int()
    {
        size_ = 2;
        unsigned_ = false;
    }

    uint32_t parse_primary()
    {
        uint32_t result = 0;
        space();
        if (*text_ == '(')
        {
            ++text_;
            result = parse_comma();
            space();
            if (*text_ != ')')
                ok_ = false;
            else
                ++text_;
            return result;
        }
        if (std::isdigit(static_cast<unsigned char>(*text_)))
        {
            const char *literal = text_;
            char *end = nullptr;
            unsigned long parsed = std::strtoul(text_, &end, 0);
            if (end == text_ || parsed > std::numeric_limits<uint32_t>::max())
                ok_ = false;
            text_ = end;
            bool literal_unsigned = false;
            bool literal_long = false;
            while (*text_ == 'u' || *text_ == 'U' || *text_ == 'l' || *text_ == 'L')
            {
                if (*text_ == 'u' || *text_ == 'U') literal_unsigned = true;
                else literal_long = true;
                ++text_;
            }
            result = static_cast<uint32_t>(parsed);
            bool decimal = literal[0] != '0';
            if (literal_unsigned)
                size_ = literal_long || result > 0xffffU ? 4 : 2;
            else if (literal_long)
            {
                size_ = 4;
                literal_unsigned = result > 0x7fffffffU;
            }
            else if (decimal)
            {
                size_ = result <= 0x7fffU ? 2 : 4;
                literal_unsigned = result > 0x7fffffffU;
            }
            else if (result <= 0x7fffU)
                size_ = 2;
            else if (result <= 0xffffU)
            {
                size_ = 2;
                literal_unsigned = true;
            }
            else
            {
                size_ = 4;
                literal_unsigned = result > 0x7fffffffU;
            }
            unsigned_ = literal_unsigned;
            return normalize(result, size_, unsigned_);
        }
        if (*text_ == '\'')
        {
            ++text_;
            if (*text_ == '\\')
            {
                ++text_;
                if (*text_ == 'x' || *text_ == 'X')
                {
                    ++text_;
                    int digits = 0;
                    while (std::isxdigit(static_cast<unsigned char>(*text_)))
                    {
                        result = result * 16 + static_cast<uint32_t>(
                            std::isdigit(static_cast<unsigned char>(*text_)) ? *text_ - '0' :
                            std::tolower(static_cast<unsigned char>(*text_)) - 'a' + 10);
                        ++text_;
                        ++digits;
                    }
                    if (!digits) ok_ = false;
                }
                else if (*text_ >= '0' && *text_ <= '7')
                {
                    int digits = 0;
                    while (digits < 3 && *text_ >= '0' && *text_ <= '7')
                    {
                        result = result * 8 + static_cast<uint32_t>(*text_ - '0');
                        ++text_;
                        ++digits;
                    }
                }
                else
                {
                    if (*text_ == 'a') result = '\a';
                    else if (*text_ == 'b') result = '\b';
                    else if (*text_ == 'f') result = '\f';
                    else if (*text_ == 'n') result = '\n';
                    else if (*text_ == 'r') result = '\r';
                    else if (*text_ == 't') result = '\t';
                    else if (*text_ == 'v') result = '\v';
                    else result = static_cast<unsigned char>(*text_);
                    if (*text_) ++text_;
                    else ok_ = false;
                }
            }
            else if (*text_)
            {
                result = static_cast<unsigned char>(*text_);
                ++text_;
            }
            else
                ok_ = false;
            if (*text_ != '\'')
                ok_ = false;
            else
                ++text_;
            set_int();
            return result;
        }
        if (std::isalpha(static_cast<unsigned char>(*text_)) || *text_ == '_' || *text_ == '*')
        {
            const char *start = text_;
            const char *cursor = text_;
            int bracket_depth = 0;
            while (*cursor == '*') ++cursor;
            while (*cursor)
            {
                if (*cursor == '[') ++bracket_depth;
                else if (*cursor == ']') --bracket_depth;
                else if (bracket_depth == 0 &&
                         (std::isspace(static_cast<unsigned char>(*cursor)) ||
                          std::strchr("+*/%<>=!&|^?:,()", *cursor) ||
                          (*cursor == '-' && cursor[1] != '>')))
                    break;
                ++cursor;
            }
            DebugValue value;
            if (cursor == start || !evaluator_.resolve(std::string(start, cursor), value) ||
                value.optimized_out ||
                value.is_array || ((value.type & 128) && !(value.type & (16 | 64))) ||
                (value.type & 15) == 5)
            {
                ok_ = false;
                return 0;
            }
            text_ = cursor;
            size_ = value.type & (16 | 64) ? 2 : value.size;
            unsigned_ = (value.type & (16 | 64 | 32)) != 0;
            return evaluator_.integer(value);
        }
        ok_ = false;
        return 0;
    }

    uint32_t parse_unary()
    {
        struct Cast { const char *text; int size; bool is_unsigned; bool boolean; };
        static const Cast casts[] = {
            {"(unsigned char)", 1, true, false}, {"(char)", 1, false, false},
            {"(unsigned int)", 2, true, false}, {"(unsigned)", 2, true, false},
            {"(int)", 2, false, false}, {"(short)", 2, false, false},
            {"(unsigned long)", 4, true, false}, {"(long)", 4, false, false},
            {"(_Bool)", 1, true, true}};
        space();
        if (std::strncmp(text_, "sizeof", 6) == 0 &&
            !std::isalnum(static_cast<unsigned char>(text_[6])) && text_[6] != '_')
        {
            text_ += 6;
            space();
            if (*text_ != '(') { ok_ = false; return 0; }
            const char *start = ++text_;
            const char *cursor = start;
            int depth = 0;
            while (*cursor)
            {
                if (*cursor == '(') ++depth;
                else if (*cursor == ')')
                {
                    if (depth == 0) break;
                    --depth;
                }
                ++cursor;
            }
            if (*cursor != ')' || cursor == start) { ok_ = false; return 0; }
            std::string operand(start, cursor);
            text_ = cursor + 1;
            size_ = 2;
            unsigned_ = true;
            DebugValue value;
            if (evaluator_.resolve(operand, value)) return static_cast<uint32_t>(value.size);
            if (operand.find('*') != std::string::npos) return 2;
            if (operand.find("char") != std::string::npos || operand.find("_Bool") != std::string::npos) return 1;
            if (operand.find("long") != std::string::npos || operand.find("float") != std::string::npos) return 4;
            if (operand.find("int") != std::string::npos || operand.find("short") != std::string::npos ||
                operand == "unsigned" || operand == "signed") return 2;
            ok_ = false;
            return 0;
        }
        for (const Cast &cast : casts)
        {
            size_t length = std::strlen(cast.text);
            if (std::strncmp(text_, cast.text, length) != 0) continue;
            text_ += length;
            uint32_t result = parse_unary();
            size_ = cast.size;
            unsigned_ = cast.is_unsigned;
            return cast.boolean ? result != 0 : normalize(result, size_, unsigned_);
        }
        if (*text_ == '+')
        {
            ++text_;
            uint32_t result = parse_unary();
            promote(size_, unsigned_);
            return normalize(result, size_, unsigned_);
        }
        if (*text_ == '-' && text_[1] != '>')
        {
            ++text_;
            uint32_t result = parse_unary();
            promote(size_, unsigned_);
            return normalize(0U - result, size_, unsigned_);
        }
        if (*text_ == '!')
        {
            ++text_;
            uint32_t result = !parse_unary();
            set_int();
            return result;
        }
        if (*text_ == '~')
        {
            ++text_;
            uint32_t result = parse_unary();
            promote(size_, unsigned_);
            return normalize(~result, size_, unsigned_);
        }
        return parse_primary();
    }

    uint32_t parse_multiplicative()
    {
        uint32_t left = parse_unary();
        int left_size = size_; bool left_unsigned = unsigned_;
        for (;;)
        {
            space();
            char operation = *text_;
            if (operation != '*' && operation != '/' && operation != '%') break;
            ++text_;
            uint32_t right = parse_unary();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned);
            right = normalize(right, common_size, common_unsigned);
            if ((operation == '/' || operation == '%') && right == 0)
            {
                if (!suppress_runtime_errors_) ok_ = false;
                return 0;
            }
            if (operation == '*') left *= right;
            else if (common_unsigned) left = operation == '/' ? left / right : left % right;
            else if (operation == '/') left = static_cast<uint32_t>(signed_value(left, common_size) / signed_value(right, common_size));
            else left = static_cast<uint32_t>(signed_value(left, common_size) % signed_value(right, common_size));
            left = normalize(left, common_size, common_unsigned);
            left_size = common_size; left_unsigned = common_unsigned;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_additive()
    {
        uint32_t left = parse_multiplicative();
        int left_size = size_; bool left_unsigned = unsigned_;
        for (;;)
        {
            space();
            char operation = *text_;
            if (operation != '+' && (operation != '-' || text_[1] == '>')) break;
            ++text_;
            uint32_t right = parse_multiplicative();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned);
            right = normalize(right, common_size, common_unsigned);
            left = normalize(operation == '+' ? left + right : left - right, common_size, common_unsigned);
            left_size = common_size; left_unsigned = common_unsigned;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_shift()
    {
        uint32_t left = parse_additive();
        int left_size = size_; bool left_unsigned = unsigned_;
        for (;;)
        {
            space();
            if (std::strncmp(text_, "<<", 2) && std::strncmp(text_, ">>", 2)) break;
            bool left_shift = text_[0] == '<';
            promote(left_size, left_unsigned);
            left = normalize(left, left_size, left_unsigned);
            text_ += 2;
            uint32_t right = parse_additive();
            if (right >= static_cast<uint32_t>(left_size * 8))
            {
                if (!suppress_runtime_errors_) ok_ = false;
                return 0;
            }
            if (left_shift) left <<= right;
            else if (left_unsigned) left >>= right;
            else left = static_cast<uint32_t>(signed_value(left, left_size) >> right);
            left = normalize(left, left_size, left_unsigned);
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_relational()
    {
        uint32_t left = parse_shift();
        int left_size = size_; bool left_unsigned = unsigned_;
        for (;;)
        {
            space();
            int operation = 0;
            if (!std::strncmp(text_, "<=", 2)) operation = 1;
            else if (!std::strncmp(text_, ">=", 2)) operation = 2;
            else if (*text_ == '<' && text_[1] != '<') operation = 3;
            else if (*text_ == '>' && text_[1] != '>') operation = 4;
            else break;
            text_ += operation <= 2 ? 2 : 1;
            uint32_t right = parse_shift();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned);
            right = normalize(right, common_size, common_unsigned);
            if (common_unsigned)
            {
                if (operation == 1) left = left <= right;
                else if (operation == 2) left = left >= right;
                else if (operation == 3) left = left < right;
                else left = left > right;
            }
            else
            {
                int64_t lhs = signed_value(left, common_size), rhs = signed_value(right, common_size);
                if (operation == 1) left = lhs <= rhs;
                else if (operation == 2) left = lhs >= rhs;
                else if (operation == 3) left = lhs < rhs;
                else left = lhs > rhs;
            }
            left_size = 2; left_unsigned = false;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_equality()
    {
        uint32_t left = parse_relational();
        int left_size = size_; bool left_unsigned = unsigned_;
        for (;;)
        {
            space();
            if (std::strncmp(text_, "==", 2) && std::strncmp(text_, "!=", 2)) break;
            bool equal = text_[0] == '=';
            text_ += 2;
            uint32_t right = parse_relational();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned);
            right = normalize(right, common_size, common_unsigned);
            left = equal ? left == right : left != right;
            left_size = 2; left_unsigned = false;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_bit_and()
    {
        uint32_t left = parse_equality();
        int left_size = size_; bool left_unsigned = unsigned_;
        while ((space(), *text_ == '&' && text_[1] != '&'))
        {
            ++text_;
            uint32_t right = parse_equality();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned) & normalize(right, common_size, common_unsigned);
            left_size = common_size; left_unsigned = common_unsigned;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_bit_xor()
    {
        uint32_t left = parse_bit_and();
        int left_size = size_; bool left_unsigned = unsigned_;
        while ((space(), *text_ == '^'))
        {
            ++text_;
            uint32_t right = parse_bit_and();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned) ^ normalize(right, common_size, common_unsigned);
            left_size = common_size; left_unsigned = common_unsigned;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_bit_or()
    {
        uint32_t left = parse_bit_xor();
        int left_size = size_; bool left_unsigned = unsigned_;
        while ((space(), *text_ == '|' && text_[1] != '|'))
        {
            ++text_;
            uint32_t right = parse_bit_xor();
            int common_size; bool common_unsigned;
            common_type(left_size, left_unsigned, size_, unsigned_, common_size, common_unsigned);
            left = normalize(left, common_size, common_unsigned) | normalize(right, common_size, common_unsigned);
            left_size = common_size; left_unsigned = common_unsigned;
        }
        size_ = left_size; unsigned_ = left_unsigned;
        return left;
    }

    uint32_t parse_logical_and()
    {
        uint32_t left = parse_bit_or();
        while ((space(), !std::strncmp(text_, "&&", 2)))
        {
            text_ += 2;
            bool saved_suppression = suppress_runtime_errors_;
            suppress_runtime_errors_ = suppress_runtime_errors_ || left == 0;
            uint32_t right = parse_bit_or();
            suppress_runtime_errors_ = saved_suppression;
            left = left != 0 && right != 0;
            set_int();
        }
        return left;
    }

    uint32_t parse_logical_or()
    {
        uint32_t left = parse_logical_and();
        while ((space(), !std::strncmp(text_, "||", 2)))
        {
            text_ += 2;
            bool saved_suppression = suppress_runtime_errors_;
            suppress_runtime_errors_ = suppress_runtime_errors_ || left != 0;
            uint32_t right = parse_logical_and();
            suppress_runtime_errors_ = saved_suppression;
            left = left != 0 || right != 0;
            set_int();
        }
        return left;
    }

    uint32_t parse_conditional()
    {
        uint32_t condition = parse_logical_or();
        space();
        if (*text_ != '?') return condition;
        ++text_;
        bool saved_suppression = suppress_runtime_errors_;
        suppress_runtime_errors_ = suppress_runtime_errors_ || condition == 0;
        uint32_t when_true = parse_comma();
        suppress_runtime_errors_ = saved_suppression;
        int true_size = size_; bool true_unsigned = unsigned_;
        space();
        if (*text_ != ':')
        {
            suppress_runtime_errors_ = saved_suppression;
            ok_ = false;
            return 0;
        }
        ++text_;
        suppress_runtime_errors_ = suppress_runtime_errors_ || condition != 0;
        uint32_t when_false = parse_conditional();
        suppress_runtime_errors_ = saved_suppression;
        int common_size; bool common_unsigned;
        common_type(true_size, true_unsigned, size_, unsigned_, common_size, common_unsigned);
        size_ = common_size; unsigned_ = common_unsigned;
        return normalize(condition ? when_true : when_false, common_size, common_unsigned);
    }

    uint32_t parse_comma()
    {
        uint32_t result = parse_conditional();
        while ((space(), *text_ == ','))
        {
            ++text_;
            result = parse_conditional();
        }
        return result;
    }

    const DebugEvaluator &evaluator_;
    std::string storage_;
    const char *text_;
    bool ok_ = true;
    bool suppress_runtime_errors_ = false;
    int size_ = 2;
    bool unsigned_ = false;
};

DebugEvaluator::DebugEvaluator(const DebugMetadata &metadata, uint16_t pc, uint16_t ix,
                                                             DebugRegisterValues registers,
                                                             bool registers_available,
                                                             RegisterWriter register_writer)
        : metadata_(metadata), pc_(pc), ix_(ix), registers_(registers),
            registers_available_(registers_available),
            register_writer_(std::move(register_writer)) {}

uint16_t DebugEvaluator::read_word(uint16_t address) const
{
    return static_cast<uint16_t>(memory[address] |
           (static_cast<uint16_t>(memory[static_cast<uint16_t>(address + 1)]) << 8));
}

bool DebugEvaluator::range_valid(uint16_t address, int size) const
{
    return size >= 0 && static_cast<uint32_t>(size) <= 0x10000U - address;
}

int DebugEvaluator::type_size(int type) const
{
    if (type & (16 | 64)) return 2;
    if (type & 128)
    {
        const DebugStruct *debug_struct = metadata_.find_struct((type & ~(16 | 64)) >> 8);
        return debug_struct ? debug_struct->size : 0;
    }
    int base = type & 15;
    if (base == 1 || base == 6) return 1;
    if (base == 4 || base == 5) return 4;
    return 2;
}

int DebugEvaluator::decay_pointer_type(int type) const
{
    if (type & 64) return type & ~64;
    if (type & 16) return type & ~16;
    return type;
}

DebugValue DebugEvaluator::value_from_variable(const DebugVariable &variable) const
{
    DebugValue value;
    value.type = variable.type;
    value.size = variable.size > 0 ? variable.size : type_size(variable.type);
    value.is_array = variable.is_array;
    value.is_function_pointer = variable.is_function_pointer;
    value.element_size = variable.element_size;
    value.dimensions = variable.dimensions;
    int64_t storage_address;
    if (!variable.global && !variable.locations.empty())
    {
        const DebugLocation *location = metadata_.find_location(variable, pc_);
        if (!location || location->kind == DebugLocationKind::OptimizedOut)
        {
            value.optimized_out = true;
            return value;
        }
        if (location->kind == DebugLocationKind::Constant)
        {
            value.immediate = true;
            value.location_kind = location->kind;
            value.immediate_value = static_cast<uint32_t>(location->detail);
            return value;
        }
        if (location->kind != DebugLocationKind::Frame)
        {
            if (!registers_available_)
            {
                value.optimized_out = true;
                return value;
            }
            value.immediate = true;
            value.location_kind = location->kind;
            if (location->kind == DebugLocationKind::HL)
                value.immediate_value = registers_.hl;
            else if (location->kind == DebugLocationKind::DE)
                value.immediate_value = registers_.de;
            else if (location->kind == DebugLocationKind::BC)
                value.immediate_value = registers_.bc;
            else if (location->kind == DebugLocationKind::IY)
                value.immediate_value = registers_.iy;
            else if (location->kind == DebugLocationKind::HL_DE)
                value.immediate_value = registers_.hl |
                    (static_cast<uint32_t>(registers_.de) << 16);
            else if (location->kind == DebugLocationKind::BC_IY)
                value.immediate_value = registers_.bc |
                    (static_cast<uint32_t>(registers_.iy) << 16);
            else
                value.optimized_out = true;
            return value;
        }
        storage_address = static_cast<int64_t>(ix_) + location->detail;
    }
    else
        storage_address = variable.global ? variable.address :
                          static_cast<int64_t>(ix_) + variable.offset;
    if (storage_address < 0 || storage_address > 0xffff)
    {
        value.size = -1;
        return value;
    }
    value.address = static_cast<uint16_t>(storage_address);
    if (variable.is_vla)
    {
        if (!range_valid(value.address, 2))
        {
            value.size = -1;
            return value;
        }
        value.address = read_word(value.address);
    }
    return value;
}

bool DebugEvaluator::resolve(const std::string &raw_expression, DebugValue &value) const
{
    std::string expression = trim(raw_expression);
    if (expression.empty()) return false;
    if (expression.front() == '*')
    {
        if (!resolve(expression.substr(1), value) || !(value.type & (16 | 64))) return false;
        value.address = static_cast<uint16_t>(
            value.immediate ? value.immediate_value : read_word(value.address));
        value.immediate = false;
        value.immediate_value = 0;
        value.type = decay_pointer_type(value.type);
        value.size = type_size(value.type);
        value.is_array = false;
        value.dimensions.clear();
        return range_valid(value.address, value.size);
    }

    size_t position = 0;
    while (position < expression.size() &&
           (std::isalnum(static_cast<unsigned char>(expression[position])) || expression[position] == '_'))
        ++position;
    if (position == 0) return false;
    const DebugVariable *variable = metadata_.find_variable(expression.substr(0, position), pc_);
    if (!variable) return false;
    value = value_from_variable(*variable);
    if (value.optimized_out)
        return position == expression.size();
    if (!value.immediate && !range_valid(value.address, value.size)) return false;

    while (position < expression.size())
    {
        while (position < expression.size() &&
               std::isspace(static_cast<unsigned char>(expression[position]))) ++position;
        if (position >= expression.size()) break;
        if (expression[position] == '[')
        {
            size_t start = ++position;
            int depth = 1;
            while (position < expression.size() && depth)
            {
                if (expression[position] == '[') ++depth;
                else if (expression[position] == ']') --depth;
                if (depth) ++position;
            }
            if (depth || position == start) return false;
            uint32_t element;
            if (!evaluate_integer(expression.substr(start, position - start), element)) return false;
            ++position;
            if (value.is_array && !value.dimensions.empty())
            {
                if (value.dimensions.front() > 0 && element >= static_cast<uint32_t>(value.dimensions.front())) return false;
                int stride = value.dimensions.front() ? value.size / value.dimensions.front() : value.element_size;
                if (stride <= 0 || element > (0xffffU - value.address) / static_cast<uint32_t>(stride))
                    return false;
                value.address = static_cast<uint16_t>(value.address + element * stride);
                value.size = stride;
                value.dimensions.erase(value.dimensions.begin());
                value.is_array = !value.dimensions.empty();
            }
            else if (value.type & (16 | 64))
            {
                value.type = decay_pointer_type(value.type);
                int stride = type_size(value.type);
                uint16_t base = static_cast<uint16_t>(
                    value.immediate ? value.immediate_value : read_word(value.address));
                if (stride <= 0 || element > (0xffffU - base) / static_cast<uint32_t>(stride))
                    return false;
                value.address = static_cast<uint16_t>(base + element * stride);
                value.size = stride;
            }
            else return false;
        }
        else if (expression[position] == '.' ||
                 (expression[position] == '-' && position + 1 < expression.size() && expression[position + 1] == '>'))
        {
            bool indirect = expression[position] == '-';
            position += indirect ? 2 : 1;
            size_t start = position;
            while (position < expression.size() &&
                   (std::isalnum(static_cast<unsigned char>(expression[position])) || expression[position] == '_'))
                ++position;
            if (position == start) return false;
            if (indirect)
            {
                if (!(value.type & (16 | 64))) return false;
                value.address = read_word(value.address);
                value.type = decay_pointer_type(value.type);
            }
            if (value.type & (16 | 64)) return false;
            if (!(value.type & 128)) return false;
            int struct_id = (value.type & ~(16 | 64)) >> 8;
            const DebugField *field = metadata_.find_field(struct_id, expression.substr(start, position - start));
            if (!field || field->offset < 0 || field->offset > 0xffff - value.address)
                return false;
            value.address = static_cast<uint16_t>(value.address + field->offset);
            value.type = field->type;
            value.size = field->size;
            value.is_array = field->is_array;
            value.element_size = field->element_size;
            value.bit_width = field->bit_width;
            value.bit_shift = field->bit_shift;
            value.dimensions = field->dimensions;
        }
        else return false;
        if (!range_valid(value.address, value.size)) return false;
    }
    return true;
}

bool DebugEvaluator::evaluate_integer(const std::string &expression, uint32_t &result,
                                      int *size, bool *is_unsigned) const
{
    DebugExpressionParser parser(*this, expression);
    int result_size;
    bool result_unsigned;
    bool ok = parser.parse(result, result_size, result_unsigned);
    if (size) *size = result_size;
    if (is_unsigned) *is_unsigned = result_unsigned;
    return ok;
}

bool DebugEvaluator::evaluate(const std::string &raw_expression, DebugValue &value) const
{
    std::string expression = trim(raw_expression);
    if (resolve(expression, value)) return true;
    if (expression.size() >= 2 && expression.front() == '&')
    {
        std::string operand = trim(expression.substr(1));
        if (operand.size() >= 2 && operand.front() == '(' && operand.back() == ')')
            operand = trim(operand.substr(1, operand.size() - 2));
        if (!resolve(operand, value) || value.immediate || value.optimized_out) return false;
        value.immediate = true;
        value.immediate_value = value.address;
        value.type = value.type & 16 ? value.type | 64 : value.type | 16;
        value.size = 2;
        value.is_array = false;
        value.dimensions.clear();
        return true;
    }
    int size;
    bool is_unsigned;
    uint32_t result;
    if (!evaluate_integer(expression, result, &size, &is_unsigned)) return false;
    value = {};
    value.immediate = true;
    value.immediate_value = result;
    value.size = size;
    value.type = (size == 1 ? 1 : (size == 4 ? 4 : 2)) | (is_unsigned ? 32 : 0);
    return true;
}

uint32_t DebugEvaluator::integer(const DebugValue &value) const
{
    uint32_t result = value.immediate ? value.immediate_value : memory[value.address];
    if (!value.immediate && value.size > 1)
        result |= static_cast<uint32_t>(memory[static_cast<uint16_t>(value.address + 1)]) << 8;
    if (!value.immediate && value.size > 2)
    {
        result |= static_cast<uint32_t>(memory[static_cast<uint16_t>(value.address + 2)]) << 16;
        result |= static_cast<uint32_t>(memory[static_cast<uint16_t>(value.address + 3)]) << 24;
    }
    if (value.bit_width > 0)
    {
        if (value.bit_width > 32 || value.bit_shift < 0 ||
            value.bit_width + value.bit_shift > value.size * 8)
            return 0;
        uint32_t mask = value.bit_width == 32 ? 0xffffffffU : ((1U << value.bit_width) - 1);
        result = (result >> value.bit_shift) & mask;
        if (!(value.type & 32) && value.bit_width < 32 && (result & (1U << (value.bit_width - 1))))
            result |= ~mask;
    }
    else if (!(value.type & 32) && value.size == 1)
        result = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(result)));
    else if (!(value.type & 32) && value.size == 2)
        result = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(result)));
    return result;
}

std::string DebugEvaluator::type_name(int type, bool is_array,
                                      const std::vector<int> &dimensions,
                                      bool is_function_pointer) const
{
    int pointer_depth = (type & 16 ? 1 : 0) + (type & 64 ? 1 : 0);
    int value_type = type & ~(16 | 64);
    int base = value_type & 15;
    bool is_unsigned = (type & 32) != 0;
    std::string result;
    if (value_type & 128)
    {
        const DebugStruct *debug_struct = metadata_.find_struct(value_type >> 8);
        result = debug_struct && debug_struct->is_union ? "union " : "struct ";
        result += debug_struct && !debug_struct->name.empty() ? debug_struct->name : "<anonymous>";
    }
    else if (base == 1) result = is_unsigned ? "unsigned char" : "char";
    else if (base == 2) result = is_unsigned ? "unsigned int" : "int";
    else if (base == 4) result = is_unsigned ? "unsigned long" : "long";
    else if (base == 5) result = "float";
    else if (base == 6) result = "_Bool";
    else result = "value";
    if (is_function_pointer)
        result += " (*)()";
    else
        for (int index = 0; index < pointer_depth; ++index) result += " *";
    if (is_array)
        for (int dimension : dimensions)
            result += dimension > 0 ? "[" + std::to_string(dimension) + "]" : "[]";
    return result;
}

std::string DebugEvaluator::type_name(const DebugValue &value) const
{
    return type_name(value.type, value.is_array, value.dimensions, value.is_function_pointer);
}

std::string DebugEvaluator::variable_type_name(const DebugVariable &variable) const
{
    return type_name(variable.type, variable.is_array, variable.dimensions,
                     variable.is_function_pointer);
}

std::string DebugEvaluator::format(const DebugValue &value) const
{
    if (value.optimized_out)
        return "<optimized out>";
    if (!value.immediate && !range_valid(value.address, value.size))
        return "<invalid address>";
    auto string_preview = [](uint16_t address, int limit)
    {
        std::string result = "'";
        limit = std::min<int>(limit, static_cast<int>(0x10000U - address));
        int index = 0;
        for (; index < limit; ++index)
        {
            unsigned char character = memory[static_cast<uint16_t>(address + index)];
            if (!character) break;
            if (character == '\'' || character == '\\') { result += '\\'; result += static_cast<char>(character); }
            else if (character == '\n') result += "\\n";
            else if (character == '\r') result += "\\r";
            else if (character == '\t') result += "\\t";
            else if (std::isprint(character)) result += static_cast<char>(character);
            else
            {
                std::ostringstream escaped;
                escaped << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned>(character);
                result += escaped.str();
            }
        }
        if (index == limit) result += "...";
        return result + "'";
    };

    int base = value.type & 15;
    bool is_unsigned = (value.type & 32) != 0;
    if (value.is_array)
    {
        if (base == 1 && value.element_size == 1)
            return string_preview(value.address, std::min(value.size, 48));
        return "[" + std::to_string(value.dimensions.empty() ? 0 : value.dimensions.front()) + "]";
    }
    if ((value.type & 128) && !(value.type & (16 | 64)))
        return "{" + type_name(value) + "}";
    uint32_t raw = integer(value);
    if (value.type & (16 | 64))
    {
        std::ostringstream output;
        output << "0x" << std::hex << std::setw(4) << std::setfill('0') << (raw & 0xffffU);
        if (base == 1 && (raw & 0xffffU)) output << ' ' << string_preview(static_cast<uint16_t>(raw), 32);
        return output.str();
    }
    if (base == 5 && value.size == 4)
    {
        float number;
        std::memcpy(&number, &raw, sizeof(number));
        std::ostringstream output;
        output << number;
        return output.str();
    }
    if (is_unsigned) return std::to_string(raw);
    if (value.size == 1) return std::to_string(static_cast<int>(static_cast<int8_t>(raw)));
    if (value.size == 2) return std::to_string(static_cast<int>(static_cast<int16_t>(raw)));
    return std::to_string(static_cast<int32_t>(raw));
}

bool DebugEvaluator::writable(const DebugValue &value) const
{
    bool scalar = !value.optimized_out && !value.is_array && !(value.type & 128) &&
                  (value.type & 15) != 5 &&
                  (value.size == 1 || value.size == 2 || value.size == 4);
    if (!scalar)
        return false;
    if (value.immediate)
        return value.location_kind != DebugLocationKind::Constant &&
               value.location_kind != DebugLocationKind::OptimizedOut &&
               static_cast<bool>(register_writer_);
    return range_valid(value.address, value.size);
}

bool DebugEvaluator::write(const DebugValue &value, uint32_t new_value) const
{
    if (!writable(value)) return false;
    uint32_t stored = (value.type & 15) == 6 ? new_value != 0 : new_value;
    if (value.immediate)
    {
        if (value.size == 1)
            stored = value.type & 32 ? stored & 0xffU :
                     static_cast<uint32_t>(static_cast<int32_t>(
                         static_cast<int8_t>(stored)));
        else if (value.size == 2)
            stored &= 0xffffU;
        return register_writer_(value.location_kind, stored);
    }
    if (value.bit_width > 0)
    {
        if (value.bit_width > 32 || value.bit_shift < 0 ||
            value.bit_width + value.bit_shift > value.size * 8)
            return false;
        uint32_t bits = value.bit_width == 32 ? 0xffffffffU : ((1U << value.bit_width) - 1);
        uint32_t mask = bits << value.bit_shift;
        uint32_t old = memory[value.address];
        if (value.size > 1) old = read_word(value.address);
        if (value.size > 2) old |= static_cast<uint32_t>(read_word(static_cast<uint16_t>(value.address + 2))) << 16;
        stored = (old & ~mask) | ((new_value << value.bit_shift) & mask);
    }
    for (int index = 0; index < value.size; ++index)
        memory[static_cast<uint16_t>(value.address + index)] = static_cast<uint8_t>(stored >> (index * 8));
    return true;
}

int DebugEvaluator::child_count(const DebugValue &value) const
{
    if (value.optimized_out) return 0;
    if (value.is_array && !value.dimensions.empty()) return std::max(0, value.dimensions.front());
    if (value.is_function_pointer) return 0;
    if (value.type & (16 | 64)) return 1;
    if (value.type & 128)
    {
        int count = 0;
        int struct_id = (value.type & ~(16 | 64)) >> 8;
        for (const DebugField &field : metadata_.fields())
            if (field.struct_id == struct_id) ++count;
        return count;
    }
    return 0;
}

bool DebugEvaluator::child_expression(const DebugValue &parent,
                                      const std::string &parent_expression,
                                      int child_index, std::string &expression,
                                      std::string &display_name) const
{
    if (parent.optimized_out) return false;
    if (parent.is_array && !parent.dimensions.empty())
    {
        if (child_index < 0 || child_index >= parent.dimensions.front()) return false;
        display_name = "[" + std::to_string(child_index) + "]";
        expression = parent_expression + display_name;
        return true;
    }
    if (!parent.is_function_pointer && (parent.type & (16 | 64)))
    {
        if (child_index != 0) return false;
        display_name = "*";
        expression = "*" + parent_expression;
        return true;
    }
    if (parent.type & 128)
    {
        int found = 0;
        int struct_id = (parent.type & ~(16 | 64)) >> 8;
        for (const DebugField &field : metadata_.fields())
            if (field.struct_id == struct_id)
            {
                if (found++ != child_index) continue;
                display_name = field.name;
                expression = parent_expression + "." + field.name;
                return true;
            }
    }
    return false;
}