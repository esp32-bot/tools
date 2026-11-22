#include "ota.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "console_commands.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ota {

namespace {

constexpr char TAG[] = "ota";
constexpr size_t NONCE_BYTES = 32;
constexpr size_t HMAC_BYTES = 32;
constexpr uint64_t NONCE_TTL_US = 60ULL * 1000000ULL;  // 60 seconds
constexpr size_t LOG_RING_BYTES = 8192;

// Self-contained FIPS 180-4 SHA-256 & RFC 2104 HMAC-SHA256
struct Sha256 {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buf[64];
    size_t buflen;

    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void init() {
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
        bitlen = 0;
        buflen = 0;
    }

    void transform(const uint8_t data[64]) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf[buflen++] = data[i];
            if (buflen == 64) {
                transform(buf);
                bitlen += 512;
                buflen = 0;
            }
        }
    }

    void final(uint8_t hash[32]) {
        bitlen += buflen * 8;
        buf[buflen++] = 0x80;
        if (buflen > 56) {
            while (buflen < 64) buf[buflen++] = 0x00;
            transform(buf);
            buflen = 0;
        }
        while (buflen < 56) buf[buflen++] = 0x00;
        for (int i = 7; i >= 0; --i) {
            buf[56 + (7 - i)] = static_cast<uint8_t>((bitlen >> (i * 8)) & 0xFF);
        }
        transform(buf);
        for (int i = 0; i < 8; ++i) {
            hash[i * 4] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
            hash[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
            hash[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
            hash[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
        }
    }
};

struct HmacSha256 {
    Sha256 inner;
    uint8_t opad[64];

    void init(const uint8_t key[32]) {
        uint8_t ipad[64];
        for (size_t i = 0; i < 32; ++i) {
            ipad[i] = key[i] ^ 0x36;
            opad[i] = key[i] ^ 0x5c;
        }
        for (size_t i = 32; i < 64; ++i) {
            ipad[i] = 0x36;
            opad[i] = 0x5c;
        }
        inner.init();
        inner.update(ipad, 64);
    }

    void update(const uint8_t* data, size_t len) {
        if (data && len > 0) inner.update(data, len);
    }

    void final(uint8_t out[32]) {
        uint8_t inner_hash[32];
        inner.final(inner_hash);
        Sha256 outer;
        outer.init();
        outer.update(opad, 64);
        outer.update(inner_hash, 32);
        outer.final(out);
    }
};

const char* configured_base_url = "https://esp32-bot.github.io/BalaX/fw";
const char* configured_binary_name = "balax.bin";

std::array<uint8_t, 32> lan_key{};
bool lan_key_set = false;

// Single active challenge nonce (one-time use, expires after 60s)
portMUX_TYPE nonce_lock = portMUX_INITIALIZER_UNLOCKED;
std::array<uint8_t, NONCE_BYTES> active_nonce{};
uint64_t nonce_issued_us = 0;
bool nonce_valid = false;

// Ring buffer for LAN log monitoring & console output capture
portMUX_TYPE log_lock = portMUX_INITIALIZER_UNLOCKED;
char log_ring[LOG_RING_BYTES];
uint32_t log_write_seq = 0;  // total bytes ever written
vprintf_like_t prev_vprintf = nullptr;

void append_to_log_ring(const char* data, size_t len) {
    taskENTER_CRITICAL(&log_lock);
    for (size_t i = 0; i < len; ++i) {
        log_ring[log_write_seq % LOG_RING_BYTES] = data[i];
        ++log_write_seq;
    }
    taskEXIT_CRITICAL(&log_lock);
}

int tee_vprintf(const char* fmt, va_list args) {
    char buf[256];
    va_list copy;
    va_copy(copy, args);
    const int n = vsnprintf(buf, sizeof buf, fmt, copy);
    va_end(copy);
    if (n > 0) {
        append_to_log_ring(buf, std::min<size_t>(n, sizeof buf - 1));
    }
    return prev_vprintf ? prev_vprintf(fmt, args) : vprintf(fmt, args);
}

void to_hex(const uint8_t* bytes, size_t len, char* out) {
    static constexpr char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = HEX[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = HEX[bytes[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

bool from_hex(const char* hex, uint8_t* out, size_t out_len) {
    if (strlen(hex) != out_len * 2) {
        return false;
    }
    for (size_t i = 0; i < out_len; ++i) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

// Consumes the active nonce if it matches `nonce_hex` and hasn't expired.
bool consume_nonce(const char* nonce_hex, std::array<uint8_t, NONCE_BYTES>& out_nonce) {
    if (!from_hex(nonce_hex, out_nonce.data(), NONCE_BYTES)) {
        return false;
    }
    const uint64_t now = esp_timer_get_time();
    bool ok = false;
    taskENTER_CRITICAL(&nonce_lock);
    if (nonce_valid && (now - nonce_issued_us) <= NONCE_TTL_US &&
        constant_time_eq(active_nonce.data(), out_nonce.data(), NONCE_BYTES)) {
        nonce_valid = false;  // one-time use!
        ok = true;
    }
    taskEXIT_CRITICAL(&nonce_lock);
    return ok;
}

// Verifies HMAC-SHA256(lan_key, nonce || payload) == sig_hex
bool verify_hmac(const std::array<uint8_t, NONCE_BYTES>& nonce,
                 const uint8_t* payload,
                 size_t payload_len,
                 const char* sig_hex) {
    uint8_t expected[HMAC_BYTES];
    if (!from_hex(sig_hex, expected, HMAC_BYTES)) {
        return false;
    }
    uint8_t actual[HMAC_BYTES];
    HmacSha256 hmac;
    hmac.init(lan_key.data());
    hmac.update(nonce.data(), nonce.size());
    hmac.update(payload, payload_len);
    hmac.final(actual);
    return constant_time_eq(actual, expected, HMAC_BYTES);
}

esp_err_t handle_challenge(httpd_req_t* req) {
    std::array<uint8_t, NONCE_BYTES> n;
    esp_fill_random(n.data(), n.size());
    taskENTER_CRITICAL(&nonce_lock);
    active_nonce = n;
    nonce_issued_us = esp_timer_get_time();
    nonce_valid = true;
    taskEXIT_CRITICAL(&nonce_lock);

    char nonce_hex[NONCE_BYTES * 2 + 1];
    to_hex(n.data(), n.size(), nonce_hex);

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t* app = esp_app_get_description();
    char json[256];
    snprintf(json, sizeof json,
             "{\"nonce\":\"%s\",\"partition\":\"%s\",\"version\":\"%s\"}",
             nonce_hex,
             running ? running->label : "?",
             app ? app->version : "?");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_lan_ota(httpd_req_t* req) {
    char nonce_hex[NONCE_BYTES * 2 + 4] = {};
    char sig_hex[HMAC_BYTES * 2 + 4] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Nonce", nonce_hex, sizeof nonce_hex) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "X-Signature", sig_hex, sizeof sig_hex) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Missing X-Nonce or X-Signature");
        return ESP_FAIL;
    }

    std::array<uint8_t, NONCE_BYTES> nonce;
    if (!consume_nonce(nonce_hex, nonce)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid or expired challenge nonce");
        return ESP_FAIL;
    }

    uint8_t expected_sig[HMAC_BYTES];
    if (!from_hex(sig_hex, expected_sig, HMAC_BYTES)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Malformed X-Signature");
        return ESP_FAIL;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_FAIL;
    }

    HmacSha256 hmac;
    hmac.init(lan_key.data());
    hmac.update(nonce.data(), nonce.size());

    char buf[2048];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        const int r = httpd_req_recv(req, buf, std::min(remaining, sizeof buf));
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota_handle);
            return ESP_FAIL;
        }
        hmac.update(reinterpret_cast<const uint8_t*>(buf), r);
        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
            return ESP_FAIL;
        }
        remaining -= r;
    }

    uint8_t computed_sig[HMAC_BYTES];
    hmac.final(computed_sig);

    // Cryptographically verify HMAC-SHA256(key, nonce || firmware_image) BEFORE activating the partition!
    if (!constant_time_eq(computed_sig, expected_sig, HMAC_BYTES)) {
        esp_ota_abort(ota_handle);
        ESP_LOGW(TAG, "LAN OTA rejected: HMAC-SHA256 signature mismatch");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "HMAC-SHA256 signature mismatch");
        return ESP_FAIL;
    }

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(update_partition) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA image validation failed");
        return ESP_FAIL;
    }

    char resp[160];
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"partition\":\"%s\",\"bytes\":%u}",
             update_partition->label,
             static_cast<unsigned>(req->content_len));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "LAN OTA verified & flashed to %s (%u bytes). Rebooting...",
             update_partition->label, static_cast<unsigned>(req->content_len));
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_lan_cmd(httpd_req_t* req) {
    char nonce_hex[NONCE_BYTES * 2 + 4] = {};
    char sig_hex[HMAC_BYTES * 2 + 4] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Nonce", nonce_hex, sizeof nonce_hex) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "X-Signature", sig_hex, sizeof sig_hex) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Missing X-Nonce or X-Signature");
        return ESP_FAIL;
    }
    std::array<uint8_t, NONCE_BYTES> nonce;
    if (!consume_nonce(nonce_hex, nonce)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid or expired challenge nonce");
        return ESP_FAIL;
    }

    char cmd[256] = {};
    const int len = httpd_req_recv(req, cmd, sizeof cmd - 1);
    if (len < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty command");
        return ESP_FAIL;
    }
    cmd[len] = '\0';

    if (!verify_hmac(nonce, reinterpret_cast<const uint8_t*>(cmd), len, sig_hex)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "HMAC-SHA256 signature mismatch");
        return ESP_FAIL;
    }

    uint32_t before_seq = 0;
    taskENTER_CRITICAL(&log_lock);
    before_seq = log_write_seq;
    taskEXIT_CRITICAL(&log_lock);

    char out_buf[2048];
    size_t out_pos = 0;
    FILE* old_stdout = stdout;
    FILE* mem_file = fopencookie(
        nullptr, "w",
        cookie_io_functions_t{
            .read = nullptr,
            .write = [](void*, const char* data, size_t size) -> ssize_t {
                append_to_log_ring(data, size);
                return size;
            },
            .seek = nullptr,
            .close = nullptr,
        });
    if (mem_file) {
        setvbuf(mem_file, nullptr, _IONBF, 0);
        stdout = mem_file;
    }

    int cmd_ret = 0;
    const esp_err_t err = esp_console_run(cmd, &cmd_ret);
    fflush(stdout);
    if (mem_file) {
        stdout = old_stdout;
        fclose(mem_file);
    }

    taskENTER_CRITICAL(&log_lock);
    const uint32_t after_seq = log_write_seq;
    const uint32_t avail = std::min<uint32_t>(after_seq - before_seq, sizeof out_buf - 1);
    const uint32_t start = after_seq - avail;
    for (uint32_t i = 0; i < avail; ++i) {
        out_buf[i] = log_ring[(start + i) % LOG_RING_BYTES];
    }
    out_pos = avail;
    taskEXIT_CRITICAL(&log_lock);
    out_buf[out_pos] = '\0';

    if (err == ESP_ERR_NOT_FOUND) {
        snprintf(out_buf, sizeof out_buf, "Unrecognized command: %s\n", cmd);
        out_pos = strlen(out_buf);
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, out_buf, out_pos);
}

