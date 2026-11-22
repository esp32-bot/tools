#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "esp_err.h"

namespace ota {

// Registers the `ota [stable|dev|<url>|version]` console command and provides programmatic OTA updates.
esp_err_t register_commands(const char* base_url = "https://esp32-bot.github.io/BalaX/fw",
                            const char* binary_name = "balax.bin");

esp_err_t update_from_url(const char* url);

// Starts the authenticated LAN HTTP server (port 80) for:
// - Cryptographic Challenge-Response LAN OTA flashing (`POST /api/ota`, verified via HMAC-SHA256(key, nonce || image))
// - Authenticated LAN Monitor & Console execution (`GET /api/logs`, `POST /api/cmd`)
// `auth_key` is a 32-byte (256-bit) key derived via PBKDF2-HMAC-SHA256 from secrets/wifi.json.
esp_err_t start_lan_server(std::span<const uint8_t, 32> auth_key);

}  // namespace ota
