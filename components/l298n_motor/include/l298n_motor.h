/**
 * @file l298n_motor.h
 * @brief Per-channel GPIO driver for L298N motor driver channels
 * 
 * @defgroup l298n_motor L298N Motor Driver
 * @ingroup l298n_motor
 *
 * Header for the L298N motor channel driver abstraction.
 * Provides a C/C++ compatible API to create, control, and destroy one motor channel
 * using two IN and one EN(able) GPIO pins.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for one L298N-driven motor channel
 */
typedef struct l298n_motor_t *l298n_motor_handle_t;

/**
 * @brief Create one L298N motor channel and return the handle.
 *
 * @param label Label for the motor; used for logging
 * @param in1 GPIO pin wired to the L298N's first IN pin
 * @param in2 GPIO pin wired to the L298N's second IN pin
 * @param en GPIO pin wired to the L298N's EN pin
 */
l298n_motor_handle_t create_motor(const char *label, int in1, int in2, int en);

/**
 * @brief Change the motor direction to forward by setting the input pins, such that IN1=1, IN2=0.
 * Combine with `l298n_motor_set_speed()` to move.
 * 
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_set_forward(l298n_motor_handle_t motor);

/**
 * @brief Change the motor direction to backward by setting the input pins, such that IN1=0, IN2=1.
 * Combine with `l298n_motor_set_speed()` to move.
 * 
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_set_reverse(l298n_motor_handle_t motor);

/**
 * @brief Coast the motor by setting both input pins to LOW and no duty.
 * 
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_set_coast(l298n_motor_handle_t motor);

/**
 * @brief Brake the motor by setting both input pins to HIGH and maximum PWM duty: motor terminals are shorted together
 *        (dynamic brake). Draws more current than coasting, use sparingly.
 * 
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_brake(l298n_motor_handle_t motor);

/**
 * @brief Set PWM duty applied to the EN pin, independent of the direction the motor is set to.
 * @param motor Handle for the motor
 * @param percent Ranges from 0-100; clamps values
 */
esp_err_t l298n_motor_set_speed(l298n_motor_handle_t motor, uint32_t percent);

/**
 * @brief Convenience method that sets the direction and speed of the motor.
 * @note Sign of `percent` represents the direction, where positive is forward and 0 coasts. Magnitude represents the speed.
 * @param motor Handle for the motor
 * @param percent Ranges from -100 to 100 inclusive; 0 coasts the motor
 */
esp_err_t l298n_motor_set_signed_speed(l298n_motor_handle_t motor, int32_t percent);

/**
 * @brief (Re)start the EN pin's PWM output.
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_enable(l298n_motor_handle_t motor);

/**
 * @brief Stop the EN pin's PWM output and release the direction pins.
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_disable(l298n_motor_handle_t motor);

/**
 * @brief Destroy MCPWM resources and free the motor handle.
 * @param motor Handle for the motor
 */
esp_err_t l298n_motor_del(l298n_motor_handle_t motor);

#ifdef __cplusplus
}
#endif
