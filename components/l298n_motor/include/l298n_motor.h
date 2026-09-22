/**
 * @file l298n_motor.h
 * @brief Per-channel GPIO driver for L298N motor driver channels
 * 
 * @defgroup l298n_motor L298N Motor Driver
 * @ingroup l298n_motor
 * 
 * @author Sidharth N
 * @date 22 September 2026
 *
 * Header for the L298N motor channel driver abstraction.
 * Provides a C/C++ compatible API to create, control, and destroy one motor channel
 * using two GPIO pins (IN1/IN2).
 * 
 * Does not support motor control via Pulse Width Modulation (PWM) yet.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for one L298N-driven motor channel.
 */
typedef struct l298n_motor_t *l298n_motor_handle_t;

/**
 * @brief Create one L298N motor channel, with two plain GPIOs for direction. (Speed control via MCPWM
 *        using the EN pins is not supported yet.)
 *
 * @param in1_gpio GPIO pin number for L298N IN1
 * @param in2_gpio GPIO pin number for L298N IN2
 * @param out_handle Returned motor handle
 * 
 * @returns An ESP error code, or `ESP_OK` if the operation was successful.
 */
esp_err_t l298n_motor_new(int in1_gpio, int in2_gpio, l298n_motor_handle_t *out_handle);

/**
 * @brief Sets the GPIO pins accordingly:
 *        Direction > 0: Forward (IN1 = HIGH, IN2 = LOW)
 *        Direction < 0: Backward (IN1 = LOW, IN2 = HIGH)
 *        Direction = 0: Coast (IN1 = IN2 = LOW)
 * 
 * @param motor Handle for the L298N motor channel to be modified.
 * @param direction Direction to set the motor to.
 * 
 * @returns An ESP error code, or `ESP_OK` if the operation was successful.
 */
esp_err_t l298n_motor_set(l298n_motor_handle_t motor, int32_t direction);

/**
 * @brief Destroy held resources and free the motor handle.
 * 
 * @param motor Handle for the L298N motor channel to be freed.
 * 
 * @returns An ESP error code, or `ESP_OK` if the operation was successful.
 */
esp_err_t l298n_motor_del(l298n_motor_handle_t motor);

#ifdef __cplusplus
}
#endif
