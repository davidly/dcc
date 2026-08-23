#include "dcc_host_mi_server.hpp"

extern "C"
{
#include "memory.h"
}

#include <array>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <process.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
volatile std::sig_atomic_t debugger_interrupt_requested = 0;

void debugger_interrupt_handler(int)
{
    debugger_interrupt_requested = 1;
}

long debugger_process_id()
{
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

bool debugger_input_ready()
{
    if (std::cin.rdbuf()->in_avail() > 0)
        return true;
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE)
        return false;
    if (GetFileType(input) == FILE_TYPE_PIPE)
    {
        DWORD available = 0;
        return PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) && available != 0;
    }
    DWORD events = 0;
    return GetNumberOfConsoleInputEvents(input, &events) && events != 0;
#else
    fd_set input;
    timeval timeout{0, 0};
    FD_ZERO(&input);
    FD_SET(STDIN_FILENO, &input);
    return select(STDIN_FILENO + 1, &input, nullptr, nullptr, &timeout) > 0;
#endif
}
}

MiServer::MiServer(FullCpmHost &host) : host_(host)
{
    host_.set_output([](uint8_t character)
    {
        std::string text(1, static_cast<char>(character));
        std::cout << "@\"" << mi_escape(text) << "\"\n" << std::flush;
    });
}

std::string MiServer::mi_escape(const std::string &text)
{
    std::string result;
    for (unsigned char character : text)
    {
        if (character == '\\' || character == '"')
        {
            result.push_back('\\');
            result.push_back(static_cast<char>(character));
        }
        else if (character == '\n' || character == '\r' || character == '\t')
        {
            result.push_back('\\');
            result.push_back(character == '\n' ? 'n' : character == '\r' ? 'r' : 't');
        }
        else if (character >= 0x20 && character < 0x7f)
        {
            result.push_back(static_cast<char>(character));
        }
    }
    return result;
}

std::string MiServer::quoted_argument(const std::string &text)
{
    size_t first = text.find('"');
    if (first == std::string::npos)
    {
        size_t space = text.find(' ');
        return space == std::string::npos ? std::string() : text.substr(space + 1);
    }
    std::string result;
    for (size_t position = first + 1; position < text.size(); ++position)
    {
        char character = text[position];
        if (character == '"')
            break;
        if (character == '\\' && position + 1 < text.size())
            character = text[++position];
        result.push_back(character);
    }
    return result;
}

std::string MiServer::last_argument(const std::string &command)
{
    size_t end = command.find_last_not_of(" \t\r\n");
    if (end == std::string::npos)
        return {};
    if (command[end] == '"')
    {
        size_t begin = command.rfind('"', end - 1);
        if (begin != std::string::npos)
            return command.substr(begin + 1, end - begin - 1);
    }
    size_t begin = command.find_last_of(" \t", end);
    size_t start = begin == std::string::npos ? 0 : begin + 1;
    return command.substr(start, end - start + 1);
}

std::string MiServer::token_from_line(const std::string &line, std::string &command)
{
    size_t position = 0;
    while (position < line.size() && std::isdigit(static_cast<unsigned char>(line[position])))
        ++position;
    command = line.substr(position);
    while (!command.empty() && (command.back() == '\r' || command.back() == '\n'))
        command.pop_back();
    return line.substr(0, position);
}

int MiServer::inline_frame(const std::string &command)
{
    size_t position = command.find("--frame");
    if (position == std::string::npos)
        return -1;
    position += 7;
    if (position < command.size() && command[position] == '=')
        ++position;
    while (position < command.size() && std::isspace(static_cast<unsigned char>(command[position])))
        ++position;
    if (position >= command.size() || !std::isdigit(static_cast<unsigned char>(command[position])))
        return -2;
    char *end = nullptr;
    long frame = std::strtol(command.c_str() + position, &end, 10);
    if (end == command.c_str() + position ||
        (*end && !std::isspace(static_cast<unsigned char>(*end))) ||
        frame > std::numeric_limits<int>::max())
        return -2;
    return static_cast<int>(frame);
}

std::vector<int> MiServer::command_numbers(const std::string &command)
{
    std::vector<int> numbers;
    std::istringstream input(command.substr(command.find(' ') + 1));
    std::string word;
    while (input >> word)
    {
        char *end = nullptr;
        long value = std::strtol(word.c_str(), &end, 10);
        if (end != word.c_str() && *end == 0)
            numbers.push_back(static_cast<int>(value));
    }
    return numbers;
}

bool MiServer::strict_command_numbers(const std::string &command,
                                      std::vector<int> &numbers)
{
    size_t space = command.find(' ');
    if (space == std::string::npos)
        return true;
    std::istringstream input(command.substr(space + 1));
    std::string word;
    while (input >> word)
    {
        char *end = nullptr;
        long value = std::strtol(word.c_str(), &end, 10);
        if (end == word.c_str() || *end ||
            value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max())
            return false;
        numbers.push_back(static_cast<int>(value));
    }
    return true;
}

std::string MiServer::option_argument(const std::string &command, const std::string &option)
{
    std::string marker = " " + option;
    size_t position = command.find(marker);
    if (position == std::string::npos)
        return {};
    position += marker.size();
    if (position < command.size() && command[position] == '=')
        ++position;
    while (position < command.size() && std::isspace(static_cast<unsigned char>(command[position])))
        ++position;
    if (position < command.size() && command[position] == '"')
        return quoted_argument(command.substr(position));
    size_t end = command.find_first_of(" \t", position);
    return command.substr(position, end == std::string::npos ? end : end - position);
}

void MiServer::result(const std::string &token, const std::string &value)
{
    std::cout << token << value << '\n' << std::flush;
}

void MiServer::emit_running(const std::string &token)
{
    result(token, "^running");
    std::cout << "*running,thread-id=\"all\"\n" << std::flush;
}

MiServer::Breakpoint *MiServer::breakpoint_at(uint16_t address)
{
    for (Breakpoint &breakpoint : breakpoints_)
        if (breakpoint.active && breakpoint.enabled && breakpoint.address == address)
            return &breakpoint;
    return nullptr;
}

