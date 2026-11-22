#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace m5 {

// A button, read by polling it regularly: every 10 ms is quick enough for a press and slow
// enough that contact bounce looks like at most one poll.
class Button {
public:
    esp_err_t init(gpio_num_t pin);
    // Call at every poll: true once per press, when it's been down for DEBOUNCE_POLLS polls.
    bool take_press();

private:
    static constexpr uint8_t DEBOUNCE_POLLS = 2;

    gpio_num_t pin_ = GPIO_NUM_NC;
    uint8_t down_polls_ = 0;
};

}  // namespace m5
