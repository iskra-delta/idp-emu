#include "partner_crt.hpp"
#include "partner_gdp.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr std::uint64_t boot_limit = 400000000ULL;
// Match the current INVADERS.COM payload.  A short fixture cannot expose
// transport failures that occur only after many Squid response pages.
constexpr std::size_t fixture_size = 25396U;

const char catalog[] = R"json([
  {
    "id":"fixture-p","name":"Fixture Partner P","vendor":"Regression",
    "platformId":"idp","platformName":"Iskra Delta Partner",
    "modelId":"p","modelName":"Partner P","releaseYear":2026,
    "version":"2.0","rating":"Great","description":"Wire v2 P fixture",
    "downloads":[{"id":"complete","label":"Program file","format":"COM",
      "files":[{"fileName":"FIXTUREP.COM","sizeBytes":25396}]}]
  },
  {
    "id":"invaders","name":"Napad iz Vesolja","vendor":"Regression",
    "platformId":"idp","platformName":"Iskra Delta Partner",
    "modelId":"gdp","modelName":"Partner G","releaseYear":2026,
    "version":"2.0","rating":"Great","description":"Wire v2 G fixture",
    "downloads":[{"id":"complete","label":"Program file","format":"COM",
      "files":[{"fileName":"INVADERS.COM","sizeBytes":25396}]}]
  }
])json";

class retro_vault_fixture
{
public:
    retro_vault_fixture()
    {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0)
            return;
        int enabled = 1;
        (void)::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR,
                           &enabled, sizeof(enabled));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listener_, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) != 0 || ::listen(listener_, 8) != 0)
        {
            ::close(listener_);
            listener_ = -1;
            return;
        }
        socklen_t size = sizeof(address);
        if (::getsockname(listener_, reinterpret_cast<sockaddr *>(&address),
                          &size) != 0)
        {
            ::close(listener_);
            listener_ = -1;
            return;
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~retro_vault_fixture()
    {
        stopping_ = true;
        if (listener_ >= 0)
        {
            (void)::shutdown(listener_, SHUT_RDWR);
            ::close(listener_);
            listener_ = -1;
        }
        if (worker_.joinable())
            worker_.join();
    }

    bool ready() const { return listener_ >= 0 && port_ != 0; }
    std::uint16_t port() const { return port_; }
    unsigned int catalog_requests() const { return catalog_requests_; }
    unsigned int download_requests() const { return download_requests_; }

private:
    static std::uint8_t fixture_byte(std::size_t index)
    {
        return static_cast<std::uint8_t>((index * 37U) ^ 0x5aU);
    }

    static bool send_all(int fd, const void *data, std::size_t size)
    {
        const auto *bytes = static_cast<const char *>(data);
        while (size != 0)
        {
            const ssize_t sent = ::send(fd, bytes, size, MSG_NOSIGNAL);
            if (sent <= 0)
                return false;
            bytes += sent;
            size -= static_cast<std::size_t>(sent);
        }
        return true;
    }

    static void reply(int fd, std::string_view type,
                      const void *body, std::size_t size,
                      std::string_view status = "200 OK")
    {
        const std::string header =
            "HTTP/1.1 " + std::string(status) + "\r\nContent-Type: " +
            std::string(type) + "\r\nContent-Length: " +
            std::to_string(size) +
            "\r\nConnection: close\r\n\r\n";
        if (send_all(fd, header.data(), header.size()) && size != 0)
            (void)send_all(fd, body, size);
    }

    void handle(int fd)
    {
        std::string request;
        char buffer[1024];
        while (request.find("\r\n\r\n") == std::string::npos &&
               request.size() < 8192)
        {
            const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
                return;
            request.append(buffer, static_cast<std::size_t>(received));
        }
        const std::size_t first_space = request.find(' ');
        const std::size_t second_space = request.find(' ', first_space + 1);
        if (first_space == std::string::npos ||
            second_space == std::string::npos)
            return;
        const std::string path = request.substr(
            first_space + 1, second_space - first_space - 1);
        if (path == "/api/v1/catalog/packages")
        {
            // Real Retro Vault responses arrive asynchronously.  Keep the
            // virtual serial link alive while the host request is in flight;
            // an immediate localhost reply does not exercise that state.
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            ++catalog_requests_;
            reply(fd, "application/json", catalog, sizeof(catalog) - 1);
            return;
        }
        if (path == "/api/v1/catalog/packages/idp/p/fixture-p/downloads/complete" ||
            path == "/api/v1/catalog/packages/idp/gdp/invaders/downloads/complete")
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::array<std::uint8_t, fixture_size> bytes{};
            for (std::size_t index = 0; index < sizeof(bytes); ++index)
                bytes[index] = fixture_byte(index);
            ++download_requests_;
            reply(fd, "application/octet-stream", bytes.data(), bytes.size());
            return;
        }
        static constexpr char missing[] = "missing";
        reply(fd, "text/plain", missing, sizeof(missing) - 1,
              "404 Not Found");
    }

    void serve()
    {
        while (!stopping_)
        {
            const int client = ::accept(listener_, nullptr, nullptr);
            if (client < 0)
            {
                if (!stopping_ && errno == EINTR)
                    continue;
                break;
            }
            handle(client);
            ::close(client);
        }
    }

    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned int> catalog_requests_{0};
    std::atomic<unsigned int> download_requests_{0};
    std::thread worker_;
};