esp_err_t handle_lan_logs(httpd_req_t* req) {
    char nonce_hex[NONCE_BYTES * 2 + 4] = {};
    char sig_hex[HMAC_BYTES * 2 + 4] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Nonce", nonce_hex, sizeof nonce_hex) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "X-Signature", sig_hex, sizeof sig_hex) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Missing X-Nonce or X-Signature");
        return ESP_FAIL;
    }
    std::array<uint8_t, NONCE_BYTES> nonce;
    if (!consume_nonce(nonce_hex, nonce) || !verify_hmac(nonce, nullptr, 0, sig_hex)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "HMAC-SHA256 verification failed");
        return ESP_FAIL;
    }

    uint32_t since = 0;
    char query[64] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        char val[32] = {};
        if (httpd_query_key_value(query, "since", val, sizeof val) == ESP_OK) {
            since = strtoul(val, nullptr, 10);
        }
    }

    char chunk[1024];
    uint32_t cur_seq = 0;
    size_t copy_len = 0;
    taskENTER_CRITICAL(&log_lock);
    cur_seq = log_write_seq;
    const uint32_t earliest = (cur_seq > LOG_RING_BYTES) ? (cur_seq - LOG_RING_BYTES) : 0;
    if (since < earliest) since = earliest;
    if (since < cur_seq) {
        copy_len = std::min<size_t>(cur_seq - since, sizeof chunk);
        for (size_t i = 0; i < copy_len; ++i) {
            chunk[i] = log_ring[(since + i) % LOG_RING_BYTES];
        }
    }
    taskEXIT_CRITICAL(&log_lock);

    char seq_hdr[32];
    snprintf(seq_hdr, sizeof seq_hdr, "%lu", static_cast<unsigned long>(since + copy_len));
    httpd_resp_set_hdr(req, "X-Log-Seq", seq_hdr);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, chunk, copy_len);
}

