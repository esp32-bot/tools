#pragma once

#include <atomic>
#include <cstdint>
#include <span>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace m5 {

// One step of a melody; 0 Hz is a rest.
struct Note {
    uint16_t hz;
    uint16_t ms;
};

// The passive buzzer, driven with a square wave. Notes play in the background, in order.
class Speaker {
public:
    static constexpr uint8_t MAX_VOLUME = 100;
    // Volume 1 is this much quieter than volume 100; every step in between is the same
    // number of dB, so steps sound even.
    static constexpr float VOLUME_RANGE_DB = 60;

    esp_err_t init();
    // 0 mutes. Takes effect from the next note.
    void set_volume(uint8_t volume);
    // Queues notes after any still playing. Notes that don't fit in the queue are dropped.
    void play(std::span<const Note> notes);
    void play(Note note) { play({&note, 1}); }

private:
    static void task(void* self);
    void sound(uint16_t hz);

    QueueHandle_t queue_ = nullptr;
    std::atomic<uint32_t> duty_{0};
};

}  // namespace m5
