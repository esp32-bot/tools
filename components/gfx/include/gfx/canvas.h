#pragma once

// Drawing into a frame of RGB565 pixels: lines, boxes and text, clipped to the frame. Plain
// C++, so the same drawing code runs on the robot and in laptop builds (host/).

#include <cstdint>
#include <span>
#include <string_view>

#include "gfx/font5x7.h"

namespace gfx {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

class Canvas {
public:
    static constexpr int CHAR_ADVANCE = FONT_COLUMNS + 1;  // at scale 1
    static constexpr int TEXT_HEIGHT = FONT_ROWS;          // at scale 1, descenders included

    // Width of `chars` characters of text, without the gap after the last one.
    static constexpr int text_width(int chars, int scale = 1) {
        return chars > 0 ? (chars * CHAR_ADVANCE - 1) * scale : 0;
    }
    static constexpr int text_width(std::string_view text, int scale = 1) {
        return text_width(static_cast<int>(text.size()), scale);
    }

    // `pixels` holds the frame row by row: at least width * height of them.
    Canvas(std::span<uint16_t> pixels, int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    std::span<const uint16_t> pixels() const { return pixels_; }

    void fill(uint16_t color);
    void fill_rect(int x, int y, int w, int h, uint16_t color);
    void rect(int x, int y, int w, int h, uint16_t color);  // a 1-pixel outline
    void hline(int x, int y, int w, uint16_t color);
    void vline(int x, int y, int h, uint16_t color);

    // Text with its top-left corner at (x, y), in the 5x7 font magnified by `scale`. Only the
    // glyphs are drawn, so whatever is behind them shows through. Returns the width drawn.
    int text(int x, int y, std::string_view text, uint16_t color, int scale = 1);

private:
    std::span<uint16_t> pixels_;
    int width_;
    int height_;
};

}  // namespace gfx
