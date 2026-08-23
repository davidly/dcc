#include "dcc_host_full_cpm.hpp"
#include "dcc_host_fixture_publish.hpp"
#include "directory_disk.hpp"

extern "C"
{
#include "memory.h"
#include "universal_88dcdd.h"
#include "interrupt_controller.h"
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
constexpr size_t kBootInstructions = 20000000;
constexpr size_t kCommandInstructions = 20000000;
constexpr size_t kLaunchInstructions = 5000000;
constexpr unsigned kInputWaitPolls = 64;
constexpr unsigned kInputReleaseInstructions = 4096;
constexpr size_t kPhysicalSectorSize = 137;
constexpr size_t kSectorsPerTrack = 32;
constexpr size_t kSectorDataOffset = 3;
constexpr size_t kSectorChecksumOffset = 132;
constexpr size_t kLogicalSectorSize = 128;
constexpr char kBootProgramName[] = "DBGBOOT.COM";
constexpr char kBootCommand[] = "A:DBGBOOT";
constexpr char kBootMarker[] = "ALTDBG-CPM-READY-8E0B7C6A";

uint32_t monotonic_ms()
{
    auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

std::string upper(std::string value)
{
    for (char &character : value)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    return value;
}

bool cpm_name_ok(const std::string &name)
{
    size_t dot = name.find('.');
    size_t base = dot == std::string::npos ? name.size() : dot;
    size_t extension = dot == std::string::npos ? 0 : name.size() - dot - 1;

    if (base == 0 || base > 8 || extension > 3 ||
        (dot != std::string::npos && name.find('.', dot + 1) != std::string::npos))
        return false;
    for (char character : name)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '.')
            return false;
    return true;
}

fs::path make_session_root()
{
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path() / ("dcc-debug-host-" + std::to_string(stamp));
    fs::create_directories(root / "DEBUG");
    return root;
}

bool copy_file(const fs::path &source, const fs::path &destination, std::string &error)
{
    std::error_code code;
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, code);
    if (!code)
        return true;
    error = "cannot copy " + source.string() + ": " + code.message();
    return false;
}

bool write_file(const fs::path &destination, const std::vector<uint8_t> &data,
                std::string &error)
{
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (output && output.write(reinterpret_cast<const char *>(data.data()),
                               static_cast<std::streamsize>(data.size())).good())
        return true;
    error = "cannot write " + destination.string();
    return false;
}

bool append_directory_fixtures(const fs::path &program,
                               std::vector<DebugFixture> &files,
                               std::string &error)
{
    fs::path directory = program.parent_path() / "fixtures";
    std::error_code code;
    bool exists = fs::exists(directory, code);
    if (code)
    {
        error = "cannot inspect debug fixtures directory: " + directory.string();
        return false;
    }
    if (!exists)
        return true;
    if (code || !fs::is_directory(directory, code) || code)
    {
        error = "debug fixtures path is not a directory: " + directory.string();
        return false;
    }

    std::vector<fs::path> paths;
    for (fs::directory_iterator iterator(directory, code), end;
         !code && iterator != end; iterator.increment(code))
        if (iterator->is_regular_file(code) && !code &&
            iterator->path().filename().string().front() != '.')
            paths.push_back(iterator->path());
    if (code)
    {
        error = "cannot read debug fixtures directory: " + directory.string();
        return false;
    }
    std::sort(paths.begin(), paths.end());
    for (const fs::path &path : paths)
        files.push_back({path, false});
    return true;
}

size_t sector_offset(size_t track, size_t sector)
{
    return (track * kSectorsPerTrack + sector - 1) * kPhysicalSectorSize;
}

void update_sector_checksum(std::vector<uint8_t> &image, size_t physical)
{
    unsigned checksum = 0;
    for (size_t index = 0; index < kLogicalSectorSize; ++index)
        checksum += image[physical + kSectorDataOffset + index];
    image[physical + kSectorChecksumOffset] = static_cast<uint8_t>(checksum);
}
}

FullCpmHost *FullCpmHost::active_ = nullptr;

