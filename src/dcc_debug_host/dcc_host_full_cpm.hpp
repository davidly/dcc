#pragma once

#include "dcc_host_debug_fixture.hpp"
#include "dcc_host_io_adapter_loader.hpp"
#include "dcc_host_target_terminal.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

extern "C"
{
#include "z80.h"
}

struct FullCpmOptions
{
    std::filesystem::path drive_a;
    std::filesystem::path drive_c;
    std::filesystem::path drive_d;
    std::filesystem::path io_adapter;
    std::filesystem::path environment_file;
    std::filesystem::path terminal_endpoint_file;
    std::filesystem::path save_fixtures_directory;
    std::vector<DebugFixture> fixtures;
};

class FullCpmHost
{
public:
    explicit FullCpmHost(FullCpmOptions options);
    ~FullCpmHost();

    bool initialize(std::string &error);
    bool prepare_program(const std::filesystem::path &program,
                         const std::string &arguments,
                         std::string &error);
    bool save_fixtures(std::string &error);

    void step();
    void queue_input(const std::string &text, bool add_return = true);
    void clear_input_request();
    void set_output(std::function<void(uint8_t)> callback) { output_callback_ = std::move(callback); }

    bool input_requested() const;
    bool target_active() const { return target_active_.load(); }
    bool target_terminal_connected() const { return target_terminal_.connected(); }
    bool target_exited() const;
    bool halted() const { return cpu_.halted; }
    uint16_t pc() const { return cpu_.registers.pc; }
    const std::filesystem::path &session_root() const { return session_root_; }

private:
    static FullCpmHost *active_;
    static uint8_t terminal_read_callback();
    static void terminal_write_callback(uint8_t character);
    static uint8_t sense_switches_callback();
    static uint8_t io_port_in_callback(uint8_t port);
    static void io_port_out_callback(uint8_t port, uint8_t data);
    static int register_interrupt_callback(
        void *context, uint8_t data_bus,
        dcc_debug_io_interrupt_poll_fn poll, void *poll_context,
        dcc_debug_io_interrupt_id_t *interrupt_id);
    static void raise_interrupt_callback(
        void *context, dcc_debug_io_interrupt_id_t interrupt_id);
    static void clear_interrupt_callback(
        void *context, dcc_debug_io_interrupt_id_t interrupt_id);

    uint8_t terminal_read();
    void terminal_write(uint8_t character);
    void queue_terminal_input(const uint8_t *data, size_t size);
    bool input_empty() const;
    bool run_until_output(const std::string &text, size_t maximum);
    bool run_until_prompt(size_t maximum, bool boot_only);
    bool run_command(const std::string &command, std::string &error);
    bool stage_file(const std::filesystem::path &source, bool text, std::string &error);
    bool output_has_prompt(bool boot_only) const;
    bool create_boot_marker(std::filesystem::path &program, std::string &error);
    bool configure_boot_autorun(const std::filesystem::path &drive_a,
                                std::string &error);
    bool start_cpm(const std::filesystem::path &drive_a,
                   std::string &error);

    FullCpmOptions options_;
    z80_t cpu_{};
    disk_controller_t disk_controller_{};
    std::filesystem::path session_root_;
    std::string program_name_;
    std::vector<uint8_t> drive_b_image_;
    mutable std::mutex input_mutex_;
    std::deque<uint8_t> input_;
    std::string output_;
    std::function<void(uint8_t)> output_callback_;
    bool booted_ = false;
    std::atomic<bool> target_active_{false};
    bool input_requested_ = false;
    unsigned empty_input_polls_ = 0;
    unsigned input_release_delay_ = 0;
    uint16_t warm_boot_address_ = 0;
    IoAdapterLoader io_adapter_;
    TargetTerminal target_terminal_;
};