std::vector<MiServer::StackFrame> MiServer::stack_frames() const
{
    z80_debug_registers_t registers{};
    std::vector<StackFrame> frames;
    z80_debug_get_registers(&registers);
    uint16_t pc = registers.pc;
    uint16_t ix = registers.ix;

    for (int depth = 0; depth < 32; ++depth)
    {
        frames.push_back({pc, ix});
        if (ix > 0xfffb)
            break;
        uint16_t caller_ix = read16(ix);
        uint16_t return_pc = read16(static_cast<uint16_t>(ix + 2));
        if (caller_ix <= ix || return_pc == 0)
            break;
        pc = static_cast<uint16_t>(return_pc - 1);
        if (!metadata_.find_function(pc))
            break;
        ix = caller_ix;
    }
    return frames;
}

bool MiServer::frame_context(int frame, StackFrame &context) const
{
    std::vector<StackFrame> frames = stack_frames();
    if (frame < 0 || static_cast<size_t>(frame) >= frames.size())
        return false;
    context = frames[static_cast<size_t>(frame)];
    return true;
}

DebugEvaluator MiServer::evaluator(int frame) const
{
    StackFrame context{};
    DebugRegisterValues values{};
    bool registers_available = false;
    if (!frame_context(frame, context))
    {
        z80_debug_registers_t registers{};
        z80_debug_get_registers(&registers);
        context = {registers.pc, registers.ix};
    }
    if (frame == 0)
    {
        z80_debug_registers_t registers{};
        z80_debug_get_registers(&registers);
        values = {registers.bc, registers.de, registers.hl, registers.iy};
        registers_available = true;
    }
    DebugEvaluator::RegisterWriter writer;
    if (frame == 0)
        writer = [](DebugLocationKind kind, uint32_t value)
        {
            return z80_debug_set_location_register(static_cast<uint8_t>(kind), value);
        };
    return DebugEvaluator(metadata_, context.pc, context.ix, values,
                          registers_available, std::move(writer));
}

MiServer::VariableObject *MiServer::find_variable_object(const std::string &command)
{
    for (VariableObject &object : variable_objects_)
    {
        if (!object.active)
            continue;
        size_t position = 0;
        while ((position = command.find(object.name, position)) != std::string::npos)
        {
            bool before = position == 0 || std::isspace(static_cast<unsigned char>(command[position - 1])) ||
                          command[position - 1] == '"';
            size_t end = position + object.name.size();
            bool after = end == command.size() || std::isspace(static_cast<unsigned char>(command[end])) ||
                         command[end] == '"';
            if (before && after)
                return &object;
            position = end;
        }
    }
    return nullptr;
}

void MiServer::emit_source(const DebugLine *line)
{
    if (!line)
        return;
    fs::path full = metadata_.source_full_path(line->file);
    std::cout << ",file=\"" << mi_escape(line->file) << "\",fullname=\""
              << mi_escape(full.string()) << "\",line=\"" << line->line << "\"";
}

const DebugLine *MiServer::stopped_source_line(uint16_t address, DebugLine &storage) const
{
    if (stopped_breakpoint_ && stopped_breakpoint_->address == address &&
        stopped_breakpoint_->line > 0)
    {
        storage = {address, stopped_breakpoint_->line, stopped_breakpoint_->file};
        return &storage;
    }
    return metadata_.find_address_line(address);
}

void MiServer::emit_stop(StopReason reason)
{
    selected_frame_ = 0;
    z80_debug_registers_t registers{};
    z80_debug_get_registers(&registers);
    DebugLine breakpoint_line{};
    const DebugLine *line = reason == StopReason::Breakpoint ?
                            stopped_source_line(registers.pc, breakpoint_line) :
                            metadata_.find_address_line(registers.pc);
    const DebugFunction *function = metadata_.find_function(registers.pc);
    const char *reason_text = reason == StopReason::Breakpoint ? "breakpoint-hit" :
                              reason == StopReason::Step || reason == StopReason::Input ||
                              reason == StopReason::Interrupt ? "end-stepping-range" :
                              reason == StopReason::Entry ? "entry-point-hit" : "signal-received";

    std::cout << "*stopped,reason=\"" << reason_text << "\"";
    if (reason == StopReason::Breakpoint && stopped_breakpoint_)
        std::cout << ",bkptno=\"" << stopped_breakpoint_->number << "\"";
    std::cout << ",frame={addr=\"0x" << std::hex << registers.pc << std::dec
              << "\",func=\"" << (function ? function->source_name : "??") << "\"";
    emit_source(line);
    std::cout << "},thread-id=\"1\",stopped-threads=\"all\"\n" << std::flush;
}

void MiServer::emit_exit()
{
    std::cout << "*stopped,reason=\"exited-normally\"\n"
                 "=thread-exited,id=\"1\",group-id=\"i1\"\n"
                 "=thread-group-exited,id=\"i1\",exit-code=\"0\"\n" << std::flush;
}

MiServer::Breakpoint *MiServer::prepare_breakpoint_stop(uint16_t address)
{
    DebugEvaluator current = evaluator(0);
    Breakpoint *stopped = nullptr;
    for (Breakpoint &breakpoint : breakpoints_)
    {
        if (!breakpoint.active || !breakpoint.enabled || breakpoint.address != address)
            continue;
        ++breakpoint.hits;
        if (breakpoint.hits <= breakpoint.ignore_count)
            continue;
        if (!breakpoint.condition.empty())
        {
            uint32_t condition = 0;
            if (!current.evaluate_integer(breakpoint.condition, condition))
            {
                if (!breakpoint.condition_error_reported)
                {
                    std::cout << "&\"Breakpoint " << breakpoint.number
                              << " condition could not be evaluated; stopping\\n\"\n";
                    breakpoint.condition_error_reported = true;
                }
                condition = 1;
            }
            if (!condition)
                continue;
        }
        if (!stopped)
            stopped = &breakpoint;
        if (breakpoint.temporary)
            breakpoint.active = false;
    }
    return stopped;
}