FullCpmHost::FullCpmHost(FullCpmOptions options) : options_(std::move(options))
{
    active_ = this;
}

FullCpmHost::~FullCpmHost()
{
    target_terminal_.stop();
    io_adapter_.close();
    host_disk_close();
    std::error_code code;
    if (!session_root_.empty())
        fs::remove_all(session_root_, code);
    if (active_ == this)
        active_ = nullptr;
}

bool FullCpmHost::initialize(std::string &error)
{
    if (options_.terminal_endpoint_file.empty())
        return true;
    return target_terminal_.connect(
        options_.terminal_endpoint_file,
        [this](const uint8_t *data, size_t size) { queue_terminal_input(data, size); },
        error);
}

uint8_t FullCpmHost::terminal_read_callback()
{
    return active_ ? active_->terminal_read() : 0;
}

void FullCpmHost::terminal_write_callback(uint8_t character)
{
    if (active_)
        active_->terminal_write(character);
}

uint8_t FullCpmHost::sense_switches_callback()
{
    return 0xff;
}

uint8_t FullCpmHost::io_port_in_callback(uint8_t port)
{
    return active_ ? active_->io_adapter_.input(port) : 0;
}

void FullCpmHost::io_port_out_callback(uint8_t port, uint8_t data)
{
    if (active_)
        active_->io_adapter_.output(port, data);
}

int FullCpmHost::register_interrupt_callback(
    void *context, uint8_t data_bus,
    dcc_debug_io_interrupt_poll_fn poll, void *poll_context,
    dcc_debug_io_interrupt_id_t *interrupt_id)
{
    (void)context;
    interrupt_provider_config_t config{};
    config.data_bus = data_bus;
    config.poll = poll;
    config.context = poll_context;
    return interrupt_controller_register(&config, interrupt_id) ? 1 : 0;
}

void FullCpmHost::raise_interrupt_callback(
    void *context, dcc_debug_io_interrupt_id_t interrupt_id)
{
    (void)context;
    interrupt_controller_raise(interrupt_id);
}

void FullCpmHost::clear_interrupt_callback(
    void *context, dcc_debug_io_interrupt_id_t interrupt_id)
{
    (void)context;
    interrupt_controller_clear(interrupt_id);
}

uint8_t FullCpmHost::terminal_read()
{
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (input_.empty())
    {
        std::array<uint8_t, 16> pending{};
        size_t count = io_adapter_.terminal_poll(
            pending.data(), pending.size(), monotonic_ms());
        input_.insert(input_.end(), pending.begin(), pending.begin() + count);
    }
    if (!input_.empty())
    {
        if (target_active_ && input_release_delay_ != 0)
            return 0;
        uint8_t character = input_.front();
        input_.pop_front();
        if (target_active_)
            input_release_delay_ = kInputReleaseInstructions;
        empty_input_polls_ = 0;
        input_requested_ = false;
        return character;
    }
    if (target_active_ && ++empty_input_polls_ >= kInputWaitPolls)
        input_requested_ = true;
    return 0;
}

void FullCpmHost::terminal_write(uint8_t character)
{
    character &= 0x7f;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        empty_input_polls_ = 0;
        input_requested_ = false;
        if (target_active_)
            input_release_delay_ = kInputReleaseInstructions;
    }
    if (output_.size() < 1024 * 1024)
        output_.push_back(static_cast<char>(character));
    if (target_active_ && !target_terminal_.write(character) && output_callback_)
        output_callback_(character);
}

void FullCpmHost::queue_input(const std::string &text, bool add_return)
{
    std::lock_guard<std::mutex> lock(input_mutex_);
    empty_input_polls_ = 0;
    input_requested_ = false;
    input_release_delay_ = 0;
    for (char character : text)
        input_.push_back(character == '\n' ? '\r' : static_cast<uint8_t>(character));
    if (add_return && (text.empty() || (text.back() != '\r' && text.back() != '\n')))
        input_.push_back('\r');
}

