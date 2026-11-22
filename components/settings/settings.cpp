#include "settings.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "console_commands.h"

namespace settings {

namespace {

constexpr char NVS_NAMESPACE[] = "settings";
constexpr size_t TEXT_BYTES = 32;

// Constant-initialized, so settings defined as globals can register in any order.
constinit SettingBase* head = nullptr;
constinit SettingBase* tail = nullptr;

class Nvs {
public:
    explicit Nvs(nvs_open_mode_t mode) : error_(nvs_open(NVS_NAMESPACE, mode, &handle_)) {}
    ~Nvs() {
        if (error_ == ESP_OK) {
            nvs_close(handle_);
        }
    }
    esp_err_t error() const { return error_; }
    nvs_handle_t handle() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t error_;
};

void print_value(const SettingBase& setting) {
    char value[TEXT_BYTES];
    printf("%.*s\n", static_cast<int>(setting.format(value, sizeof value)), value);
}

void print_line(const SettingBase& setting) {
    char value[TEXT_BYTES];
    char range[TEXT_BYTES];
    const int value_size = setting.format(value, sizeof value);
    const int range_size = setting.format_range(range, sizeof range);
    printf("%-15s %-8.*s %-10.*s %s\n", setting.name(), value_size, value, range_size, range, setting.help());
}

SettingBase* find_or_complain(const char* name) {
    SettingBase* setting = SettingBase::find(name);
    if (!setting) {
        printf("No setting '%s'. `get` lists them.\n", name);
    }
    return setting;
}

int get_command(int argc, char** argv) {
    if (argc == 1) {
        for (const SettingBase* s = head; s; s = s->next()) {
            print_line(*s);
        }
        return 0;
    }
    const SettingBase* setting = find_or_complain(argv[1]);
    if (!setting) {
        return 1;
    }
    print_value(*setting);
    return 0;
}

int set_command(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: set <name> <value>\n");
        return 1;
    }
    SettingBase* setting = find_or_complain(argv[1]);
    if (!setting) {
        return 1;
    }
    if (!setting->parse(argv[2])) {
        char range[TEXT_BYTES];
        printf("%s takes %.*s\n", setting->name(), static_cast<int>(setting->format_range(range, sizeof range)), range);
        return 1;
    }
    print_value(*setting);
    return 0;
}

int save_command(int, char**) {
    const esp_err_t err = save_all();
    if (err != ESP_OK) {
        printf("Save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Saved\n");
    return 0;
}

int defaults_command(int, char**) {
    for (SettingBase* s = head; s; s = s->next()) {
        s->reset();
    }
    printf("Back to defaults; `save` keeps them\n");
    return 0;
}

}  // namespace

SettingBase::SettingBase(const char* name, const char* help) : name_(name), help_(help) {
    assert(strlen(name) < NVS_KEY_NAME_MAX_SIZE);
    (tail ? tail->next_ : head) = this;
    tail = this;
}

SettingBase* SettingBase::first() {
    return head;
}

SettingBase* SettingBase::find(std::string_view name) {
    SettingBase* s = head;
    while (s && name != s->name()) {
        s = s->next();
    }
    return s;
}

esp_err_t load_all() {
    const Nvs nvs(NVS_READONLY);
    // Nothing has been saved yet the first time; every setting takes its default.
    if (nvs.error() != ESP_OK && nvs.error() != ESP_ERR_NVS_NOT_FOUND) {
        return nvs.error();
    }
    for (SettingBase* s = head; s; s = s->next()) {
        if (nvs.error() == ESP_OK) {
            s->load(nvs.handle());
        } else {
            s->reset();
        }
    }
    return ESP_OK;
}

esp_err_t save_all() {
    const Nvs nvs(NVS_READWRITE);
    esp_err_t err = nvs.error();
    for (const SettingBase* s = head; s && err == ESP_OK; s = s->next()) {
        err = s->save(nvs.handle());
    }
    return err == ESP_OK ? nvs_commit(nvs.handle()) : err;
}

esp_err_t register_commands() {
    static constexpr console_commands::Command COMMANDS[] = {
        {"get", "[name]", "List settings: name, value, range, meaning. With a name, print its value.", get_command},
        {"set", "<name> <value>", "Change a setting now. `save` keeps it after a reboot.", set_command},
        {"save", nullptr, "Save all settings to flash.", save_command},
        {"defaults", nullptr, "Put every setting back to its default (not saved).", defaults_command},
    };
    return console_commands::register_all(COMMANDS);
}

}  // namespace settings
