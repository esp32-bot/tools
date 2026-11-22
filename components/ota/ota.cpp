#include "ota.h"

#include <cstdio>
#include <cstring>

#include "console_commands.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

namespace ota {

namespace {

const char* configured_base_url = "https://esp32-bot.github.io/balax/fw";
const char* configured_binary_name = "balax.bin";

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

}  // namespace ota
