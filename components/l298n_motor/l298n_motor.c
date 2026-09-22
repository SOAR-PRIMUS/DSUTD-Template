/**
 * @file l298n_motor.c
 * @brief Component code for the GPIO driver for L298N motor driver channels
 * 
 * @ingroup l298n_motor
 * 
 * @author Sidharth N
 * @date 22 September 2026
 * 
 * Source code for the L298N motor channel driver abstraction.  
 */

#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_check.h>

#include "l298n_motor.h"

// Component-specific tag for logging
static const char *TAG = "l298n_motor";

// Internal handle for representing a L298N motor channel.
// l298n_motor_handle_t resolves to this; see `l298n_motor.h`
struct l298n_motor_t {
    int in1_gpio;
    int in2_gpio;
};

esp_err_t l298n_motor_new(int in1_gpio, int in2_gpio, l298n_motor_handle_t *out_handle) {
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "out_handle is NULL");

    l298n_motor_handle_t motor = calloc(1, sizeof(struct l298n_motor_t));
    ESP_RETURN_ON_FALSE(motor != NULL, ESP_ERR_NO_MEM, TAG, "out of memory: could not alloc motor handle");

    motor->in1_gpio = in1_gpio;
    motor->in2_gpio = in2_gpio;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << in1_gpio) | (1ULL << in2_gpio),
        .mode = GPIO_MODE_OUTPUT
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        free(motor);
        return ret;
    }

    // Precaution: ensure motors don't beeline upon initialization
    gpio_set_level(motor->in1_gpio, 0);
    gpio_set_level(motor->in2_gpio, 0);

    ESP_LOGI(TAG, "Initialized motor: IN1 = %d, IN2 = %d", in1_gpio, in2_gpio);
    *out_handle = motor;
    return ESP_OK;
}

esp_err_t l298n_motor_set(l298n_motor_handle_t motor, int32_t direction) {
    ESP_RETURN_ON_FALSE(motor != NULL, ESP_ERR_INVALID_ARG, TAG, "motor handle is NULL");

    if (direction > 0) {
        gpio_set_level(motor->in1_gpio, 1);
        gpio_set_level(motor->in2_gpio, 0);
    } else if (direction < 0) {
        gpio_set_level(motor->in1_gpio, 0);
        gpio_set_level(motor->in2_gpio, 1);
    } else {
        gpio_set_level(motor->in1_gpio, 0);
        gpio_set_level(motor->in2_gpio, 0);
    }

    return ESP_OK;
}

esp_err_t l298n_motor_del(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor != NULL, ESP_ERR_INVALID_ARG, TAG, "motor handle is NULL");
    free(motor);
    return ESP_OK;
}
