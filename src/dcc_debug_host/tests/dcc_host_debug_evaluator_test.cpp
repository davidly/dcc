#include "dcc_host_debug_evaluator.hpp"

extern "C"
{
#include "memory.h"
}

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
void write_word(uint16_t address, uint16_t value)
{
    memory[address] = static_cast<uint8_t>(value);
    memory[static_cast<uint16_t>(address + 1)] = static_cast<uint8_t>(value >> 8);
}

void write_metadata(const std::filesystem::path &path)
{
    std::ofstream output(path);
    output << "DCCDBG 2\n"
              "function-begin 0100 \"_main\" \"main\"\n"
              "line 0150 1 \"fixture.c\"\n"
              "variable 0100 \"_main\" \"local\" 2 2 -2 2 0 0 2 0 \"\"\n"
              "variable 0100 \"_main\" \"argument\" 2 3 4 2 0 0 2 0 \"\"\n"
              "variable 0100 \"_main\" \"array\" 2 2 -10 6 1 0 2 0 \"3\"\n"
              "variable 0100 \"_main\" \"matrix\" 2 2 -38 12 1 0 6 0 \"2,3\"\n"
              "variable 0100 \"_main\" \"pointer\" 18 2 -12 2 0 0 2 0 \"\"\n"
              "variable 0100 \"_main\" \"values\" 2 2 -20 6 1 1 2 0 \"0\"\n"
              "variable 0100 \"_main\" \"callback\" 18 2 -22 2 0 0 2 1 \"\"\n"
              "variable 0100 \"_main\" \"text\" 17 2 -24 2 0 0 1 0 \"\"\n"
              "variable 0100 \"_main\" \"bitptr\" 1936 2 -26 2 0 0 2 0 \"\"\n"
              "variable 0100 \"_main\" \"badlocal\" 2 2 -20000 2 0 0 2 0 \"\"\n"
              "global 4000 \"_bits\" \"bits\" 1920 2 0 0 2 0 \"\"\n"
              "global 4100 \"_numbers\" \"numbers\" 2 6 1 0 2 0 \"3\"\n"
              "struct 7 2 0 \"Bits\"\n"
              "field 7 \"low\" 34 0 2 0 2 3 0 \"\"\n"
              "field 7 \"high\" 2 0 2 0 2 4 3 \"\"\n"
              "function-end 0200 \"_main\" \"main\"\n";
}
}

