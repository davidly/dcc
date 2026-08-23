#include "dcc_host_debug_metadata.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main()
{
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dcc-debug-metadata-test";
    fs::create_directories(root);
    fs::path program = root / "fixture.COM";
    fs::path metadata = root / "fixture.DBG";
    std::ofstream(program, std::ios::binary).put('\0');
    std::ofstream output(metadata);
    output << "DCCDBG 2\n"
              "function-begin 0100 \"_main\" \"main\"\n"
              "line 0100 1 \"fixture.c\"\n"
              "line 0100 2 \"fixture.c\"\n"
              "line 0100 3 \"fixture.c\"\n"
              "line 0110 4 \"C:\\\\source\\\\win.c\"\n"
              "variable 0110 \"_main\" \"value\" 2 2 -2 2 0 0 2 0 \"\"\n"
              "variable 0120 \"_main\" \"value\" 2 2 -4 2 0 0 2 0 \"\"\n"
              "variable-end 0130 \"_main\" \"value\" -4\n"
              "variable 0110 \"_main\" \"items\" 2 2 -10 6 1 1 2 0 \"3\"\n"
              "global 4000 \"_global\" \"global\" 34 2 0 0 2 0 \"\"\n"
              "struct 7 2 0 \"Bits\"\n"
              "field 7 \"low\" 34 0 2 0 2 3 0 \"\"\n"
              "function-end 0140 \"_main\" \"main\"\n";
    output.close();

    DebugMetadata debug;
    std::string error;
    assert(debug.load_for_program(program, error));
    std::vector<DebugLine> lines = debug.source_lines("fixture.c");
    assert(lines.size() == 1 && lines[0].address == 0x0100 && lines[0].line == 3);
    assert(debug.find_address_line(0x0100)->line == 3);
    const DebugLine *windows_line = debug.find_source_line("C:/source/win.c", 4);
    assert(windows_line && windows_line->file == "C:\\source\\win.c");
    assert(debug.find_variable("value", 0x0125)->offset == -4);
    assert(debug.find_variable("value", 0x0135)->offset == -2);
    const DebugVariable *items = debug.find_variable("items", 0x0120);
    assert(items && items->is_array && items->is_vla && items->dimensions.size() == 1);
    assert(items->dimensions[0] == 3);
    assert(debug.find_variable("global", 0x0120)->address == 0x4000);
    assert(debug.find_struct(7)->name == "Bits");
    const DebugField *field = debug.find_field(7, "low");
    assert(field && field->bit_width == 3 && field->type == 34);
    assert(debug.scoped_variables(0x0125).size() == 2);

    fs::path broken_program = root / "broken.COM";
    fs::path broken_metadata = root / "broken.DBG";
    std::ofstream(broken_program, std::ios::binary).put('\0');
    std::ofstream broken(broken_metadata);
    broken << "DCCDBG 2\n"
              "function-begin 0100 \"_main\" \"main\"\n"
              "line 0100 1 \"broken.c\"\n"
              "variable 0100 \"_main\" \"bad\" 2 2 -2 2 1 0 2 0 \"3,no\"\n"
              "function-end 0110 \"_main\" \"main\"\n";
    broken.close();
    error.clear();
    assert(!debug.load_for_program(broken_program, error));
    assert(error.find("invalid variable dimensions") != std::string::npos);

    broken.open(broken_metadata, std::ios::trunc);
    broken << "DCCDBG 2\n"
              "function-begin 0100 \"_main\" \"main\"\n"
              "line 0100 1 \"broken.c\"\n"
              "variable-end 0105 \"_main\" \"missing\" -2\n"
              "function-end 0110 \"_main\" \"main\"\n";
    broken.close();
    error.clear();
    assert(!debug.load_for_program(broken_program, error));
    assert(error.find("unmatched variable-end") != std::string::npos);

    broken.open(broken_metadata, std::ios::trunc);
    broken << "DCCDBG 2\n"
              "function-begin 0100 \"_main\" \"main\"\n"
              "line 0100 1 \"broken.c\"\n";
    broken.close();
    error.clear();
    assert(!debug.load_for_program(broken_program, error));
    assert(error.find("unterminated function") != std::string::npos);

    broken.open(broken_metadata, std::ios::trunc);
    broken << "DCCDBG 2\n"
              "function-begin 0100 \"_main\" \"main\"\n"
              "line 0100 1 \"broken.c\"\n"
              "variable 0100 \"_main\" \"bad\" 2 2 -2 2 1 0 2 0 \"3,\"\n"
              "function-end 0110 \"_main\" \"main\"\n";
    broken.close();
    error.clear();
    assert(!debug.load_for_program(broken_program, error));
    assert(error.find("invalid variable dimensions") != std::string::npos);

    fs::remove_all(root);
    return 0;
}