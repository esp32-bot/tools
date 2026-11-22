#include "m5stickc_plus/led.h"

#include "driver/gpio.h"
#include "m5stickc_plus/pins.h"

namespace m5 {

esp_err_t Led::init() {
    set(false);
    return gpio_set_direction(LED, GPIO_MODE_OUTPUT);
}

void Led::set(bool on) const {
    gpio_set_level(LED, !on);  // lit when low
}

}  // namespace m5
