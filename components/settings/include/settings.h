#pragma once

// Runtime settings: named values that can be changed from the console without reflashing,
// and saved to flash so they survive a reboot.
//
// Define each setting as a global; it registers itself by name. load_all() at startup reads
// the saved values (or the defaults) and applies each one through its on_change callback.
// On the console:
//   get [name]          list settings, or print one
//   set <name> <value>  change one now; it's lost on reboot unless saved
//   save                save all current values
//   defaults            go back to the defaults (not saved)

#include <atomic>
#include <charconv>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "esp_err.h"
#include "nvs.h"

namespace settings {

class SettingBase {
public:
    SettingBase(const SettingBase&) = delete;
    SettingBase& operator=(const SettingBase&) = delete;

    const char* name() const { return name_; }
    const char* help() const { return help_; }
    SettingBase* next() const { return next_; }
    static SettingBase* first();
    static SettingBase* find(std::string_view name);

    // Sets the value from text; false if it isn't a number in range.
    virtual bool parse(std::string_view text) = 0;
    // These write text and return its length.
    virtual size_t format(char* out, size_t size) const = 0;
    virtual size_t format_range(char* out, size_t size) const = 0;
    virtual void reset() = 0;
    // Applies the saved value, or the default when there's none.
    virtual void load(nvs_handle_t nvs) = 0;
    virtual esp_err_t save(nvs_handle_t nvs) const = 0;

protected:
    // `name` is also the key in flash, so at most 15 characters.
    SettingBase(const char* name, const char* help);
    ~SettingBase() = default;

private:
    const char* const name_;
    const char* const help_;
    SettingBase* next_ = nullptr;
};

// A number between min and max. Reading and setting are safe from any task.
template <typename T>
class Setting final : public SettingBase {
    static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);

public:
    using OnChange = void (*)(T value);

    Setting(const char* name, T initial, T min, T max, const char* help, OnChange on_change = nullptr)
        : SettingBase(name, help), initial_(initial), min_(min), max_(max), value_(initial), on_change_(on_change) {}

    T get() const { return value_.load(std::memory_order_relaxed); }

    // False, and no change, if value is out of range.
    bool set(T value) {
        if (!(value >= min_ && value <= max_)) {  // also rejects NaN
            return false;
        }
        value_.store(value, std::memory_order_relaxed);
        if (on_change_) {
            on_change_(value);
        }
        return true;
    }

    bool parse(std::string_view text) override {
        T value{};
        const char* end = text.data() + text.size();
        const auto [stop, error] = std::from_chars(text.data(), end, value);
        return error == std::errc{} && stop == end && set(value);
    }

    size_t format(char* out, size_t size) const override { return to_text(get(), out, out + size) - out; }

    size_t format_range(char* out, size_t size) const override {
        char* const end = out + size;
        char* p = to_text(min_, out, end);
        if (end - p > 2) {
            *p++ = '.';
            *p++ = '.';
            p = to_text(max_, p, end);
        }
        return p - out;
    }

    void reset() override { set(initial_); }

    void load(nvs_handle_t nvs) override {
        T saved{};
        size_t size = sizeof saved;
        if (nvs_get_blob(nvs, name(), &saved, &size) != ESP_OK || size != sizeof saved || !set(saved)) {
            reset();
        }
    }

    esp_err_t save(nvs_handle_t nvs) const override {
        const T value = get();
        return nvs_set_blob(nvs, name(), &value, sizeof value);
    }

private:
    static char* to_text(T value, char* out, char* end) {
        const auto [stop, error] = std::to_chars(out, end, value);
        return error == std::errc{} ? stop : out;
    }

    const T initial_;
    const T min_;
    const T max_;
    std::atomic<T> value_;
    const OnChange on_change_;
};

// Applies every setting: the saved value, or the default.
esp_err_t load_all();
esp_err_t save_all();
// Adds get, set, save and defaults to the console.
esp_err_t register_commands();

}  // namespace settings