int ota_command(int argc, char** argv) {
    if (argc > 2) {
        printf("Usage: ota [stable|dev|https://...|version]\n");
        return 1;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t* app = esp_app_get_description();
    if (argc == 2 && strcmp(argv[1], "version") == 0) {
        printf("partition     %s (0x%lx)\n", running ? running->label : "?",
               running ? static_cast<unsigned long>(running->address) : 0UL);
        printf("version       %s\n", app ? app->version : "?");
        printf("built         %s %s\n", app ? app->date : "?", app ? app->time : "?");
        return 0;
    }

    const char* arg = (argc == 2) ? argv[1] : "stable";
    char url[256];
    if (strncmp(arg, "http://", 7) == 0 || strncmp(arg, "https://", 8) == 0) {
        snprintf(url, sizeof url, "%s", arg);
    } else if (strcmp(arg, "stable") == 0 || strcmp(arg, "dev") == 0) {
        snprintf(url, sizeof url, "%s/%s/%s", configured_base_url, arg, configured_binary_name);
    } else {
        printf("Usage: ota [stable|dev|https://...|version]\n");
        return 1;
    }

    printf("Running on %s (%s). Fetching %s ...\n",
           running ? running->label : "?",
           app ? app->version : "?",
           url);
    fflush(stdout);

    const esp_err_t err = update_from_url(url);
    if (err != ESP_OK) {
        printf("OTA failed: %s (0x%x)\n", esp_err_to_name(err), err);
        return 1;
    }
    return 0;
}

}  // namespace