bool machine_contains(const partner_crt &, std::string_view output,
                      std::string_view text)
{
    return output.find(text) != std::string_view::npos;
}

bool machine_contains(const partner_gdp &machine, std::string_view output,
                      std::string_view text)
{
    if (output.find(text) != std::string_view::npos)
        return true;
    const auto &vram = machine.get_avdc().vram;
    for (std::size_t start = 0; start < sizeof(vram); ++start)
    {
        std::size_t offset = 0;
        while (offset < text.size() &&
               vram[(start + offset) & 0x3fffU] ==
                   static_cast<std::uint8_t>(text[offset]))
            ++offset;
        if (offset == text.size())
            return true;
    }
    return false;
}

bool send_machine_key(partner_crt &machine, std::uint8_t key)
{
    machine.key_input(key);
    return true;
}

bool send_machine_key(partner_gdp &machine, std::uint8_t key)
{
    return machine.key_input(key);
}

std::size_t occurrences(std::string_view text, std::string_view pattern)
{
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(pattern, position)) != std::string_view::npos)
    {
        ++count;
        position += pattern.size();
    }
    return count;
}

bool command_prompt(const partner_crt &, std::string_view output)
{
    return occurrences(output, "A>") >= 2U;
}

bool command_prompt(const partner_gdp &machine, std::string_view)
{
    const auto &avdc = machine.get_avdc();
    for (std::size_t start = 0; start < sizeof(avdc.vram); ++start)
    {
        if (avdc.vram[start] == 'A' &&
            avdc.vram[(start + 1U) & 0x3fffU] == '>' &&
            ((start + 2U) & 0x3fffU) == avdc.cursor_addr)
            return true;
    }
    return false;
}

std::size_t machine_occurrences(const partner_crt &, std::string_view output,
                                std::string_view pattern)
{
    return occurrences(output, pattern);
}

std::size_t machine_occurrences(const partner_gdp &machine,
                                std::string_view output,
                                std::string_view pattern)
{
    std::size_t count = occurrences(output, pattern);
    const auto &vram = machine.get_avdc().vram;
    for (std::size_t start = 0; start < sizeof(vram); ++start)
    {
        std::size_t offset = 0;
        while (offset < pattern.size() &&
               vram[(start + offset) & 0x3fffU] ==
                   static_cast<std::uint8_t>(pattern[offset]))
            ++offset;
        if (offset == pattern.size())
            ++count;
    }
    return count;
}

