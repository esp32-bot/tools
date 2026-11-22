#pragma once

#include "esp_err.h"

namespace ota {

// Registers the `ota [stable|dev|<url>]` console command and provides programmatic OTA updates.
// By default `base_url` is "https://esp32-bot.github.io/balax/fw" and `binary_name` is "balax.bin",
// so `ota` (or `ota stable`) fetches:
//   https://esp32-bot.github.io/balax/fw/stable/balax.bin
// and `ota dev` fetches:
//   https://esp32-bot.github.io/balax/fw/dev/balax.bin
esp_err_t register_commands(const char* base_url = "https://esp32-bot.github.io/balax/fw",
                            const char* binary_name = "balax.bin");

esp_err_t update_from_url(const char* url);

}  // namespace ota
