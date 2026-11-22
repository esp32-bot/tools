#include "gfx/canvas.h"

#include <algorithm>
#include <cassert>

namespace gfx {

Canvas::Canvas(std::span<uint16_t> pixels, int width, int height)
    : pixels_(pixels.first(static_cast<size_t>(width) * height)), width_(width), height_(height) {
    assert(width > 0 && height > 0);
}

void Canvas::fill(uint16_t color) {
    std::ranges::fill(pixels_, color);
}

void Canvas::fill_rect(int x, int y, int w, int h, uint16_t color) {
    const int left = std::max(x, 0);
    const int right = std::min(x + w, width_);
    if (left >= right) {
        return;
    }
    for (int row = std::max(y, 0); row < std::min(y + h, height_); ++row) {
        const auto start = pixels_.begin() + row * width_;
        std::fill(start + left, start + right, color);
    }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t color) {
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

void Canvas::hline(int x, int y, int w, uint16_t color) {
    fill_rect(x, y, w, 1, color);
}

void Canvas::vline(int x, int y, int h, uint16_t color) {
    fill_rect(x, y, 1, h, color);
}

int Canvas::text(int x, int y, std::string_view text, uint16_t color, int scale) {
    for (int left = x; char c : text) {
        if (c < FONT_FIRST || c > FONT_LAST) {
            c = '?';
        }
        const uint8_t* glyph = &FONT[(c - FONT_FIRST) * FONT_COLUMNS];
        for (int column = 0; column < FONT_COLUMNS; ++column) {
            for (int row = 0; row < FONT_ROWS; ++row) {
                if (glyph[column] >> row & 1) {
                    fill_rect(left + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        left += CHAR_ADVANCE * scale;
    }
    return text_width(text, scale);
}

}  // namespace gfx