int main()
{
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dcc-debug-evaluator-test";
    fs::create_directories(root);
    fs::path program = root / "fixture.COM";
    std::ofstream(program, std::ios::binary).put('\0');
    write_metadata(root / "fixture.DBG");

    DebugMetadata metadata;
    std::string error;
    assert(metadata.load_for_program(program, error));
    std::memset(memory, 0, 64 * 1024);
    constexpr uint16_t frame = 0x3000;
    write_word(frame - 2, 7);
    write_word(frame + 4, 5);
    write_word(frame - 10, 1);
    write_word(frame - 8, 2);
    write_word(frame - 6, 3);
    write_word(frame - 32, 40);
    write_word(frame - 28, 42);
    write_word(frame - 12, frame - 8);
    write_word(frame - 20, 0x4200);
    write_word(0x4200, 11);
    write_word(0x4202, 22);
    write_word(0x4204, 33);
    write_word(frame - 22, 0x1234);
    write_word(frame - 24, 0x4300);
    write_word(frame - 26, 0x4000);
    std::memcpy(memory + 0x4300, "hello", 6);
    write_word(0x4000, static_cast<uint16_t>(5 | (14 << 3)));
    write_word(0x4100, 10);
    write_word(0x4102, 20);
    write_word(0x4104, 30);

    DebugEvaluator evaluator(metadata, 0x0150, frame);
    DebugValue value;
    uint32_t integer;
    assert(evaluator.evaluate("local", value) && evaluator.format(value) == "7");
    assert(evaluator.evaluate_integer("local + argument * 2", integer) && integer == 17);
    assert(evaluator.evaluate("array[2]", value) && evaluator.format(value) == "3");
    assert(evaluator.evaluate("matrix[1][0]", value) && evaluator.format(value) == "40");
    assert(evaluator.evaluate("matrix[1][2]", value) && evaluator.format(value) == "42");
    assert(evaluator.evaluate("*pointer", value) && evaluator.format(value) == "2");
    assert(evaluator.evaluate("values[2]", value) && evaluator.format(value) == "33");
    assert(evaluator.evaluate("numbers[1]", value) && evaluator.format(value) == "20");
    assert(evaluator.evaluate("bits.low", value) && evaluator.format(value) == "5");
    assert(evaluator.evaluate("bits.high", value) && evaluator.format(value) == "-2");
    assert(evaluator.evaluate("bitptr", value) && evaluator.format(value) == "0x4000");
    assert(evaluator.type_name(value) == "struct Bits *");
    assert(evaluator.evaluate("bitptr->low", value) && evaluator.format(value) == "5");
    assert(!evaluator.evaluate("bitptr.low", value));
    assert(!evaluator.evaluate("badlocal", value));
    assert(evaluator.evaluate("text", value) && evaluator.format(value) == "0x4300 'hello'");
    assert(evaluator.evaluate("callback", value) && evaluator.type_name(value) == "int (*)()");
    assert(evaluator.evaluate("&array[1]", value) && value.immediate_value == frame - 8);
    assert(evaluator.evaluate_integer("sizeof(array) == 6", integer) && integer == 1);
    assert(evaluator.evaluate_integer("0xffff == -1", integer) && integer == 1);
    assert(evaluator.evaluate_integer("65535 > -1", integer) && integer == 1);
    assert(evaluator.evaluate_integer("(local << 1) == 14 && argument == 5", integer) && integer == 1);
    assert(evaluator.evaluate_integer("0 && missing_name", integer) == false);
    assert(evaluator.evaluate_integer("0 && argument == 5", integer) && integer == 0);
    assert(evaluator.evaluate_integer("1 || argument == 6", integer) && integer == 1);
    assert(evaluator.evaluate_integer("local == 7 ? 42 : 0", integer) && integer == 42);
    assert(evaluator.evaluate_integer("'\\x41' == 65", integer) && integer == 1);
    assert(evaluator.evaluate_integer("'\\101' == 'A'", integer) && integer == 1);
    assert(evaluator.evaluate_integer("'\\b' == 8", integer) && integer == 1);
    assert(evaluator.evaluate_integer("1 + 2 << 3", integer) && integer == 24);
    assert(evaluator.evaluate_integer("1, 2, 3", integer) && integer == 3);
    assert(evaluator.evaluate_integer("0 ? 2 : 3", integer) && integer == 3);
    assert(evaluator.evaluate_integer("(unsigned char)257", integer) && integer == 1);
    assert(evaluator.evaluate_integer("(_Bool)42", integer) && integer == 1);
    assert(evaluator.evaluate_integer("-7 / 3", integer) && static_cast<int16_t>(integer) == -2);
    assert(evaluator.evaluate_integer("-7 % 3", integer) && static_cast<int16_t>(integer) == -1);
    assert(!evaluator.evaluate_integer("1 / 0", integer));
    assert(evaluator.evaluate_integer("0 && (1 / 0)", integer) && integer == 0);
    assert(evaluator.evaluate_integer("1 || (1 / 0)", integer) && integer == 1);
    assert(evaluator.evaluate_integer("1 ? 7 : (1 / 0)", integer) && integer == 7);
    assert(evaluator.evaluate_integer("0 ? (1 / 0) : 8", integer) && integer == 8);
    assert(!evaluator.evaluate_integer("local +", integer));
    assert(!evaluator.evaluate("array[3]", value));
    assert(!evaluator.evaluate("matrix[2][0]", value));
    assert(!evaluator.evaluate("bits.missing", value));
    assert(!evaluator.evaluate("pointer->low", value));

    assert(evaluator.evaluate("local", value) && evaluator.writable(value));
    assert(evaluator.write(value, 8) && evaluator.format(value) == "8");
    assert(evaluator.evaluate("bits.low", value) && evaluator.write(value, 3));
    assert(evaluator.evaluate("bits.low", value) && evaluator.format(value) == "3");
    assert(evaluator.evaluate("array", value) && evaluator.child_count(value) == 3);
    std::string child_expression, display_name;
    assert(evaluator.child_expression(value, "array", 1, child_expression, display_name));
    assert(child_expression == "array[1]" && display_name == "[1]");
    assert(evaluator.evaluate("bits", value) && evaluator.child_count(value) == 2);
    assert(evaluator.evaluate("pointer", value) && evaluator.child_count(value) == 1);

    write_word(frame - 12, 0xfffe);
    assert(!evaluator.evaluate("pointer[1]", value));
    write_word(frame - 12, 0xffff);
    assert(!evaluator.evaluate("*pointer", value));

    fs::remove_all(root);
    return 0;
}