void FullCpmHost::queue_terminal_input(const uint8_t *data, size_t size)
{
    if (!target_active_)
        return;
    std::lock_guard<std::mutex> lock(input_mutex_);
    empty_input_polls_ = 0;
    input_requested_ = false;
    input_release_delay_ = 0;
    for (size_t index = 0; index < size; ++index)
    {
        std::array<uint8_t, 16> translated{};
        size_t count = io_adapter_.terminal_input(
            data + index, 1, translated.data(), translated.size(), monotonic_ms());
        input_.insert(input_.end(), translated.begin(), translated.begin() + count);
    }
}

void FullCpmHost::clear_input_request()
{
    std::lock_guard<std::mutex> lock(input_mutex_);
    input_requested_ = false;
    empty_input_polls_ = 0;
}

bool FullCpmHost::input_requested() const
{
    std::lock_guard<std::mutex> lock(input_mutex_);
    return input_requested_;
}

bool FullCpmHost::input_empty() const
{
    std::lock_guard<std::mutex> lock(input_mutex_);
    return input_.empty();
}

void FullCpmHost::step()
{
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        if (input_release_delay_ != 0)
            --input_release_delay_;
    }
    z80_cycle(&cpu_);
    interrupt_controller_service(&cpu_);
}

bool FullCpmHost::output_has_prompt(bool boot_only) const
{
    size_t position = output_.size();
    while (position > 0 && (output_[position - 1] == '\r' || output_[position - 1] == '\n' ||
                            output_[position - 1] == ' '))
        --position;
    if (position < 2 || output_[position - 1] != '>')
        return false;
    if (position > 2 && output_[position - 3] != '\r' && output_[position - 3] != '\n')
        return false;
    char drive = static_cast<char>(std::toupper(static_cast<unsigned char>(output_[position - 2])));
    return boot_only ? drive == 'A' : drive >= 'A' && drive <= 'P';
}

bool FullCpmHost::run_until_prompt(size_t maximum, bool boot_only)
{
    size_t executed = 0;
    while (executed < maximum)
    {
        size_t count = std::min<size_t>(4000, maximum - executed);
        z80_execute_instructions(&cpu_, static_cast<uint16_t>(count));
        interrupt_controller_service(&cpu_);
        executed += count;
        if (input_empty() && output_has_prompt(boot_only))
            return true;
    }
    return input_empty() && output_has_prompt(boot_only);
}

bool FullCpmHost::run_until_output(const std::string &text, size_t maximum)
{
    size_t executed = 0;
    while (executed < maximum)
    {
        size_t count = std::min<size_t>(4000, maximum - executed);
        z80_execute_instructions(&cpu_, static_cast<uint16_t>(count));
        interrupt_controller_service(&cpu_);
        executed += count;
        if (output_.find(text) != std::string::npos)
            return true;
    }
    return output_.find(text) != std::string::npos;
}

bool FullCpmHost::create_boot_marker(fs::path &program, std::string &error)
{
    constexpr uint16_t message_address = 0x010b;
    const std::array<uint8_t, 11> code = {
        0x11, static_cast<uint8_t>(message_address), static_cast<uint8_t>(message_address >> 8),
        0x0e, 0x09,
        0xcd, 0x05, 0x00,
        0xc3, 0x00, 0x00};

    program = session_root_ / kBootProgramName;
    std::ofstream output(program, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "cannot create CP/M boot marker program";
        return false;
    }
    output.write(reinterpret_cast<const char *>(code.data()), code.size());
    output << "\r\n" << kBootMarker << "\r\n$";
    if (!output.good())
    {
        error = "cannot write CP/M boot marker program";
        return false;
    }
    return true;
}

