#include "m5stickc_plus/display.h"

#include <algorithm>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "m5stickc_plus/pins.h"

namespace m5 {

namespace {

constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
constexpr unsigned PIXEL_CLOCK_HZ = 27'000'000;  // what M5Stack's library runs it at
constexpr int SPI_MODE = 0;
constexpr int COMMAND_BITS = 8;
constexpr int PARAMETER_BITS = 8;
constexpr int BITS_PER_PIXEL = 16;
constexpr size_t TRANSFER_QUEUE_DEPTH = 1;
constexpr uint32_t FLUSH_TIMEOUT_MS = 100;

// The panel is 240x320 inside; the visible 135x240 sits at this offset when rotated 180 degrees.
constexpr int GAP_X = 53;
constexpr int GAP_Y = 40;

// Rows sent per transfer.
constexpr int BUFFER_ROWS = 24;
static_assert(Display::HEIGHT % BUFFER_ROWS == 0);
constexpr size_t BUFFER_BYTES = Display::WIDTH * BUFFER_ROWS * sizeof(uint16_t);

// The display takes each pixel high byte first.
constexpr uint16_t to_wire(uint16_t color) {
    return __builtin_bswap16(color);
}

// FNV-1a, to tell whether a row changed without keeping a copy of the frame.
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

uint32_t row_hash(std::span<const uint16_t> row) {
    uint32_t hash = FNV_OFFSET;
    for (uint16_t pixel : row) {
        hash = (hash ^ pixel) * FNV_PRIME;
    }
    return hash;
}

}  // namespace

esp_err_t Display::init() {
    spi_bus_config_t bus = {};
    std::fill(std::begin(bus.iocfg), std::end(bus.iocfg), GPIO_NUM_NC);
    bus.mosi_io_num = LCD_MOSI;
    bus.sclk_io_num = LCD_SCLK;
    bus.max_transfer_sz = BUFFER_BYTES;
    esp_err_t err = spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_handle_t io = nullptr;
    if (err == ESP_OK) {
        esp_lcd_panel_io_spi_config_t config = {};
        config.cs_gpio_num = LCD_CS;
        config.dc_gpio_num = LCD_DC;
        config.spi_mode = SPI_MODE;
        config.pclk_hz = PIXEL_CLOCK_HZ;
        config.trans_queue_depth = TRANSFER_QUEUE_DEPTH;
        config.on_color_trans_done = on_flushed;
        config.user_ctx = this;
        config.lcd_cmd_bits = COMMAND_BITS;
        config.lcd_param_bits = PARAMETER_BITS;
        err = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI_HOST), &config, &io);
    }
    if (err == ESP_OK) {
        esp_lcd_panel_dev_config_t config = {};
        config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;  // BGR swaps red and blue on this panel
        config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
        config.bits_per_pixel = BITS_PER_PIXEL;
        config.reset_gpio_num = LCD_RST;
        err = esp_lcd_new_panel_st7789(io, &config, &panel_);
    }
    if (err == ESP_OK) {
        flushed_ = xSemaphoreCreateBinary();
        rows_ = static_cast<uint16_t*>(heap_caps_malloc(BUFFER_BYTES, MALLOC_CAP_DMA));
        err = flushed_ && rows_ ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) err = esp_lcd_panel_reset(panel_);
    if (err == ESP_OK) err = esp_lcd_panel_init(panel_);
    if (err == ESP_OK) err = esp_lcd_panel_invert_color(panel_, true);
    if (err == ESP_OK) err = esp_lcd_panel_mirror(panel_, true, true);
    if (err == ESP_OK) err = esp_lcd_panel_set_gap(panel_, GAP_X, GAP_Y);
    if (err == ESP_OK) err = clear(0);
    if (err == ESP_OK) err = esp_lcd_panel_disp_on_off(panel_, true);
    return err;
}

