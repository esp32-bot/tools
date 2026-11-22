#pragma once

#include "esp_err.h"

namespace m5 {

// The red LED.
class Led {
public:
    esp_err_t init();  // starts off
    void set(bool on) const;
};

}  // namespace m5