bool FullCpmHost::configure_boot_autorun(const fs::path &drive_a, std::string &error)
{
    std::ifstream input(drive_a, std::ios::binary);
    if (!input)
    {
        error = "cannot open disposable CP/M boot disk";
        return false;
    }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    size_t command_sector = sector_offset(0, 4);
    size_t flag_sector = sector_offset(1, 26);
    if (image.size() < flag_sector + kPhysicalSectorSize)
    {
        error = "CP/M boot disk is too small for Burcon autorun records";
        return false;
    }

    size_t command_data = command_sector + kSectorDataOffset;
    image[command_data + 7] = sizeof(kBootCommand) - 1;
    std::copy_n(kBootCommand, sizeof(kBootCommand) - 1,
                image.begin() + static_cast<std::ptrdiff_t>(command_data + 8));
    image[command_data + 8 + sizeof(kBootCommand) - 1] = 0;
    size_t flag_data = flag_sector + kSectorDataOffset;
    image[flag_data + 89] = static_cast<uint8_t>((image[flag_data + 89] & 0xfc) | 0x01);
    update_sector_checksum(image, command_sector);
    update_sector_checksum(image, flag_sector);

    input.close();
    std::ofstream output(drive_a, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char *>(image.data()),
                                 static_cast<std::streamsize>(image.size())).good())
    {
        error = "cannot configure CP/M boot autorun marker";
        return false;
    }
    return true;
}

bool FullCpmHost::start_cpm(const fs::path &boot_drive_a, std::string &error)
{
    fs::path drive_a = session_root_ / "drive-a.dsk";
    fs::path drive_c = session_root_ / "drive-c.dsk";
    fs::path drive_d = session_root_ / "drive-d.dsk";

    if (!copy_file(boot_drive_a, drive_a, error) ||
        !copy_file(options_.drive_c, drive_c, error) ||
        !copy_file(options_.drive_d, drive_d, error) ||
        !configure_boot_autorun(drive_a, error))
        return false;

    if (!host_disk_init_memory_b(drive_a.string().c_str(), drive_b_image_.data(),
                                 drive_b_image_.size(), drive_c.string().c_str(),
                                 drive_d.string().c_str()))
    {
        error = "cannot open disposable CP/M disk images";
        return false;
    }

    disk_controller_ = host_disk_controller();
    std::memset(memory, 0, 64 * 1024);
    loadDiskLoader(0xff00);
    interrupt_controller_init();
    if (!options_.io_adapter.empty())
    {
        std::string environment_file = options_.environment_file.string();
        std::string files_root = session_root_.string();
        dcc_debug_io_adapter_config_t config{};
        config.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
        config.struct_size = sizeof(config);
        config.environment_file = environment_file.c_str();
        config.files_root = files_root.c_str();
        config.host.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
        config.host.struct_size = sizeof(config.host);
        config.host.context = this;
        config.host.register_interrupt = register_interrupt_callback;
        config.host.raise_interrupt = raise_interrupt_callback;
        config.host.clear_interrupt = clear_interrupt_callback;
        if (!io_adapter_.load(options_.io_adapter, config, error))
            return false;
    }
    z80_reset(&cpu_, terminal_read_callback, terminal_write_callback,
              sense_switches_callback, &disk_controller_,
              io_port_in_callback, io_port_out_callback);
    z80_examine(&cpu_, 0xff00);
    output_.clear();
    if (!run_until_output(kBootMarker, kBootInstructions))
    {
        error = "CP/M 2.2 autorun marker was not seen; output: " +
                output_.substr(output_.size() > 512 ? output_.size() - 512 : 0);
        return false;
    }
    if (!run_until_prompt(kBootInstructions, true))
    {
        error = "CP/M 2.2 did not return to its prompt after the autorun marker";
        return false;
    }
    output_.clear();
    booted_ = true;
    return true;
}

bool FullCpmHost::run_command(const std::string &command, std::string &error)
{
    output_.clear();
    queue_input(command, true);
    if (run_until_prompt(kCommandInstructions, false))
        return true;
    error = "CP/M command did not return to a prompt: " + command +
            "; output: " + output_.substr(output_.size() > 512 ? output_.size() - 512 : 0);
    return false;
}

bool FullCpmHost::stage_file(const fs::path &source, bool text, std::string &error)
{
    std::string name = upper(source.filename().string());
    if (!cpm_name_ok(name))
    {
        error = "file is not CP/M 8.3-compatible: " + source.string();
        return false;
    }
    if (!copy_file(source, session_root_ / "DEBUG" / name, error))
        return false;
    return run_command(std::string("FT ") + (text ? "-G " : "-GB ") +
                       "FILE://DEBUG/" + name, error);
}

