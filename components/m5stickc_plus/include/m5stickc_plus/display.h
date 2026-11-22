#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "m5stickc_plus/screen.h"

namespace m5 {

// The 135x240 ST7789V2 display, portrait, the way up the old firmware used (M5 rotation 2).
// Shows frames of RGB565 pixels drawn elsewhere (see gfx::Canvas), sending only the rows that
// changed. Not thread-safe: use it from one task.
class Display {
public:
    static constexpr int WIDTH = SCREEN_WIDTH;
    static constexpr int HEIGHT = SCREEN_HEIGHT;

    // Needs the AXP192's display rails on.
    esp_err_t init();
    esp_err_t clear(uint16_t color);
    // Shows `frame`, WIDTH x HEIGHT pixels row by row.
    esp_err_t present(std::span<const uint16_t> frame);

private:
    esp_err_t flush(int y, int rows);
    static bool on_flushed(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* event, void* self);

    esp_lcd_panel_handle_t panel_ = nullptr;
    SemaphoreHandle_t flushed_ = nullptr;
    uint16_t* rows_ = nullptr;                // pixels on their way to the display, in wire byte order
    std::array<uint32_t, HEIGHT> shown_ = {};  // a hash of each row on the screen
};

}  // namespace m5