MiServer::StopReason MiServer::run_target(RunMode mode)
{
    z80_debug_registers_t start{};
    z80_debug_get_registers(&start);
    const DebugLine *start_line = metadata_.find_address_line(start.pc);
    uint16_t return_pc = 0;
    uint16_t return_ix = 0;
    if (mode == RunMode::StepOut)
    {
        StackFrame context{};
        if (frame_context(selected_frame_, context) && context.ix <= 0xfffb)
        {
            return_ix = read16(context.ix);
            return_pc = read16(static_cast<uint16_t>(context.ix + 2));
        }
    }
    stopped_breakpoint_ = nullptr;
    host_.clear_input_request();
    bool executed = false;

    while (true)
    {
        z80_debug_registers_t current{};
        z80_debug_get_registers(&current);
        Breakpoint *breakpoint = breakpoint_at(current.pc);
        if (breakpoint && !(skip_breakpoint_ && current.pc == skip_address_))
        {
            stopped_breakpoint_ = prepare_breakpoint_stop(current.pc);
            if (stopped_breakpoint_)
                return StopReason::Breakpoint;
        }
        skip_breakpoint_ = false;

        host_.step();
        executed = true;
        if (debugger_interrupt_requested)
        {
            debugger_interrupt_requested = 0;
            return StopReason::Interrupt;
        }
        if (debugger_input_ready())
        {
            char first = 0;
            if (std::cin.get(first))
            {
                if (first == 3)
                    return StopReason::Interrupt;
                std::string remainder, command;
                if (!std::getline(std::cin, remainder))
                    return StopReason::Interrupt;
                std::string input(1, first);
                input += remainder;
                std::string token = token_from_line(input, command);
                if (command.rfind("-exec-interrupt", 0) == 0)
                {
                    result(token, "^done");
                    return StopReason::Interrupt;
                }
                if (command.rfind("-gdb-exit", 0) == 0)
                {
                    result(token, "^exit");
                    quit_requested_ = true;
                    return StopReason::Quit;
                }
                result(token, "^error,msg=\"target is running; only interrupt or exit is accepted\"");
            }
        }
        if (host_.input_requested() && !host_.target_terminal_connected())
            return StopReason::Input;
        if (host_.target_exited() || host_.halted())
            return StopReason::Exit;

        z80_debug_registers_t after{};
        z80_debug_get_registers(&after);
        if (mode == RunMode::Instruction && executed)
            return StopReason::Step;
        if (mode == RunMode::StepOut && return_pc != 0 &&
            after.pc == return_pc && after.ix == return_ix)
            return StopReason::Step;
        if (mode == RunMode::StepInto || mode == RunMode::StepOver)
        {
            const DebugLine *line = metadata_.find_address_line(after.pc);
            bool changed = line && (!start_line || line->line != start_line->line || line->file != start_line->file);
            if (changed && (mode != RunMode::StepOver || after.sp >= start.sp))
                return StopReason::Step;
        }
    }
}

void MiServer::handle_run(const std::string &token, RunMode mode)
{
    std::string error;
    bool resuming_target = host_.target_active();
    if (!host_.prepare_program(program_, program_arguments_, error))
    {
        result(token, "^error,msg=\"" + mi_escape(error) + "\"");
        return;
    }
    z80_debug_registers_t registers{};
    z80_debug_get_registers(&registers);
    skip_address_ = registers.pc;
    skip_breakpoint_ = resuming_target && breakpoint_at(registers.pc) != nullptr;
    emit_running(token);
    StopReason stop = run_target(mode);
    if (stop == StopReason::Quit)
        return;
    if (stop == StopReason::Exit)
    {
        std::string save_error;
        if (host_.target_exited() && !host_.save_fixtures(save_error))
            std::cout << "&\"" << mi_escape("cannot save fixtures: " + save_error + "\n")
                      << "\"\n";
        emit_exit();
    }
    else
        emit_stop(stop);
}

