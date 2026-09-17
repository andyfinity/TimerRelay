#include "relays.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "relays";

// Relay index 0..5 -> GPIO number. Order matches Relay 1..6 above.
static const gpio_num_t RELAY_GPIO[RELAY_COUNT] = {
    GPIO_NUM_2,   // Relay 1 (K1, D2)
    GPIO_NUM_21,  // Relay 2 (K2, D3)
    GPIO_NUM_1,   // Relay 3 (K3, D1)
    GPIO_NUM_0,   // Relay 4 (K4, D0)
    GPIO_NUM_19,  // Relay 5 (K5, D8)
    GPIO_NUM_18,  // Relay 6 (K6, D10)
};

void relays_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < RELAY_COUNT; i++) {
        mask |= (1ULL << RELAY_GPIO[i]);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    // Safe default: everything disabled (de-energized / NC).
    for (int i = 0; i < RELAY_COUNT; i++) {
        gpio_set_level(RELAY_GPIO[i], 0);
    }
    ESP_LOGI(TAG, "initialised %d relay outputs (all disabled)", RELAY_COUNT);
}

void relays_apply(const uint8_t states[RELAY_COUNT])
{
    for (int i = 0; i < RELAY_COUNT; i++) {
        gpio_set_level(RELAY_GPIO[i], states[i] ? 1 : 0);
    }
}
