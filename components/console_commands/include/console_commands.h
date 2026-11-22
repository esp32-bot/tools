#pragma once

#include <span>

#include "esp_console.h"

namespace console_commands {

struct Command {
    const char* name;
    const char* hint;  // arguments, e.g. "<name> <value>"; nullptr if none
    const char* help;
    esp_console_cmd_func_t run;
};

// Adds the commands to the console. The strings must outlive the console.
inline esp_err_t register_all(std::span<const Command> commands) {
    for (const Command& command : commands) {
        esp_console_cmd_t cmd = {};
        cmd.command = command.name;
        cmd.hint = command.hint;
        cmd.help = command.help;
        cmd.func = command.run;
        if (const esp_err_t err = esp_console_cmd_register(&cmd); err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

}  // namespace console_commands