void print_prompt_diagnostic(const partner_crt &machine,
                             std::string_view output)
{
    std::printf(" prompt=%d ready=%d pending=%zu",
                command_prompt(machine, output) ? 1 : 0,
                machine.keyboard_input_ready() ? 1 : 0,
                machine.pending_key_count());
}

void print_prompt_diagnostic(const partner_gdp &machine,
                             std::string_view output)
{
    const auto &avdc = machine.get_avdc();
    std::size_t last_prompt = sizeof(avdc.vram);
    for (std::size_t start = 0; start < sizeof(avdc.vram); ++start)
    {
        if (avdc.vram[start] == 'A' &&
            avdc.vram[(start + 1U) & 0x3fffU] == '>')
            last_prompt = start;
    }
    std::printf(" prompt=%d ready=%d pending=%zu cursor=%04x last-a=%04zx",
                command_prompt(machine, output) ? 1 : 0,
                machine.keyboard_input_ready() ? 1 : 0,
                machine.pending_key_count(),
                static_cast<unsigned int>(avdc.cursor_addr & 0x3fffU),
                last_prompt);
}

bool downloaded_file_matches(const std::string &disk,
                             std::string_view filename)
{
    constexpr std::size_t directory_offset = 32U * 256U;
    constexpr std::size_t directory_entries = 1024U;
    constexpr std::size_t allocation_block_size = 4096U;
    std::ifstream input(disk, std::ios::binary);
    constexpr std::size_t extent_size = 128U * 128U;
    constexpr std::uint8_t extent_mask = 1U;
    struct extent {
        std::size_t logical;
        std::size_t byte_count;
        std::array<std::uint16_t, 8> blocks;
    };
    std::array<extent, 8> extents{};
    std::size_t extent_count = 0;
    std::array<std::uint8_t, 32> entry{};

    if (!input)
        return false;
    for (std::size_t index = 0; index < directory_entries; ++index)
    {
        input.seekg(static_cast<std::streamoff>(
            directory_offset + index * entry.size()));
        input.read(reinterpret_cast<char *>(entry.data()), entry.size());
        if (!input || entry[0] != 1U)
            continue;

        std::string current;
        for (std::size_t position = 1; position < 9; ++position)
            current.push_back(static_cast<char>(entry[position] & 0x7fU));
        while (!current.empty() && current.back() == ' ')
            current.pop_back();
        current.push_back('.');
        for (std::size_t position = 9; position < 12; ++position)
            current.push_back(static_cast<char>(entry[position] & 0x7fU));
        while (!current.empty() && current.back() == ' ')
            current.pop_back();
        if (current != filename || extent_count == extents.size())
            continue;
        extent &current_extent = extents[extent_count++];
        current_extent.logical =
            (entry[12] & static_cast<std::uint8_t>(~extent_mask)) |
            (static_cast<std::size_t>(entry[14] & 0x3fU) << 5U);
        current_extent.byte_count =
            ((entry[12] & extent_mask) * 128U + entry[15]) * 128U;
        for (std::size_t block = 0; block < current_extent.blocks.size(); ++block)
        {
            const std::size_t position = 16U + block * 2U;
            current_extent.blocks[block] =
                static_cast<std::uint16_t>(entry[position]) |
                static_cast<std::uint16_t>(entry[position + 1U] << 8U);
        }
    }
    std::sort(extents.begin(), extents.begin() + extent_count,
              [](const extent &left, const extent &right) {
                  return left.logical < right.logical;
              });
    std::size_t expected_extent = 0;
    std::size_t payload_offset = 0;
    for (std::size_t index = 0; index < extent_count; ++index)
    {
        const extent &current_extent = extents[index];
        if (current_extent.logical != expected_extent)
            return false;
        std::size_t remaining = current_extent.byte_count;
        for (const std::uint16_t block : current_extent.blocks)
        {
            if (remaining == 0U)
                break;
            if (block == 0U)
                return false;
            const std::size_t count = std::min(allocation_block_size, remaining);
            std::array<std::uint8_t, allocation_block_size> payload{};
            input.seekg(static_cast<std::streamoff>(
                directory_offset + block * allocation_block_size));
            input.read(reinterpret_cast<char *>(payload.data()),
                       static_cast<std::streamsize>(count));
            if (!input)
                return false;
            for (std::size_t position = 0;
                 position < count && payload_offset < fixture_size;
                 ++position, ++payload_offset)
            {
                const std::uint8_t wanted = static_cast<std::uint8_t>(
                    (payload_offset * 37U) ^ 0x5aU);
                if (payload[position] != wanted)
                    return false;
            }
            remaining -= count;
        }
        if (remaining != 0U)
            return false;
        expected_extent +=
            (current_extent.byte_count + extent_size - 1U) / extent_size;
    }
    return payload_offset == fixture_size;
}