esp_err_t update_from_url(const char* url) {
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.timeout_ms = 20000;
    http_cfg.keep_alive_enable = true;
    http_cfg.buffer_size = 4096;
    http_cfg.buffer_size_tx = 2048;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    const esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        const esp_partition_t* boot = esp_ota_get_boot_partition();
        printf("OTA succeeded! Next boot partition: %s. Rebooting...\n",
               boot ? boot->label : "?");
        fflush(stdout);
        esp_restart();
    }
    return err;
}

esp_err_t register_commands(const char* base_url, const char* binary_name) {
    configured_base_url = base_url;
    configured_binary_name = binary_name;
    static constexpr console_commands::Command COMMANDS[] = {
        {"ota", "[stable|dev|url|version]",
         "Update firmware over Wi-Fi from esp32-bot.github.io (default: stable), or print running slot.",
         ota_command},
    };
    return console_commands::register_all(COMMANDS);
}

esp_err_t start_lan_server(std::span<const uint8_t, 32> auth_key) {
    std::copy(auth_key.begin(), auth_key.end(), lan_key.begin());
    lan_key_set = true;

    if (!prev_vprintf) {
        prev_vprintf = esp_log_set_vprintf(tee_vprintf);
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;

    httpd_handle_t server = nullptr;
    const esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    static constexpr httpd_uri_t ROUTES[] = {
        {"/api/challenge", HTTP_GET, handle_challenge, nullptr},
        {"/api/ota", HTTP_POST, handle_lan_ota, nullptr},
        {"/api/cmd", HTTP_POST, handle_lan_cmd, nullptr},
        {"/api/logs", HTTP_GET, handle_lan_logs, nullptr},
    };
    for (const httpd_uri_t& route : ROUTES) {
        httpd_register_uri_handler(server, &route);
    }
    ESP_LOGI(TAG, "LAN OTA & Monitor server listening on port 80 (HMAC-SHA256 challenge-response)");
    return ESP_OK;
}

}  // namespace ota
