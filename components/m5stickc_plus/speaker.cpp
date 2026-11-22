#include "m5stickc_plus/speaker.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "driver/ledc.h"
#include "freertos/task.h"
#include "m5stickc_plus/pins.h"

namespace m5 {

namespace {

constexpr ledc_mode_t MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t CHANNEL = LEDC_CHANNEL_0;
// 13 bits resolves volume down to -60 dB and reaches 9.7 kHz on the 80 MHz APB clock,
// which the CPU frequency scaling never changes (it never goes below 80 MHz).
constexpr ledc_timer_bit_t RESOLUTION = LEDC_TIMER_13_BIT;
constexpr ledc_clk_cfg_t CLOCK = LEDC_USE_APB_CLK;
constexpr uint32_t FULL_DUTY = 1u << RESOLUTION;
constexpr uint32_t IDLE_HZ = 1000;  // the timer needs some frequency before the first note

constexpr size_t QUEUE_NOTES = 32;
constexpr uint32_t TASK_STACK_BYTES = 2048;
constexpr UBaseType_t TASK_PRIORITY = 1;

}  // namespace

esp_err_t Speaker::init() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = MODE;
    timer.duty_resolution = RESOLUTION;
    timer.timer_num = TIMER;
    timer.freq_hz = IDLE_HZ;
    timer.clk_cfg = CLOCK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err == ESP_OK) {
        ledc_channel_config_t channel = {};
        channel.gpio_num = BUZZER;
        channel.speed_mode = MODE;
        channel.channel = CHANNEL;
        channel.timer_sel = TIMER;
        channel.duty = 0;
        err = ledc_channel_config(&channel);
    }
    if (err == ESP_OK) {
        queue_ = xQueueCreate(QUEUE_NOTES, sizeof(Note));
        err = queue_ && xTaskCreate(task, "speaker", TASK_STACK_BYTES, this, TASK_PRIORITY, nullptr) == pdPASS
                  ? ESP_OK
                  : ESP_ERR_NO_MEM;
    }
    return err;
}

void Speaker::set_volume(uint8_t volume) {
    volume = std::min(volume, MAX_VOLUME);
    uint32_t duty = 0;
    if (volume > 0) {
        // A square wave's fundamental grows with sin(pi * duty), peaking at 50% duty.
        const float db = -VOLUME_RANGE_DB * (MAX_VOLUME - volume) / MAX_VOLUME;
        const float amplitude = std::pow(10.0f, db / 20);
        duty = std::max(1l, std::lround(std::asin(amplitude) / std::numbers::pi_v<float> * FULL_DUTY));
    }
    duty_.store(duty);
}

void Speaker::play(std::span<const Note> notes) {
    for (const Note& note : notes) {
        if (xQueueSend(queue_, &note, 0) != pdTRUE) {
            break;
        }
    }
}

void Speaker::task(void* self) {
    auto& speaker = *static_cast<Speaker*>(self);
    Note note;
    for (;;) {
        xQueueReceive(speaker.queue_, &note, portMAX_DELAY);
        speaker.sound(note.hz);
        vTaskDelay(pdMS_TO_TICKS(note.ms));
        if (uxQueueMessagesWaiting(speaker.queue_) == 0) {
            speaker.sound(0);
        }
    }
}

void Speaker::sound(uint16_t hz) {
    const uint32_t duty = hz ? duty_.load() : 0;
    if (duty) {
        ledc_set_freq(MODE, TIMER, hz);
    }
    ledc_set_duty(MODE, CHANNEL, duty);
    ledc_update_duty(MODE, CHANNEL);
}

}  // namespace m5