template<class Machine>
bool run_flow(Machine &machine, const std::string &rom,
              const std::string &disk, std::string_view package)
{
    machine.load_rom(rom);
    machine.load_hdd(disk);
    machine.reset();

    const std::string download = "paket " + std::string(package) +
        " A:/1/\r";
    std::size_t download_position = 0;
    bool initial_prompt = false;
    bool transfer_started = false;
    bool transfer_succeeded = false;
    std::uint64_t transfer_start_tick = 0U;
    std::uint64_t transfer_success_tick = 0U;
    bool post_transfer_key_sent = false;
    bool post_transfer_key_consumed = false;
    std::uint64_t post_transfer_key_tick = 0U;
    std::uint32_t post_transfer_ack_count = 0U;
    bool prompt_reported = false;
    std::size_t nmi_entries = 0U;
    std::uint16_t previous_pc = machine.get_current_pc();
    std::uint8_t initial_i = 0U;
    std::vector<std::uint8_t> initial_im2;
    std::uint32_t initial_ack_count = 0U;

    while (machine.get_tick_count() < boot_limit)
    {
        machine.tick();
        const std::uint16_t current_pc = machine.get_current_pc();
        if (initial_prompt && current_pc == 0x0066U && previous_pc != 0x0066U)
        {
            ++nmi_entries;
            std::printf(
                "test_partner_paket_flow: %.*s NMI entry %zu at %llu "
                "rtc=%02x/%02x rollover=%02x\n",
                static_cast<int>(package.size()), package.data(), nmi_entries,
                static_cast<unsigned long long>(machine.get_tick_count()),
                machine.get_rtc().interrupt_status,
                machine.get_rtc().interrupt_control,
                machine.get_rtc().rollover_status);
        }
        previous_pc = current_pc;
        if (transfer_succeeded && nmi_entries != 0U)
            break;
        if ((machine.get_tick_count() % 50000000U) == 0U)
        {
            std::printf("test_partner_paket_flow: %.*s ticks=%llu pc=%04x\n",
                        static_cast<int>(package.size()), package.data(),
                        static_cast<unsigned long long>(machine.get_tick_count()),
                        static_cast<unsigned int>(machine.get_current_pc()));
            std::fflush(stdout);
        }
        if (!transfer_succeeded && initial_prompt &&
            download_position < download.size() &&
            machine.keyboard_input_ready() &&
            machine.pending_key_count() == 0U)
            machine.key_input(download[download_position++]);
        if (post_transfer_key_sent && machine.pending_key_count() == 0U)
            post_transfer_key_consumed = true;
        if (post_transfer_key_sent && !post_transfer_key_consumed &&
            machine.get_tick_count() - post_transfer_key_tick >= 100000U)
            break;
        if ((machine.get_tick_count() % 1000000U) != 0U)
            continue;
        const std::string output = machine.dump_raw_serial_text();
        initial_prompt = initial_prompt ||
            machine_contains(machine, output, "A>");
        if (initial_prompt && !prompt_reported)
        {
            const auto cpu = machine.capture_debug_cpu_state();
            initial_i = cpu.i;
            initial_im2 = machine.read_debug_memory(
                static_cast<std::uint32_t>(initial_i) << 8U, 256U);
            initial_ack_count = machine.dbg_im2_ack_count;
            std::printf("test_partner_paket_flow: %.*s booted\n",
                        static_cast<int>(package.size()), package.data());
            std::fflush(stdout);
            prompt_reported = true;
        }
        if (!transfer_started &&
            machine_contains(machine, output, "PRENOS PAKETA"))
        {
            transfer_started = true;
            transfer_start_tick = machine.get_tick_count();
        }
        if (machine_occurrences(machine, output, "PRENOS PAKETA") > 1U)
        {
            std::printf(
                "test_partner_paket_flow: %.*s duplicate transfer at %llu "
                "pc=%04x nmis=%zu\n",
                static_cast<int>(package.size()), package.data(),
                static_cast<unsigned long long>(machine.get_tick_count()),
                static_cast<unsigned int>(current_pc), nmi_entries);
            return false;
        }
        if (transfer_started &&
            (machine.get_tick_count() % 50000000U) == 0U)
        {
            std::printf("test_partner_paket_flow: %.*s input",
                        static_cast<int>(package.size()), package.data());
            print_prompt_diagnostic(machine, output);
            std::printf(" download=%zu/%zu started=%d\n",
                        download_position, download.size(),
                        transfer_started ? 1 : 0);
            std::fflush(stdout);
        }
        if (!transfer_succeeded &&
            machine_contains(machine, output, "Prenos je uspesno koncan."))
        {
            transfer_succeeded = true;
            transfer_success_tick = machine.get_tick_count();
            const auto serial = machine.get_sio_port_status(
                partner::sio_port_id::sio1_b);
            const double seconds = static_cast<double>(
                transfer_success_tick - transfer_start_tick) / 4000000.0;
            std::printf(
                "test_partner_paket_flow: %.*s transfer %.3f s, "
                "serial tx=%llu rx=%llu, payload %.1f B/s\n",
                static_cast<int>(package.size()), package.data(), seconds,
                static_cast<unsigned long long>(serial.tx_bytes),
                static_cast<unsigned long long>(serial.rx_bytes),
                static_cast<double>(fixture_size) / seconds);
            std::fflush(stdout);
        }
        if (transfer_succeeded && !post_transfer_key_sent &&
            command_prompt(machine, output) &&
            machine.keyboard_input_ready() &&
            machine.pending_key_count() == 0U)
        {
            post_transfer_key_sent = send_machine_key(machine, '\r');
            if (post_transfer_key_sent)
            {
                post_transfer_key_tick = machine.get_tick_count();
                post_transfer_ack_count = machine.dbg_im2_ack_count;
            }
        }
        if (post_transfer_key_consumed)
            return initial_prompt && transfer_started &&
                download_position == download.size() &&
                nmi_entries == 0U &&
                machine_occurrences(machine, output, "PRENOS PAKETA") == 1U &&
                machine_contains(machine, output, "preostalo: 0 bajtov") &&
                !machine_contains(machine, output, "Napaka:");
        if (transfer_started &&
            machine_contains(machine, output, "Prenos ni uspel:"))
            break;
        if (machine_contains(machine, output,
                             "Napaka: serijska povezava ni uspela"))
            break;
    }
    std::printf("test_partner_paket_flow: %.*s output=%s\n",
                static_cast<int>(package.size()), package.data(),
                machine.dump_raw_serial_text().c_str());
    std::printf(
        "test_partner_paket_flow: %.*s download=%zu/%zu started=%d "
        "ready=%d pending=%zu\n",
        static_cast<int>(package.size()), package.data(), download_position,
        download.size(), transfer_started ? 1 : 0,
        machine.keyboard_input_ready() ? 1 : 0,
        machine.pending_key_count());
    const auto serial = machine.get_sio_port_status(
        partner::sio_port_id::sio1_b);
    const auto &channel = machine.get_sio().chn[Z80SIO_CHANNEL_B];
    const auto &keyboard_channel = machine.get_sio().chn[Z80SIO_CHANNEL_A];
    const auto cpu = machine.capture_debug_cpu_state();
    std::printf(
        "test_partner_paket_flow: serial tx=%llu rx=%llu pending=%zu "
        "rts=%d cts=%d connected=%d overrun=%d wr1=%02x wr3=%02x "
        "wr5=%02x detail=%s\n",
        static_cast<unsigned long long>(serial.tx_bytes),
        static_cast<unsigned long long>(serial.rx_bytes),
        serial.pending_rx_bytes, serial.rts ? 1 : 0, serial.cts ? 1 : 0,
        serial.connected ? 1 : 0, channel.rx_overrun ? 1 : 0,
        channel.wr[1], channel.wr[3], channel.wr[5],
        serial.detail.c_str());
    std::printf(
        "test_partner_paket_flow: keyboard wr1=%02x wr3=%02x ready=%d "
        "fifo=%u vector=%02x irq=%02x/%02x/%02x deferred=%02x m1=%d "
        "cpu-i=%02x im=%u iff=%d halt=%d pc=%04x\n",
        keyboard_channel.wr[1], keyboard_channel.wr[3],
        keyboard_channel.rx_ready ? 1 : 0, keyboard_channel.rx_fifo_count,
        keyboard_channel.int_vector,
        keyboard_channel.int_source_state[Z80SIO_INT_RECEIVE],
        keyboard_channel.int_source_state[Z80SIO_INT_TRANSMIT],
        keyboard_channel.int_source_state[Z80SIO_INT_EXTERNAL],
        keyboard_channel.int_deferred, keyboard_channel.m1_active ? 1 : 0,
        cpu.i, cpu.im, cpu.iff1 ? 1 : 0, cpu.halted ? 1 : 0, cpu.pc);
    const auto final_im2 = machine.read_debug_memory(
        static_cast<std::uint32_t>(cpu.i) << 8U, 256U);
    if (initial_i == cpu.i && initial_im2.size() == final_im2.size())
    {
        std::printf("test_partner_paket_flow: changed IM2 bytes");
        for (std::size_t index = 0; index < final_im2.size(); ++index)
            if (initial_im2[index] != final_im2[index])
                std::printf(" %02zx:%02x>%02x", index,
                            initial_im2[index], final_im2[index]);
        std::printf("\n");
    }
    const auto common = machine.read_debug_memory(0xc180U, 32U);
    std::printf("test_partner_paket_flow: common c180");
    for (const std::uint8_t byte : common)
        std::printf(" %02x", byte);
    std::printf("\n");
    std::printf("test_partner_paket_flow: IM2 acks %u>%u post=%u last",
                initial_ack_count, machine.dbg_im2_ack_count,
                post_transfer_ack_count);
    const std::uint32_t first_ack = machine.dbg_im2_ack_count > 8U
        ? machine.dbg_im2_ack_count - 8U : 0U;
    for (std::uint32_t ack = first_ack;
         ack < machine.dbg_im2_ack_count; ++ack)
    {
        const std::size_t index = ack & 0x7U;
        std::printf(" %02x@%04x", machine.dbg_im2_ack_vectors[index],
                    machine.dbg_im2_ack_pcs[index]);
    }
    std::printf("\n");
    return false;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    retro_vault_fixture fixture;
    if (!fixture.ready())
    {
        std::puts("test_partner_paket_flow: FAIL fixture server");
        return 1;
    }
    const std::string url = "http://127.0.0.1:" +
        std::to_string(fixture.port());
    if (::setenv("RETRO_VAULT_API_URL", url.c_str(), 1) != 0)
    {
        std::puts("test_partner_paket_flow: FAIL environment");
        return 1;
    }
    // Make the minute edge deterministic and close optional JJ12 so this test
    // continues to prove that PAKET masks RTC NMI during its transfer.
    if (::setenv("IDP_FIXED_RTC", "1700000030", 1) != 0)
    {
        std::puts("test_partner_paket_flow: FAIL fixed RTC environment");
        return 1;
    }

    const fs::path root = IDP_SOURCE_ROOT;
    const char *model = std::getenv("IDP_TEST_PAKET_MODEL");
    const bool run_p = model == nullptr || std::strcmp(model, "g") != 0;
    const bool run_g = model == nullptr || std::strcmp(model, "p") != 0;
    const char *p_override = std::getenv("IDP_TEST_PAKET_P_HDD");
    const char *g_override = std::getenv("IDP_TEST_PAKET_G_HDD");
    const std::string suffix = std::to_string(::getpid());
    const fs::path p_work = root /
        ("tests/dump/paket-flow-p-" + suffix + ".img");
    const fs::path g_work = root /
        ("tests/dump/paket-flow-g-" + suffix + ".img");
    const std::string p_disk = p_override != nullptr ? p_override :
        p_work.string();
    const std::string g_disk = g_override != nullptr ? g_override :
        g_work.string();

    const fs::path p_nvram = root /
        ("tests/dump/paket-flow-p-" + suffix + ".nvram");
    const fs::path g_nvram = root /
        ("tests/dump/paket-flow-g-" + suffix + ".nvram");
    std::error_code error;
    if (run_p) {
        if (p_override == nullptr) {
            fs::copy_file(root / "disks/hdd-partner-p-system.img", p_work,
                          fs::copy_options::overwrite_existing, error);
            if (error)
                return 1;
        }
        fs::copy_file(root / "partner_cmos.bin", p_nvram,
                      fs::copy_options::overwrite_existing, error);
        if (error)
            return 1;
    }
    if (run_g) {
        if (g_override == nullptr) {
            fs::copy_file(root / "disks/hdd-partner-g-system.img", g_work,
                          fs::copy_options::overwrite_existing, error);
            if (error)
                return 1;
        }
        fs::copy_file(root / "partner_cmos.bin", g_nvram,
                      fs::copy_options::overwrite_existing, error);
        if (error)
            return 1;
    }

    bool p_ok = false;
    bool g_ok = false;
    if (run_p) {
        partner_crt machine(terminal_profile::vt52, p_nvram.string());
        machine.set_rtc_nmi_enabled(true);
        p_ok = run_flow(
            machine, (root / "roms/partner_crt.rom").string(),
            p_disk, "fixture-p");
    }
    if (run_p)
        p_ok = p_ok && downloaded_file_matches(p_disk, "FIXTUREP.COM");
    if (run_g) {
        partner_gdp machine(g_nvram.string());
        machine.set_rtc_nmi_enabled(true);
        g_ok = run_flow(
            machine, (root / "roms/partner_gdp.rom").string(),
            g_disk, "invaders");
    }
    if (run_g)
        g_ok = g_ok && downloaded_file_matches(g_disk, "INVADERS.COM");
    if (run_p) {
        fs::remove(p_nvram, error);
        if (p_override == nullptr)
            fs::remove(p_work, error);
    }
    if (run_g) {
        fs::remove(g_nvram, error);
        if (g_override == nullptr)
            fs::remove(g_work, error);
    }

    const unsigned int expected_requests =
        static_cast<unsigned int>(run_p) + static_cast<unsigned int>(run_g);
    const bool http_ok = fixture.catalog_requests() == expected_requests &&
        fixture.download_requests() == expected_requests;
    if ((run_p && !p_ok) || (run_g && !g_ok) || !http_ok)
    {
        std::printf(
            "test_partner_paket_flow: FAIL p=%d g=%d catalog=%u download=%u\n",
            p_ok ? 1 : 0, g_ok ? 1 : 0, fixture.catalog_requests(),
            fixture.download_requests());
        return 1;
    }
    std::printf("test_partner_paket_flow: PASS long downloads on %s\n",
                run_p ? (run_g ? "P and G" : "P") : "G");
    return 0;
}