bool FullCpmHost::prepare_program(const fs::path &program,
                                  const std::string &arguments,
                                  std::string &error)
{
    if (target_active_)
        return true;
    std::string name = upper(program.filename().string());
    if (!cpm_name_ok(name) || upper(program.extension().string()) != ".COM")
    {
        error = "target must have a CP/M 8.3 .COM filename";
        return false;
    }
    std::vector<DebugFixture> files;
    fs::path boot_marker;
    if (session_root_.empty())
        session_root_ = make_session_root();
    if (!create_boot_marker(boot_marker, error))
        return false;
    files.push_back({program, false});
    files.insert(files.end(), options_.fixtures.begin(), options_.fixtures.end());
    if (!append_directory_fixtures(program, files, error))
        return false;
    fs::path boot_directory = session_root_ / "BOOT";
    fs::path boot_drive = boot_directory / "drive-a-template.dsk";
    std::vector<uint8_t> boot_image;
    fs::create_directories(boot_directory);
    if (!DirectoryDisk::build(options_.drive_a, {{boot_marker, false}}, boot_image, error) ||
        !write_file(boot_drive, boot_image, error))
        return false;
    if (!DirectoryDisk::build(options_.drive_a, files, drive_b_image_, error))
        return false;
    program_name_ = name;
    if (!booted_ && !start_cpm(boot_drive, error))
        return false;
    if (!run_command("B:", error))
        return false;
    if (!run_command("DIR", error))
        return false;
    if (output_.find(upper(program.stem().string())) == std::string::npos)
    {
        error = "target is not visible on directory-backed B:; DIR output: " + output_;
        return false;
    }

    output_.clear();
    std::string command = program.stem().string();
    if (!arguments.empty())
        command += " " + arguments;
    queue_input(command, true);
    for (size_t executed = 0; executed < kLaunchInstructions; ++executed)
    {
        if (cpu_.registers.pc == 0x0100)
        {
            warm_boot_address_ = static_cast<uint16_t>(memory[1] |
                                 (static_cast<uint16_t>(memory[2]) << 8));
            target_active_ = true;
            clear_input_request();
            output_.clear();
            return true;
        }
        step();
    }
    error = "target did not enter the CP/M TPA at 0x0100; output: " +
            output_.substr(output_.size() > 512 ? output_.size() - 512 : 0);
    return false;
}

bool FullCpmHost::save_fixtures(std::string &error)
{
    if (options_.save_fixtures_directory.empty())
        return true;

    std::vector<DirectoryDiskFile> files;
    if (!DirectoryDisk::extract(drive_b_image_, files, error))
        return false;

    const fs::path destination = options_.save_fixtures_directory;
    if (destination.filename().empty())
    {
        error = "fixture save destination must name a directory";
        return false;
    }
    std::error_code code;
    fs::create_directories(destination.parent_path(), code);
    if (code)
    {
        error = "cannot create fixture destination parent: " + code.message();
        return false;
    }
    bool destination_exists = fs::exists(destination, code);
    if (code || (destination_exists && !fs::is_directory(destination, code)) || code)
    {
        error = "fixture save destination is not a directory: " + destination.string();
        return false;
    }

    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path staging = destination.parent_path() /
                       ("." + destination.filename().string() + ".tmp-" + std::to_string(stamp));
    fs::path backup = destination.parent_path() /
                      ("." + destination.filename().string() + ".old-" + std::to_string(stamp));
    if (!fs::create_directory(staging, code) || code)
    {
        error = "cannot create fixture staging directory: " + code.message();
        return false;
    }
    for (const DirectoryDiskFile &file : files)
    {
        if (file.name != program_name_ && !write_file(staging / file.name, file.data, error))
        {
            fs::remove_all(staging, code);
            return false;
        }
    }

    return publish_fixture_directory(staging, destination, backup,
                                     destination_exists, error);
}

bool FullCpmHost::target_exited() const
{
    return target_active_ &&
           (cpu_.registers.pc == 0x0000 || cpu_.registers.pc == warm_boot_address_);
}
