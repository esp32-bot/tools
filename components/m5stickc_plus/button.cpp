#include "m5stickc_plus/button.h"

namespace m5 {

esp_err_t Button::init(gpio_num_t pin) {
    pin_ = pin;
    gpio_config_t config = {};
    config.pin_bit_mask = 1ull << pin;
    config.mode = GPIO_MODE_INPUT;  // the board has the pull-up
    return gpio_config(&config);
}

bool Button::take_press() {
    if (gpio_get_level(pin_) != 0) {
        down_polls_ = 0;
        return false;
    }
    if (down_polls_ > DEBOUNCE_POLLS) {
        return false;  // still held from a press already taken
    }
    return ++down_polls_ == DEBOUNCE_POLLS;
}

}  // namespace m5
