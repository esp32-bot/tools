#pragma once

// How the M5StickC Plus is wired.

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_types.h"

namespace m5 {

// Internal I2C bus: the AXP192 power chip and the MPU6886 IMU.
constexpr i2c_port_num_t INTERNAL_I2C_PORT = 0;
constexpr gpio_num_t INTERNAL_I2C_SDA = GPIO_NUM_21;
constexpr gpio_num_t INTERNAL_I2C_SCL = GPIO_NUM_22;
constexpr uint32_t INTERNAL_I2C_HZ = 400'000;  // fast mode, the most both chips support
constexpr uint16_t AXP192_ADDRESS = 0x34;
constexpr uint16_t MPU6886_ADDRESS = 0x68;

// ST7789V2 display on SPI.
constexpr gpio_num_t LCD_MOSI = GPIO_NUM_15;
constexpr gpio_num_t LCD_SCLK = GPIO_NUM_13;
constexpr gpio_num_t LCD_CS = GPIO_NUM_5;
constexpr gpio_num_t LCD_DC = GPIO_NUM_23;
constexpr gpio_num_t LCD_RST = GPIO_NUM_18;

// Passive buzzer.
constexpr gpio_num_t BUZZER = GPIO_NUM_2;

// Red LED, lit when the pin is low.
constexpr gpio_num_t LED = GPIO_NUM_10;

// Buttons, low while pressed, with pull-ups on the board. A is the big one on the front, B
// the one on the side. (The power button is read through the AXP192.)
constexpr gpio_num_t BUTTON_A = GPIO_NUM_37;
constexpr gpio_num_t BUTTON_B = GPIO_NUM_39;

// The I2C bus on the hat header along the top, for hats like the BalaC base.
constexpr i2c_port_num_t HAT_I2C_PORT = 1;
constexpr gpio_num_t HAT_SDA = GPIO_NUM_0;
constexpr gpio_num_t HAT_SCL = GPIO_NUM_26;

}  // namespace m5
