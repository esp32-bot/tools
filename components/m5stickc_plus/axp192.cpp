#include "m5stickc_plus/axp192.h"

#include <cmath>

#include "axp192_registers.h"
#include "m5stickc_plus/pins.h"

namespace m5 {

using namespace axp192;

namespace {

// An ADC reading: the high 8 bits, then the low bits in the next byte.
unsigned adc_raw(const uint8_t* data, int low_bits) {
    return (data[0] << low_bits) | (data[1] & ((1u << low_bits) - 1));
}

struct RegisterValue {
    uint8_t reg;
    uint8_t value;
};

constexpr RegisterValue STARTUP[] = {
    {LDO23_VOLTAGE, LDO23_STARTUP},
    {ADC_ENABLE, ADC_ALL},
    {CHARGE_CONTROL, CHARGE_4V2_100MA},
    {POWER_KEY, POWER_KEY_TIMING},
    {GPIO0_LDO_VOLTAGE, GPIO0_LDO_3V3},
    {GPIO0_FUNCTION, GPIO0_AS_LDO},
    {VBUS_IPSOUT, VBUS_NO_LIMITS},
    {CHARGE_HIGH_TEMP_LIMIT, CHARGE_HIGH_TEMP_STARTUP},
    {BACKUP_BATTERY_CHARGE, BACKUP_CHARGE_ON},
    {SHUTDOWN_BATTERY_DETECT, BATTERY_DETECT_ON},
};

}  // namespace

esp_err_t Axp192::init(i2c_master_bus_handle_t bus) {
    esp_err_t err = device_.init(bus, AXP192_ADDRESS, INTERNAL_I2C_HZ);
    for (const auto& [reg, value] : STARTUP) {
        if (err == ESP_OK) {
            err = device_.write(reg, value);
        }
    }
    if (err == ESP_OK) {
        err = device_.update(POWER_OUTPUT_CONTROL, 0, STARTUP_OUTPUTS);
    }
    return err;
}

esp_err_t Axp192::set_backlight(uint8_t percent) const {
    if (percent == 0) {
        return device_.update(POWER_OUTPUT_CONTROL, LDO2_ENABLE, 0);
    }
    if (percent > MAX_BACKLIGHT) {
        percent = MAX_BACKLIGHT;
    }
    const int span = BACKLIGHT_MAX_STEP - BACKLIGHT_MIN_STEP;
    const int step = BACKLIGHT_MIN_STEP + ((percent - 1) * span + (MAX_BACKLIGHT - 1) / 2) / (MAX_BACKLIGHT - 1);
    esp_err_t err = device_.update(LDO23_VOLTAGE, LDO2_MASK, step << LDO2_SHIFT);
    if (err == ESP_OK) {
        err = device_.update(POWER_OUTPUT_CONTROL, 0, LDO2_ENABLE);
    }
    return err;
}

esp_err_t Axp192::power_off() const {
    return device_.update(SHUTDOWN_BATTERY_DETECT, 0, SHUTDOWN);
}

float Axp192::temperature_c() const {
    return read_adc(INTERNAL_TEMP, ADC_12_BIT_LOW_BITS, TEMP_C_PER_LSB, TEMP_OFFSET_C);
}

float Axp192::battery_v() const {
    return read_adc(BATTERY_VOLTAGE, ADC_12_BIT_LOW_BITS, BATTERY_VOLTS_PER_LSB);
}

float Axp192::battery_ma() const {
    uint8_t data[4];  // charge, then discharge current, in one read
    if (device_.read(BATTERY_CHARGE, data, sizeof data) != ESP_OK) {
        return NAN;
    }
    const int charge = adc_raw(data, ADC_13_BIT_LOW_BITS);
    const int discharge = adc_raw(data + 2, ADC_13_BIT_LOW_BITS);
    return (charge - discharge) * BATTERY_MA_PER_LSB;
}

float Axp192::usb_v() const {
    return read_adc(VBUS_VOLTAGE, ADC_12_BIT_LOW_BITS, VBUS_VOLTS_PER_LSB);
}

float Axp192::usb_ma() const {
    return read_adc(VBUS_CURRENT, ADC_12_BIT_LOW_BITS, VBUS_MA_PER_LSB);
}

Axp192::Press Axp192::take_power_button() const {
    uint8_t status;
    if (device_.read(IRQ_STATUS_3, &status, 1) != ESP_OK) {
        return Press::NONE;
    }
    status &= POWER_KEY_SHORT_PRESS | POWER_KEY_LONG_PRESS;
    if (status == 0 || device_.write(IRQ_STATUS_3, status) != ESP_OK) {
        return Press::NONE;
    }
    return status & POWER_KEY_LONG_PRESS ? Press::LONG : Press::SHORT;
}

float Axp192::read_adc(uint8_t reg, int low_bits, float scale, float offset) const {
    uint8_t data[2];
    if (device_.read(reg, data, sizeof data) != ESP_OK) {
        return NAN;
    }
    return adc_raw(data, low_bits) * scale + offset;
}

}  // namespace m5
