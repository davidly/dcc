#pragma once

#include "dcc_host_debug_metadata.hpp"
#include "dcc_host_debug_evaluator.hpp"
#include "dcc_host_full_cpm.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class MiServer
{
public:
    explicit MiServer(FullCpmHost &host);
    int run();

private:
    struct Breakpoint
    {
        int number = 0;
        uint16_t address = 0;
        bool active = false;
        bool enabled = true;
        bool temporary = false;
        bool condition_error_reported = false;
        unsigned long hits = 0;
        unsigned long ignore_count = 0;
        std::string condition;
        std::string file;
        int line = 0;
    };

    struct VariableObject
    {
        bool active = true;
        int frame = 0;
        uint16_t frame_ix = 0;
        std::string name;
        std::string expression;
    };

    enum class StopReason { Entry, Breakpoint, Step, Input, Interrupt, Exit, Quit, Error };
    enum class RunMode { Continue, StepInto, StepOver, StepOut, Instruction };
    struct StackFrame { uint16_t pc; uint16_t ix; };

    void handle_command(const std::string &input, bool &done);
    void handle_run(const std::string &token, RunMode mode);
    StopReason run_target(RunMode mode);
    void emit_stop(StopReason reason);
    void emit_exit();
    void emit_source(const DebugLine *line);
    const DebugLine *stopped_source_line(uint16_t address, DebugLine &storage) const;
    void emit_running(const std::string &token);
    void result(const std::string &token, const std::string &value);

    bool parse_break_location(const std::string &command, uint16_t &address,
                              std::string &file, int &line);
    Breakpoint *breakpoint_at(uint16_t address);
    Breakpoint *prepare_breakpoint_stop(uint16_t address);
    std::vector<StackFrame> stack_frames() const;
    bool frame_context(int frame, StackFrame &context) const;
    DebugEvaluator evaluator(int frame) const;
    VariableObject *find_variable_object(const std::string &command);
    static int inline_frame(const std::string &command);
    static std::vector<int> command_numbers(const std::string &command);
    static bool strict_command_numbers(const std::string &command,
                                       std::vector<int> &numbers);
    static std::string option_argument(const std::string &command, const std::string &option);

    static std::string mi_escape(const std::string &text);
    static std::string quoted_argument(const std::string &text);
    static std::string last_argument(const std::string &command);
    static std::string token_from_line(const std::string &line, std::string &command);

    FullCpmHost &host_;
    DebugMetadata metadata_;
    std::filesystem::path program_;
    std::string program_arguments_;
    std::vector<Breakpoint> breakpoints_;
    std::vector<VariableObject> variable_objects_;
    int next_breakpoint_ = 1;
    int next_variable_object_ = 1;
    int selected_frame_ = 0;
    bool thread_started_ = false;
    bool quit_requested_ = false;
    bool skip_breakpoint_ = false;
    uint16_t skip_address_ = 0;
    Breakpoint *stopped_breakpoint_ = nullptr;
};