esp_err_t Display::clear(uint16_t color) {
    const esp_err_t err = fill_rect(0, 0, WIDTH, HEIGHT, color);
    if (err == ESP_OK) {
        std::array<uint16_t, WIDTH> row;
        row.fill(color);
        shown_.fill(row_hash(row));
        shown_valid_ = true;
    }
    return err;
}

esp_err_t Display::present(std::span<const uint16_t> frame) {
    if (frame.size() != static_cast<size_t>(WIDTH) * HEIGHT) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!panel_ || !rows_) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wait_for_flush();
    int pending = 0;  // changed rows in the buffer, waiting to be sent
    for (int y = 0; y < HEIGHT && err == ESP_OK; ++y) {
        const auto row = frame.subspan(y * WIDTH, WIDTH);
        const uint32_t hash = row_hash(row);
        const bool changed = !shown_valid_ || hash != shown_[y];
        if (changed) {
            std::ranges::transform(row, rows_ + pending * WIDTH, to_wire);
            shown_[y] = hash;
            ++pending;
        }
        // Send a run of changed rows when it ends, fills the buffer, or reaches the bottom.
        if (pending > 0 && (!changed || pending == BUFFER_ROWS || y == HEIGHT - 1)) {
            const int end = changed ? y + 1 : y;
            err = flush(0, end - pending, WIDTH, pending);
            pending = 0;
        }
    }
    shown_valid_ = err == ESP_OK;
    return err;
}

esp_err_t Display::fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!panel_ || !rows_) return ESP_ERR_INVALID_STATE;
    if (w <= 0 || h <= 0) return ESP_OK;
    const int right = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(x) + w, 0, WIDTH));
    const int bottom = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(y) + h, 0, HEIGHT));
    x = std::clamp(x, 0, WIDTH);
    y = std::clamp(y, 0, HEIGHT);
    w = right - x;
    h = bottom - y;
    if (w <= 0 || h <= 0) return ESP_OK;
    esp_err_t err = wait_for_flush();
    if (err != ESP_OK) return err;
    shown_valid_ = false;
    const int max_rows = WIDTH * BUFFER_ROWS / w;
    std::fill_n(rows_, std::min(h, max_rows) * w, to_wire(color));
    for (int cy = y; cy < bottom && err == ESP_OK;) {
        const int chunk = std::min(max_rows, bottom - cy);
        err = flush(x, cy, w, chunk);
        cy += chunk;
    }
    return err;
}

esp_err_t Display::draw_pixels(int x, int y, int w, int h, const uint16_t* rgb565_native) {
    if (!panel_ || !rows_) return ESP_ERR_INVALID_STATE;
    if (!rgb565_native || x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT ||
        w <= 0 || h <= 0 || w > WIDTH - x || h > HEIGHT - y) return ESP_ERR_INVALID_ARG;
    const int total = w * h;
    if (total > WIDTH * BUFFER_ROWS) return ESP_ERR_INVALID_SIZE;
    const esp_err_t err = wait_for_flush();
    if (err != ESP_OK) return err;
    shown_valid_ = false;
    std::transform(rgb565_native, rgb565_native + total, rows_, to_wire);
    return flush(x, y, w, h);
}

esp_err_t Display::wait_for_flush() {
    if (transfer_pending_) {
        // A timeout does not cancel DMA. Keep the buffer intact until the callback arrives.
        if (xSemaphoreTake(flushed_, pdMS_TO_TICKS(FLUSH_TIMEOUT_MS)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        transfer_pending_ = false;
    }
    return ESP_OK;
}

esp_err_t Display::flush(int x, int y, int w, int h) {
    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel_, x, y, x + w, y + h, rows_);
    if (err != ESP_OK) return err;
    transfer_pending_ = true;
    return wait_for_flush();
}

bool Display::on_flushed(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* self) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<Display*>(self)->flushed_, &woken);
    return woken == pdTRUE;
}

}  // namespace m5
