#pragma once

// The AXP192 registers this driver uses, from the AXP192 datasheet. The startup values are
// the ones M5Stack's library uses for the StickC Plus.

#include <cstdint>

namespace m5::axp192 {

// Power rails. LDO2 powers the display backlight, LDO3 the display, DC-DC1 the ESP32.
constexpr uint8_t POWER_OUTPUT_CONTROL = 0x12;
constexpr uint8_t LDO2_ENABLE = 1 << 2;
constexpr uint8_t STARTUP_OUTPUTS = 0x4d;  // EXTEN, LDO3, LDO2, DC-DC1

// LDO2 voltage in the high nibble, LDO3 in the low: 1.8 V + 0.1 V per step.
constexpr uint8_t LDO23_VOLTAGE = 0x28;
constexpr uint8_t LDO23_STARTUP = 0xcc;  // both 3.0 V
constexpr uint8_t LDO2_SHIFT = 4;
constexpr uint8_t LDO2_MASK = 0xf0;
// The backlight range M5Stack uses: 2.5 V (dim) to 3.0 V (bright).
constexpr uint8_t BACKLIGHT_MIN_STEP = 7;
constexpr uint8_t BACKLIGHT_MAX_STEP = 12;

constexpr uint8_t VBUS_IPSOUT = 0x30;
constexpr uint8_t VBUS_NO_LIMITS = 0x80;  // USB powers the system, no voltage hold or current limit

constexpr uint8_t SHUTDOWN_BATTERY_DETECT = 0x32;
constexpr uint8_t BATTERY_DETECT_ON = 0x46;
constexpr uint8_t SHUTDOWN = 1 << 7;

constexpr uint8_t CHARGE_CONTROL = 0x33;
constexpr uint8_t CHARGE_4V2_100MA = 0xc0;  // charge on, to 4.2 V, at 100 mA

constexpr uint8_t BACKUP_BATTERY_CHARGE = 0x35;
constexpr uint8_t BACKUP_CHARGE_ON = 0xa2;  // RTC backup cell: 3.0 V, 200 uA

constexpr uint8_t POWER_KEY = 0x36;
constexpr uint8_t POWER_KEY_TIMING = 0x0c;  // 128 ms press to power on, 4 s hold to power off

// Power button presses, latched until cleared by writing their bits back.
constexpr uint8_t IRQ_STATUS_3 = 0x46;
constexpr uint8_t POWER_KEY_SHORT_PRESS = 1 << 1;
constexpr uint8_t POWER_KEY_LONG_PRESS = 1 << 0;  // held 1 s (POWER_KEY_TIMING)

constexpr uint8_t CHARGE_HIGH_TEMP_LIMIT = 0x39;
constexpr uint8_t CHARGE_HIGH_TEMP_STARTUP = 0xfc;

constexpr uint8_t ADC_ENABLE = 0x82;
constexpr uint8_t ADC_ALL = 0xff;

// GPIO0 as an LDO at 3.3 V: powers the microphone.
constexpr uint8_t GPIO0_FUNCTION = 0x90;
constexpr uint8_t GPIO0_AS_LDO = 0x02;
constexpr uint8_t GPIO0_LDO_VOLTAGE = 0x91;
constexpr uint8_t GPIO0_LDO_3V3 = 0xf0;

// ADC readings: the high 8 bits, then the low bits in the next register.
constexpr uint8_t VBUS_VOLTAGE = 0x5a;        // 12 bits
constexpr uint8_t VBUS_CURRENT = 0x5c;        // 12 bits
constexpr uint8_t INTERNAL_TEMP = 0x5e;       // 12 bits
constexpr uint8_t BATTERY_VOLTAGE = 0x78;     // 12 bits
constexpr uint8_t BATTERY_CHARGE = 0x7a;      // 13 bits, then the discharge current at 0x7c
constexpr int ADC_12_BIT_LOW_BITS = 4;
constexpr int ADC_13_BIT_LOW_BITS = 5;

constexpr float VBUS_VOLTS_PER_LSB = 1.7e-3f;
constexpr float VBUS_MA_PER_LSB = 0.375f;
constexpr float TEMP_C_PER_LSB = 0.1f;
constexpr float TEMP_OFFSET_C = -144.7f;
constexpr float BATTERY_VOLTS_PER_LSB = 1.1e-3f;
constexpr float BATTERY_MA_PER_LSB = 0.5f;

}  // namespace m5::axp192
