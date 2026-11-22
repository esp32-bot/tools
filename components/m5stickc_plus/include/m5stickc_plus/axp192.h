#pragma once

#include <cstdint>

#include "esp_err.h"
#include "m5stickc_plus/i2c_device.h"

namespace m5 {

// The AXP192 power chip: power rails, battery charging, backlight and power measurements.
// Readings return NaN when the chip doesn't answer.
class Axp192 {
public:
    static constexpr uint8_t MAX_BACKLIGHT = 100;

    enum class Press : uint8_t { NONE, SHORT, LONG };

    // Turns on the rails the display and sensors need; call before using them.
    esp_err_t init(i2c_master_bus_handle_t bus);
    // 0 turns the backlight off, 1..100 dims to bright.
    esp_err_t set_backlight(uint8_t percent) const;
    // Cuts all power; only the power button turns the device back on.
    esp_err_t power_off() const;
    // The power button press since the last call, if any. A long press is held 1 s; held
    // 4 s, the chip cuts the power by itself.
    Press take_power_button() const;

    float temperature_c() const;
    float battery_v() const;
    // Positive while charging, negative while running on battery.
    float battery_ma() const;
    float usb_v() const;
    float usb_ma() const;

private:
    float read_adc(uint8_t reg, int low_bits, float scale, float offset = 0) const;

    I2cDevice device_;
};

}  // namespace m5
