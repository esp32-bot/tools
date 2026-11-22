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
// changed. Serialize calls from different tasks.
class Display {
public:
    static constexpr int WIDTH = SCREEN_WIDTH;
    static constexpr int HEIGHT = SCREEN_HEIGHT;

    // Needs the AXP192's display rails on.
    esp_err_t init();
    esp_err_t clear(uint16_t color);
    // Shows `frame`, WIDTH x HEIGHT pixels row by row.
    esp_err_t present(std::span<const uint16_t> frame);
    // Fills the visible portion of a rectangle.
    esp_err_t fill_rect(int x, int y, int w, int h, uint16_t color);
    // Sends an in-bounds rectangle of native RGB565 pixels in one DMA transfer.
    esp_err_t draw_pixels(int x, int y, int w, int h, const uint16_t* rgb565_native);

private:
    esp_err_t wait_for_flush();
    esp_err_t flush(int x, int y, int w, int h);
    static bool on_flushed(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* event, void* self);

    esp_lcd_panel_handle_t panel_ = nullptr;
    SemaphoreHandle_t flushed_ = nullptr;
    uint16_t* rows_ = nullptr;                // pixels on their way to the display, in wire byte order
    std::array<uint32_t, HEIGHT> shown_ = {};  // a hash of each row on the screen
    bool shown_valid_ = false;
    bool transfer_pending_ = false;
};

}  // namespace m5
