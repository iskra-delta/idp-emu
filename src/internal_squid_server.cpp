#include "internal_squid_server.hpp"

extern "C" {
#include "retrovault_core.h"
}

#include <curl/curl.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::once_flag curl_initialization_once;
bool curl_available = false;

void initialize_curl()
{
    curl_available = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}
}

struct internal_squid_server::implementation
{
    struct work_item
    {
        std::uint64_t session = 0;
        std::vector<std::uint8_t> payload;
    };

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<work_item> requests;
    std::deque<work_item> responses;
    std::thread worker;
    std::atomic<bool> stopping{false};
    bool worker_busy = false;
    std::string status = "Ready";
    std::string base_url;

    implementation()
    {
        std::call_once(curl_initialization_once, initialize_curl);
        const char *configured = std::getenv("RETRO_VAULT_API_URL");
        base_url = configured != nullptr && configured[0] != '\0'
            ? configured : "https://retro-vault.org";
        while (base_url.size() > 8 && base_url.back() == '/')
            base_url.pop_back();
        if (!(base_url.starts_with("https://") || base_url.starts_with("http://")))
        {
            base_url = "https://retro-vault.org";
            status = "Invalid RETRO_VAULT_API_URL; using default";
        }
        worker = std::thread([this] { run(); });
    }

    ~implementation()
    {
        stopping = true;
        wake.notify_all();
        if (worker.joinable())
            worker.join();
    }

    static std::size_t receive_http_data(char *data, std::size_t element_size,
                                         std::size_t element_count,
                                         void *user_data)
    {
        auto *buffer = static_cast<retro_vault_buffer *>(user_data);
        if (element_size != 0 && element_count > SIZE_MAX / element_size)
            return 0;
        const std::size_t size = element_size * element_count;
        return retro_vault_buffer_append(buffer, data, size) == 0 ? size : 0;
    }

    static int transfer_progress(void *user_data, curl_off_t, curl_off_t,
                                 curl_off_t, curl_off_t)
    {
        const auto *self = static_cast<const implementation *>(user_data);
        return self->stopping ? 1 : 0;
    }

    static int http_get(void *user_data, const char *path,
                        std::size_t size_limit,
                        retro_vault_buffer *response, long *status_code)
    {
        auto *self = static_cast<implementation *>(user_data);
        if (!curl_available || self == nullptr || path == nullptr ||
            path[0] != '/' || response == nullptr || status_code == nullptr)
            return retro_vault_http_error;

        retro_vault_buffer_free(response);
        response->limit = size_limit;
        *status_code = 0;

        CURL *curl = curl_easy_init();
        if (curl == nullptr)
            return retro_vault_http_error;
        const std::string url = self->base_url + path;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_http_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "idp-emu-internal-squid/1");
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, self);
#ifdef CURLSSLOPT_NATIVE_CA
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif

        const CURLcode result = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);
        curl_easy_cleanup(curl);
        if (result != CURLE_OK)
            return response->overflowed
                ? retro_vault_http_too_large : retro_vault_http_error;
        return retro_vault_http_ok;
    }

    void run()
    {
        retro_vault_http_client http{http_get, this};
        retro_vault_context context;
        init_retro_vault_context(&context, &http);

        for (;;)
        {
            work_item request;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !requests.empty(); });
                if (stopping)
                    break;
                request = std::move(requests.front());
                requests.pop_front();
                worker_busy = true;
                status = "Contacting Retro Vault";
            }

            std::array<std::uint8_t, squid_link_server::maximum_packet_size> output{};
            const int size = handle_retro_vault_request(
                &context, request.payload.data(), request.payload.size(),
                output.data(), output.size());

            {
                std::lock_guard lock(mutex);
                worker_busy = false;
                if (size > 0)
                {
                    request.payload.assign(output.begin(), output.begin() + size);
                    responses.push_back(std::move(request));
                    status = "Ready";
                }
                else
                {
                    status = "Retro Vault request failed";
                }
            }
        }

        free_retro_vault_context(&context);
    }

    void enqueue(squid_link_server::request request)
    {
        {
            std::lock_guard lock(mutex);
            requests.push_back({request.session, std::move(request.payload)});
        }
        wake.notify_one();
    }

    bool take_response(work_item &response)
    {
        std::lock_guard lock(mutex);
        if (responses.empty())
            return false;
        response = std::move(responses.front());
        responses.pop_front();
        return true;
    }

    bool busy() const
    {
        std::lock_guard lock(mutex);
        return worker_busy || !requests.empty();
    }

    std::string status_text() const
    {
        std::lock_guard lock(mutex);
        return status;
    }
};

internal_squid_server::internal_squid_server()
    : implementation_(std::make_unique<implementation>())
{
}

internal_squid_server::~internal_squid_server() = default;

void internal_squid_server::reset_link()
{
    link_.reset();
}

void internal_squid_server::receive_serial_byte(std::uint8_t byte)
{
    link_.receive_serial_byte(byte);
}

void internal_squid_server::service(
    std::deque<std::uint8_t> &serial_receive_queue)
{
    link_.service();

    squid_link_server::request request;
    while (link_.take_request(request))
        implementation_->enqueue(std::move(request));

    implementation::work_item response;
    while (implementation_->take_response(response))
        (void)link_.submit_response(response.session,
                                    response.payload.data(),
                                    response.payload.size());

    link_.service();
    std::uint8_t byte = 0;
    while (link_.take_serial_byte(byte))
        serial_receive_queue.push_back(byte);
}

bool internal_squid_server::link_up() const
{
    return link_.link_up();
}

bool internal_squid_server::busy() const
{
    return implementation_->busy();
}

std::string internal_squid_server::status_text() const
{
    if (!link_.link_up())
        return "Internal Squid (waiting for PAKET)";
    return "Internal Squid: " + implementation_->status_text();
}

std::size_t internal_squid_server::pending_serial_bytes() const
{
    return link_.pending_serial_bytes();
}