bool MiServer::parse_break_location(const std::string &command, uint16_t &address,
                                    std::string &file, int &line)
{
    size_t star = command.rfind('*');
    if (star != std::string::npos)
    {
        char *end = nullptr;
        unsigned long value = std::strtoul(command.c_str() + star + 1, &end, 0);
        const char *start = command.c_str() + star + 1;
        while (end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (end != start && end && *end == 0 && value <= 0xffff)
        {
            address = static_cast<uint16_t>(value);
            return true;
        }
    }

    size_t file_option = command.find(" -f ");
    size_t line_option = command.find(" -l ");
    if (file_option != std::string::npos && line_option != std::string::npos)
    {
        file = quoted_argument(command.substr(file_option + 4));
        std::string line_text = option_argument(command, "-l");
        char *end = nullptr;
        long parsed_line = std::strtol(line_text.c_str(), &end, 10);
        if (line_text.empty() || end == line_text.c_str() || *end ||
            parsed_line <= 0 || parsed_line > std::numeric_limits<int>::max())
            return false;
        line = static_cast<int>(parsed_line);
    }
    else
    {
        std::string location = last_argument(command);
        size_t colon = location.rfind(':');
        if (colon != std::string::npos)
        {
            file = location.substr(0, colon);
            std::string line_text = location.substr(colon + 1);
            char *end = nullptr;
            long parsed_line = std::strtol(line_text.c_str(), &end, 10);
            if (line_text.empty() || end == line_text.c_str() || *end ||
                parsed_line <= 0 || parsed_line > std::numeric_limits<int>::max())
                return false;
            line = static_cast<int>(parsed_line);
        }
        else if (const DebugFunction *function = metadata_.find_function(location))
        {
            address = function->start;
            return true;
        }
    }
    if (line <= 0)
        return false;
    const DebugLine *debug_line = metadata_.find_source_line(file, line);
    if (!debug_line || debug_line->line != line)
        return false;
    address = debug_line->address;
    file = debug_line->file;
    return true;
}

void MiServer::handle_command(const std::string &input, bool &done)
{
    std::string command;
    std::string token = token_from_line(input, command);
    int saved_frame = selected_frame_;
    int command_frame = inline_frame(command);
    if (command_frame == -2)
    {
        result(token, "^error,msg=\"invalid frame\"");
        return;
    }
    if (command_frame >= 0)
    {
        if (static_cast<size_t>(command_frame) >= stack_frames().size())
        {
            result(token, "^error,msg=\"invalid frame\"");
            return;
        }
        selected_frame_ = command_frame;
    }
    struct FrameRestore
    {
        int &selected;
        int saved;
        bool restore;
        ~FrameRestore() { if (restore) selected = saved; }
    } frame_restore{selected_frame_, saved_frame, command_frame >= 0};

    if (command.rfind("-file-exec-and-symbols", 0) == 0)
    {
        std::string error;
        program_ = fs::absolute(quoted_argument(command));
        if (!metadata_.load_for_program(program_, error))
        {
            result(token, "^error,msg=\"" + mi_escape(error) + "\"");
            return;
        }
        variable_objects_.clear();
        selected_frame_ = 0;
        result(token, "^done");
        if (!thread_started_)
        {
            thread_started_ = true;
            std::cout << "=thread-group-started,id=\"i1\",pid=\""
                      << debugger_process_id() << "\"\n"
                         "=thread-created,id=\"1\",group-id=\"i1\"\n";
            emit_stop(StopReason::Entry);
        }
    }
    else if (command.rfind("-exec-arguments", 0) == 0)
    {
        size_t space = command.find(' ');
        program_arguments_ = space == std::string::npos ? "" : command.substr(space + 1);
        result(token, "^done");
    }
    else if (command.rfind("-environment-cd", 0) == 0)
    {
        std::error_code code;
        fs::current_path(quoted_argument(command), code);
        result(token, code ? "^error,msg=\"cannot change directory\"" : "^done");
    }
    else if (command.rfind("-break-insert", 0) == 0)
    {
        uint16_t address = 0;
        std::string file;
        int source_line = 0;
        if (!parse_break_location(command, address, file, source_line))
        {
            result(token, "^error,msg=\"no executable source location found\"");
            return;
        }
        Breakpoint breakpoint;
        breakpoint.number = next_breakpoint_++;
        breakpoint.address = address;
        breakpoint.active = true;
        breakpoint.temporary = command.find(" -t ") != std::string::npos;
        breakpoint.condition = option_argument(command, "-c");
        std::string ignore = option_argument(command, "-i");
        if (command.find(" -c") != std::string::npos && breakpoint.condition.empty())
        {
            result(token, "^error,msg=\"breakpoint condition is empty\"");
            return;
        }
        if (!ignore.empty())
        {
            char *end = nullptr;
            breakpoint.ignore_count = std::strtoul(ignore.c_str(), &end, 0);
            if (ignore.front() == '-' || end == ignore.c_str() || *end)
            {
                result(token, "^error,msg=\"invalid breakpoint ignore count\"");
                return;
            }
        }
        else if (command.find(" -i") != std::string::npos)
        {
            result(token, "^error,msg=\"invalid breakpoint ignore count\"");
            return;
        }
        breakpoint.file = file;
        breakpoint.line = source_line;
        breakpoints_.push_back(breakpoint);
        std::ostringstream response;
        response << "^done,bkpt={number=\"" << breakpoint.number
                 << "\",type=\"breakpoint\",disp=\""
                 << (breakpoint.temporary ? "del" : "keep")
                 << "\",enabled=\"y\",addr=\"0x" << std::hex << address << std::dec << "\"";
        if (source_line > 0)
            response << ",file=\"" << mi_escape(file) << "\",fullname=\""
                     << mi_escape(metadata_.source_full_path(file).string())
                     << "\",line=\"" << source_line << "\"";
        response << ",thread-groups=[\"i1\"],times=\"0\"}";
        result(token, response.str());
    }
    else if (command.rfind("-break-list", 0) == 0)
    {
        size_t count = std::count_if(breakpoints_.begin(), breakpoints_.end(),
                                     [](const Breakpoint &breakpoint) { return breakpoint.active; });
        std::ostringstream response;
        response << "^done,BreakpointTable={nr_rows=\"" << count
                 << "\",nr_cols=\"6\",body=[";
        bool first = true;
        for (const Breakpoint &breakpoint : breakpoints_)
        {
            if (!breakpoint.active) continue;
            if (!first) response << ',';
            response << "bkpt={number=\"" << breakpoint.number
                     << "\",type=\"breakpoint\",disp=\""
                     << (breakpoint.temporary ? "del" : "keep")
                     << "\",enabled=\"" << (breakpoint.enabled ? "y" : "n")
                     << "\",addr=\"0x" << std::hex
                     << breakpoint.address << std::dec << "\",times=\""
                     << breakpoint.hits << "\"}";
            first = false;
        }
        response << "]}";
        result(token, response.str());
    }
    else if (command.rfind("-break-delete", 0) == 0)
    {
        std::vector<int> numbers;
        if (!strict_command_numbers(command, numbers))
        {
            result(token, "^error,msg=\"invalid breakpoint number\"");
            return;
        }
        for (int number : numbers)
            for (Breakpoint &breakpoint : breakpoints_)
                if (breakpoint.number == number)
                    breakpoint.active = false;
        result(token, "^done");
    }
    else if (command.rfind("-break-enable", 0) == 0 || command.rfind("-break-disable", 0) == 0)
    {
        bool enabled = command.rfind("-break-enable", 0) == 0;
        std::vector<int> numbers;
        if (!strict_command_numbers(command, numbers))
        {
            result(token, "^error,msg=\"invalid breakpoint number\"");
            return;
        }
        for (int number : numbers)
            for (Breakpoint &breakpoint : breakpoints_)
                if (breakpoint.active && breakpoint.number == number)
                    breakpoint.enabled = enabled;
        result(token, "^done");
    }
    else if (command.rfind("-break-after", 0) == 0)
    {
        std::vector<int> numbers;
        if (!strict_command_numbers(command, numbers) || numbers.size() != 2)
            result(token, "^error,msg=\"breakpoint number and ignore count required\"");
        else
        {
            for (Breakpoint &breakpoint : breakpoints_)
                if (breakpoint.number == numbers[0])
                    breakpoint.ignore_count = static_cast<unsigned long>(std::max(0, numbers[1]));
            result(token, "^done");
        }
    }
    else if (command.rfind("-break-condition", 0) == 0)
    {
        std::vector<int> numbers = command_numbers(command);
        if (numbers.empty())
            result(token, "^error,msg=\"breakpoint number required\"");
        else
        {
            size_t number_position = command.find(std::to_string(numbers[0]));
            size_t expression_position = command.find_first_not_of(" \t", number_position + std::to_string(numbers[0]).size());
            std::string condition = expression_position == std::string::npos ? "" :
                                    quoted_argument(" " + command.substr(expression_position));
            for (Breakpoint &breakpoint : breakpoints_)
                if (breakpoint.number == numbers[0])
                {
                    breakpoint.condition = condition;
                    breakpoint.condition_error_reported = false;
                }
            result(token, "^done");
        }
    }
    else if (command.rfind("-exec-step-instruction", 0) == 0 ||
             command.rfind("-exec-next-instruction", 0) == 0)
        handle_run(token, RunMode::Instruction);
    else if (command.rfind("-exec-run", 0) == 0 || command.rfind("-exec-continue", 0) == 0)
        handle_run(token, RunMode::Continue);
    else if (command.rfind("-exec-finish", 0) == 0)
        handle_run(token, RunMode::StepOut);
    else if (command.rfind("-exec-until", 0) == 0)
    {
        uint16_t address = 0;
        std::string file;
        int source_line = 0;
        if (!parse_break_location(command, address, file, source_line))
            result(token, "^error,msg=\"no executable source location found\"");
        else
        {
            Breakpoint breakpoint;
            breakpoint.number = next_breakpoint_++;
            breakpoint.address = address;
            breakpoint.active = true;
            breakpoint.temporary = true;
            breakpoint.file = file;
            breakpoint.line = source_line;
            breakpoints_.push_back(std::move(breakpoint));
            handle_run(token, RunMode::Continue);
        }
    }
    else if (command.rfind("-exec-step", 0) == 0)
        handle_run(token, RunMode::StepInto);
    else if (command.rfind("-exec-next", 0) == 0)
        handle_run(token, RunMode::StepOver);
    else if (command.rfind("-exec-interrupt", 0) == 0)
        result(token, "^done");
    else if (command.rfind("-interpreter-exec", 0) == 0 && command.find("input") != std::string::npos)
    {
        std::string value = quoted_argument(command);
        size_t position = value.find("input");
        value = position == std::string::npos ? "" : value.substr(position + 5);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.erase(value.begin());
        host_.queue_input(value, true);
        host_.clear_input_request();
        handle_run(token, RunMode::Continue);
    }
    else if (command.rfind("-symbol-list-lines", 0) == 0)
    {
        std::vector<DebugLine> lines = metadata_.source_lines(quoted_argument(command));
        std::ostringstream response;
        response << "^done,lines=[";
        for (size_t index = 0; index < lines.size(); ++index)
        {
            if (index) response << ',';
            response << "{pc=\"0x" << std::hex << lines[index].address << std::dec
                     << "\",line=\"" << lines[index].line << "\"}";
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-file-list-exec-source-files", 0) == 0)
    {
        std::ostringstream response;
        response << "^done,files=[";
        std::vector<std::string> files = metadata_.source_files();
        for (size_t index = 0; index < files.size(); ++index)
        {
            if (index) response << ',';
            response << "{file=\"" << mi_escape(files[index]) << "\",fullname=\""
                     << mi_escape(metadata_.source_full_path(files[index]).string()) << "\"}";
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-file-list-exec-source-file", 0) == 0)
    {
        z80_debug_registers_t registers{};
        z80_debug_get_registers(&registers);
        DebugLine breakpoint_line{};
        const DebugLine *line = stopped_source_line(registers.pc, breakpoint_line);
        if (!line)
            result(token, "^done,line=\"0\",file=\"\",fullname=\"\",macro-info=\"0\"");
        else
            result(token, "^done,line=\"" + std::to_string(line->line) + "\",file=\"" +
                   mi_escape(line->file) + "\",fullname=\"" +
                   mi_escape(metadata_.source_full_path(line->file).string()) + "\",macro-info=\"0\"");
    }
    else if (command.rfind("-data-evaluate-expression", 0) == 0)
    {
        std::string expression = last_argument(command);
        if (!expression.empty() && expression.front() == '$')
        {
            z80_debug_registers_t registers{};
            z80_debug_get_registers(&registers);
            std::string name = expression.substr(1);
            uint16_t value;
            bool found = true;
            if (name == "af") value = registers.af;
            else if (name == "bc") value = registers.bc;
            else if (name == "de") value = registers.de;
            else if (name == "hl") value = registers.hl;
            else if (name == "ix") value = registers.ix;
            else if (name == "iy") value = registers.iy;
            else if (name == "sp") value = registers.sp;
            else if (name == "pc") value = registers.pc;
            else if (name == "i") value = registers.i;
            else if (name == "r") value = registers.r;
            else { value = 0; found = false; }
            if (!found)
                result(token, "^error,msg=\"unknown register\"");
            else
            {
                std::ostringstream response;
                response << "^done,value=\"0x" << std::hex << std::setw(4)
                         << std::setfill('0') << value << "\"";
                result(token, response.str());
            }
        }
        else
        {
            DebugEvaluator current = evaluator(selected_frame_);
            DebugValue value;
            if (!current.evaluate(expression, value))
                result(token, "^error,msg=\"unsupported, unavailable, or side-effecting expression\"");
            else
                result(token, "^done,value=\"" + mi_escape(current.format(value)) + "\"");
        }
    }
    else if (command.rfind("-var-create", 0) == 0)
    {
        std::string expression = last_argument(command);
        DebugEvaluator current = evaluator(selected_frame_);
        DebugValue value;
        if (!current.evaluate(expression, value))
        {
            result(token, "^error,msg=\"variable is not available in the current scope\"");
            return;
        }
        VariableObject object;
        object.frame = selected_frame_;
        StackFrame object_context{};
        if (!frame_context(object.frame, object_context))
        {
            result(token, "^error,msg=\"variable is not available in the current scope\"");
            return;
        }
        object.frame_ix = object_context.ix;
        object.name = "var" + std::to_string(next_variable_object_++);
        object.expression = expression;
        variable_objects_.push_back(object);
        std::ostringstream response;
        response << "^done,name=\"" << object.name << "\",numchild=\""
                 << current.child_count(value) << "\",value=\""
                 << mi_escape(current.format(value)) << "\",type=\""
                 << mi_escape(current.type_name(value))
                 << "\",thread-id=\"1\",has_more=\"0\"";
        result(token, response.str());
    }
    else if (command.rfind("-var-evaluate-expression", 0) == 0 ||
             command.rfind("-var-info-type", 0) == 0 ||
             command.rfind("-var-info-num-children", 0) == 0 ||
             command.rfind("-var-info-expression", 0) == 0 ||
             command.rfind("-var-show-attributes", 0) == 0 ||
             command.rfind("-var-list-children", 0) == 0)
    {
        VariableObject *object = find_variable_object(command);
        if (!object)
        {
            result(token, "^error,msg=\"variable object is not available\"");
            return;
        }
        int object_frame = object->frame;
        uint16_t object_frame_ix = object->frame_ix;
        std::string object_expression = object->expression;
        StackFrame object_context{};
        if (!frame_context(object_frame, object_context) || object_context.ix != object_frame_ix)
        {
            result(token, "^error,msg=\"variable object is not available\"");
            return;
        }
        DebugEvaluator current = evaluator(object_frame);
        DebugValue value;
        if (!current.evaluate(object_expression, value))
        {
            result(token, "^error,msg=\"variable object is not available\"");
            return;
        }
        if (command.rfind("-var-info-type", 0) == 0)
            result(token, "^done,type=\"" + mi_escape(current.type_name(value)) + "\"");
        else if (command.rfind("-var-info-num-children", 0) == 0)
            result(token, "^done,numchild=\"" + std::to_string(current.child_count(value)) + "\"");
        else if (command.rfind("-var-info-expression", 0) == 0)
            result(token, "^done,lang=\"C\",exp=\"" + mi_escape(object_expression) + "\"");
        else if (command.rfind("-var-show-attributes", 0) == 0)
            result(token, current.writable(value) ? "^done,attr=\"editable\"" :
                                                   "^done,attr=\"noneditable\"");
        else if (command.rfind("-var-list-children", 0) == 0)
        {
            int count = current.child_count(value);
            std::ostringstream response;
            response << "^done,numchild=\"" << count << "\",children=[";
            bool first = true;
            for (int index = 0; index < count; ++index)
            {
                std::string child_expression, display_name;
                DebugValue child;
                if (!current.child_expression(value, object_expression, index,
                                              child_expression, display_name) ||
                    !current.resolve(child_expression, child))
                    continue;
                VariableObject child_object;
                child_object.frame = object_frame;
                child_object.frame_ix = object_frame_ix;
                child_object.expression = child_expression;
                auto existing = std::find_if(variable_objects_.begin(), variable_objects_.end(),
                    [&](const VariableObject &candidate)
                    {
                        return candidate.active && candidate.frame == child_object.frame &&
                               candidate.frame_ix == child_object.frame_ix &&
                               candidate.expression == child_object.expression;
                    });
                if (existing != variable_objects_.end())
                    child_object.name = existing->name;
                else
                {
                    child_object.name = "var" + std::to_string(next_variable_object_++);
                    variable_objects_.push_back(child_object);
                }
                if (!first) response << ',';
                response << "child={name=\"" << child_object.name << "\",exp=\""
                         << mi_escape(display_name) << "\",numchild=\""
                         << current.child_count(child) << "\",value=\""
                         << mi_escape(current.format(child)) << "\",type=\""
                         << mi_escape(current.type_name(child)) << "\",thread-id=\"1\"}";
                first = false;
            }
            response << "],has_more=\"0\"";
            result(token, response.str());
        }
        else
            result(token, "^done,value=\"" + mi_escape(current.format(value)) + "\"");
    }
    else if (command.rfind("-var-assign", 0) == 0)
    {
        VariableObject *object = find_variable_object(command);
        std::string assignment = last_argument(command);
        if (!object)
        {
            result(token, "^error,msg=\"variable object is not available\"");
            return;
        }
        StackFrame object_context{};
        if (!frame_context(object->frame, object_context) || object_context.ix != object->frame_ix)
        {
            result(token, "^error,msg=\"variable object is not available\"");
            return;
        }
        DebugEvaluator current = evaluator(object->frame);
        DebugValue value;
        uint32_t assigned;
        if (!current.resolve(object->expression, value) ||
            !current.evaluate_integer(assignment, assigned) || !current.write(value, assigned))
            result(token, "^error,msg=\"variable is not editable or value is unsupported\"");
        else
        {
            DebugEvaluator refreshed = evaluator(object->frame);
            DebugValue updated;
            result(token, "^done,value=\"" +
                          mi_escape(refreshed.resolve(object->expression, updated) ?
                                    refreshed.format(updated) : current.format(value)) + "\"");
        }
    }
    else if (command.rfind("-var-update", 0) == 0)
    {
        std::ostringstream response;
        response << "^done,changelist=[";
        bool first = true;
        for (const VariableObject &object : variable_objects_)
        {
            if (!object.active) continue;
            StackFrame object_context{};
            if (!frame_context(object.frame, object_context) || object_context.ix != object.frame_ix)
            {
                if (!first) response << ',';
                response << "{name=\"" << object.name << "\",in_scope=\"false\"}";
                first = false;
                continue;
            }
            DebugEvaluator current = evaluator(object.frame);
            DebugValue value;
            if (!first) response << ',';
            response << "{name=\"" << object.name << "\"";
            if (!current.evaluate(object.expression, value))
                response << ",in_scope=\"false\"}";
            else
                response << ",value=\"" << mi_escape(current.format(value))
                         << "\",in_scope=\"true\",type_changed=\"false\",has_more=\"0\"}";
            first = false;
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-var-delete", 0) == 0)
    {
        std::string name = last_argument(command);
        int deleted = 0;
        for (VariableObject &object : variable_objects_)
            if (object.active && object.name == name)
            {
                object.active = false;
                ++deleted;
            }
        result(token, "^done,ndeleted=\"" + std::to_string(deleted) + "\"");
    }
    else if (command.rfind("-var-set-format", 0) == 0 ||
             command.rfind("-var-set-frozen", 0) == 0)
        result(token, "^done");
    else if (command.rfind("-data-list-register-names", 0) == 0)
        result(token, "^done,register-names=[\"af\",\"bc\",\"de\",\"hl\",\"ix\",\"iy\",\"sp\",\"pc\",\"af'\",\"bc'\",\"de'\",\"hl'\",\"i\",\"r\"]");
    else if (command.rfind("-data-list-register-values", 0) == 0)
    {
        z80_debug_registers_t registers{};
        z80_debug_get_registers(&registers);
        std::array<uint16_t, 14> values = {registers.af, registers.bc, registers.de, registers.hl,
                                           registers.ix, registers.iy, registers.sp, registers.pc,
                                           registers.af_alt, registers.bc_alt, registers.de_alt,
                                           registers.hl_alt, registers.i, registers.r};
        std::ostringstream response;
        response << "^done,register-values=[";
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (index) response << ',';
            response << "{number=\"" << index << "\",value=\"0x" << std::hex
                     << std::setw(4) << std::setfill('0') << values[index]
                     << std::dec << "\"}";
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-data-read-memory-bytes", 0) == 0)
    {
        std::istringstream arguments(command.substr(command.find(' ') + 1));
        std::string address_text, offset_text, count_text;
        long offset = 0;
        arguments >> address_text;
        if (address_text == "-o")
        {
            arguments >> offset_text >> address_text;
            char *offset_end = nullptr;
            offset = std::strtol(offset_text.c_str(), &offset_end, 0);
            if (offset_end == offset_text.c_str() || *offset_end)
            {
                result(token, "^error,msg=\"invalid memory range\"");
                return;
            }
        }
        arguments >> count_text;
        char *address_end = nullptr;
        char *count_end = nullptr;
        unsigned long base = std::strtoul(address_text.c_str(), &address_end, 0);
        unsigned long count = std::strtoul(count_text.c_str(), &count_end, 0);
        if (address_text.empty() || address_text.front() == '-' ||
            address_end == address_text.c_str() || *address_end ||
            count_text.empty() || count_text.front() == '-' ||
            count_end == count_text.c_str() || *count_end || base > 0xffff)
        {
            result(token, "^error,msg=\"invalid memory range\"");
            return;
        }
        int64_t requested = static_cast<int64_t>(base) + offset;
        if (requested < 0 || requested > 0xffff ||
            count > 0x10000UL - static_cast<unsigned long>(requested))
        {
            result(token, "^error,msg=\"invalid memory range\"");
            return;
        }
        unsigned long address = static_cast<unsigned long>(requested);
        std::ostringstream response;
        response << "^done,memory=[{begin=\"0x" << std::hex << address
                 << "\",offset=\"0x0\",end=\"0x" << address + count << "\",contents=\"";
        for (unsigned long index = 0; index < count; ++index)
            response << std::setw(2) << std::setfill('0') << static_cast<unsigned>(memory[address + index]);
        response << "\"}]";
        result(token, response.str());
    }
    else if (command.rfind("-data-write-memory-bytes", 0) == 0)
    {
        std::istringstream arguments(command.substr(command.find(' ') + 1));
        std::string address_text, contents;
        arguments >> address_text >> contents;
        char *address_end = nullptr;
        unsigned long address = std::strtoul(address_text.c_str(), &address_end, 0);
        if (address_text.empty() || address_text.front() == '-' ||
            address_end == address_text.c_str() || *address_end || address > 0xffff ||
            contents.empty() || contents.size() % 2 != 0 ||
            contents.size() / 2 > 0x10000 - address)
        {
            result(token, "^error,msg=\"invalid memory write\"");
            return;
        }
        std::vector<uint8_t> bytes;
        bytes.reserve(contents.size() / 2);
        for (size_t index = 0; index < contents.size(); index += 2)
        {
            std::string byte_text = contents.substr(index, 2);
            char *end = nullptr;
            unsigned long byte = std::strtoul(byte_text.c_str(), &end, 16);
            if (end == byte_text.c_str() || *end || byte > 0xff)
            {
                result(token, "^error,msg=\"invalid memory contents\"");
                return;
            }
            bytes.push_back(static_cast<uint8_t>(byte));
        }
        std::copy(bytes.begin(), bytes.end(), memory + address);
        result(token, "^done");
    }
    else if (command.rfind("-data-disassemble", 0) == 0)
    {
        std::string start_text = option_argument(command, "-s");
        std::string end_text = option_argument(command, "-e");
        if (start_text.empty() || end_text.empty())
        {
            result(token, "^error,msg=\"missing disassembly range\"");
            return;
        }
        char *start_end = nullptr;
        char *end_end = nullptr;
        unsigned long start = std::strtoul(start_text.c_str(), &start_end, 0);
        unsigned long end = std::strtoul(end_text.c_str(), &end_end, 0);
        size_t mode_option = command.find("--");
        char *mode_end = nullptr;
        const char *mode_start = mode_option == std::string::npos ? nullptr : command.c_str() + mode_option + 2;
        while (mode_start && std::isspace(static_cast<unsigned char>(*mode_start))) ++mode_start;
        long mode = mode_start ? std::strtol(mode_start, &mode_end, 10) : -1;
        while (mode_end && std::isspace(static_cast<unsigned char>(*mode_end))) ++mode_end;
        if (start_text.front() == '-' || start_end == start_text.c_str() || *start_end ||
            end_text.front() == '-' || end_end == end_text.c_str() || *end_end ||
            start > 0xffff || end > 0x10000 || end < start || !mode_start ||
            mode_end == mode_start || !mode_end || *mode_end || (mode != 0 && mode != 2))
        {
            result(token, "^error,msg=\"invalid or unsupported disassembly range\"");
            return;
        }
        std::ostringstream response;
        response << "^done,asm_insns=[";
        bool first = true;
        for (unsigned long address = start; address < end && address <= 0xffff;)
        {
            uint8_t length = z80_debug_instruction_length(static_cast<uint16_t>(address));
            std::string instruction;
            if (!length || length > 0x10000UL - address)
            {
                length = 1;
                std::ostringstream data;
                data << "db " << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<unsigned>(memory[address]) << 'h';
                instruction = data.str();
            }
            else
                instruction = z80_debug_disassemble(static_cast<uint16_t>(address));
            if (!first) response << ',';
            const DebugFunction *function = metadata_.find_function(static_cast<uint16_t>(address));
            response << "{address=\"0x" << std::hex << address << std::dec
                     << "\",func-name=\"" << (function ? function->source_name : "??")
                     << "\",offset=\"" << (function ? address - function->start : 0) << "\",opcodes=\"";
            for (uint8_t index = 0; index < length; ++index)
            {
                if (index) response << ' ';
                response << std::hex << std::setw(2) << std::setfill('0')
                         << static_cast<unsigned>(memory[(address + index) & 0xffff]) << std::dec;
            }
            response << "\",inst=\"" << mi_escape(instruction) << "\"}";
            first = false;
            address += length;
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-stack-select-frame", 0) == 0)
    {
        int frame = std::atoi(last_argument(command).c_str());
        if (frame < 0 || static_cast<size_t>(frame) >= stack_frames().size())
            result(token, "^error,msg=\"invalid stack frame\"");
        else
        {
            selected_frame_ = frame;
            result(token, "^done");
        }
    }
    else if (command.rfind("-stack-info-frame", 0) == 0)
    {
        StackFrame context{};
        if (!frame_context(selected_frame_, context))
        {
            result(token, "^error,msg=\"invalid stack frame\"");
            return;
        }
        const DebugFunction *function = metadata_.find_function(context.pc);
        DebugLine breakpoint_line{};
        const DebugLine *line = selected_frame_ == 0 ?
                    stopped_source_line(context.pc, breakpoint_line) :
                    metadata_.find_address_line(context.pc);
        std::ostringstream response;
        response << "^done,frame={level=\"" << selected_frame_ << "\",addr=\"0x"
                 << std::hex << context.pc << std::dec << "\",func=\""
                 << (function ? function->source_name : "??") << "\"";
        if (line)
            response << ",file=\"" << mi_escape(line->file) << "\",fullname=\""
                     << mi_escape(metadata_.source_full_path(line->file).string())
                     << "\",line=\"" << line->line << "\"";
        response << '}';
        result(token, response.str());
    }
    else if (command.rfind("-stack-list-frames", 0) == 0)
    {
        std::vector<StackFrame> frames = stack_frames();
        std::ostringstream response;
        response << "^done,stack=[";
        for (size_t index = 0; index < frames.size(); ++index)
        {
            const DebugFunction *function = metadata_.find_function(frames[index].pc);
            DebugLine breakpoint_line{};
            const DebugLine *line = index == 0 ?
                                    stopped_source_line(frames[index].pc, breakpoint_line) :
                                    metadata_.find_address_line(frames[index].pc);
            if (index) response << ',';
            response << "frame={level=\"" << index << "\",addr=\"0x" << std::hex
                     << frames[index].pc << std::dec << "\",func=\""
                     << (function ? function->source_name : "??") << "\"";
            if (line)
                response << ",file=\"" << mi_escape(line->file) << "\",fullname=\""
                         << mi_escape(metadata_.source_full_path(line->file).string())
                         << "\",line=\"" << line->line << "\"";
            response << '}';
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-stack-info-depth", 0) == 0)
        result(token, "^done,depth=\"" + std::to_string(stack_frames().size()) + "\"");
    else if (command.rfind("-stack-list-locals", 0) == 0 || command.rfind("-stack-list-variables", 0) == 0)
    {
        StackFrame context{};
        if (!frame_context(selected_frame_, context))
        {
            result(token, "^error,msg=\"invalid stack frame\"");
            return;
        }
        DebugEvaluator current = evaluator(selected_frame_);
        std::ostringstream response;
        response << "^done,variables=[";
        bool first = true;
        for (const DebugVariable *variable : metadata_.scoped_variables(context.pc))
        {
            DebugValue value = current.value_from_variable(*variable);
            if (!first) response << ',';
            response << "{name=\"" << mi_escape(variable->name) << "\",value=\""
                     << mi_escape(current.format(value)) << "\",type=\""
                     << mi_escape(current.variable_type_name(*variable)) << "\""
                     << (variable->storage == 3 ? ",arg=\"1\"" : "") << '}';
            first = false;
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-stack-list-arguments", 0) == 0)
    {
        std::vector<StackFrame> frames = stack_frames();
        std::ostringstream response;
        response << "^done,stack-args=[";
        for (size_t frame = 0; frame < frames.size(); ++frame)
        {
            if (frame) response << ',';
            DebugEvaluator current = evaluator(static_cast<int>(frame));
            response << "frame={level=\"" << frame << "\",args=[";
            bool first = true;
            for (const DebugVariable *variable : metadata_.scoped_variables(frames[frame].pc))
            {
                if (variable->storage != 3) continue;
                if (!first) response << ',';
                response << "{name=\"" << mi_escape(variable->name) << "\",value=\""
                         << mi_escape(current.format(current.value_from_variable(*variable))) << "\"}";
                first = false;
            }
            response << "]}";
        }
        response << ']';
        result(token, response.str());
    }
    else if (command.rfind("-thread-info", 0) == 0)
        result(token, "^done,threads=[{id=\"1\",target-id=\"CP/M\",state=\"stopped\"}],current-thread-id=\"1\"");
    else if (command.rfind("-thread-list-ids", 0) == 0)
        result(token, "^done,thread-ids={thread-id=\"1\",number-of-threads=\"1\"},current-thread-id=\"1\"");
    else if (command.rfind("-list-thread-groups", 0) == 0)
        result(token, "^done,groups=[{id=\"i1\",type=\"process\",pid=\"" +
                      std::to_string(debugger_process_id()) +
                      "\",executable=\"dcc-debug-host\"}]");
    else if (command.rfind("-list-features", 0) == 0)
        result(token, "^done,features=[]");
    else if (command.rfind("-gdb-exit", 0) == 0)
    {
        result(token, "^exit");
        done = true;
    }
    else
        result(token, "^done");
}

int MiServer::run()
{
    bool done = false;
    std::string input;
    std::signal(SIGINT, debugger_interrupt_handler);
#ifdef SIGTRAP
    std::signal(SIGTRAP, debugger_interrupt_handler);
#endif
    std::cout << "=thread-group-added,id=\"i1\"\n(gdb) \n" << std::flush;
    while (!done && !quit_requested_ && std::getline(std::cin, input))
        handle_command(input, done);
    return 0;
}
