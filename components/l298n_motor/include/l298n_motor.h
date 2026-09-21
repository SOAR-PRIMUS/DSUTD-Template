/*
 * l298n_motor.h
 *
 * header file for the driver abstraction for a single L298N motor channel (two IN pins + PWM-driven EN pin for speed control)
 * took some minor inspiration from the following:
 * - https://lastminuteengineers.com/l298n-dc-stepper-driver-arduino-tutorial/
 * - https://github.com/espressif/esp-idf/tree/master/examples/peripherals/mcpwm/mcpwm_bdc_speed_control
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
 * @brief Configuration for one L298N channel
 */
typedef struct {
    int in1_gpio_num;     // GPIO pin wired to the L298N's first IN pin
    int in2_gpio_num;     // GPIO pin wired to the L298N's second IN pin
    int en_gpio_num;      // GPIO pin wired to the L298N's EN pin, PWM-driven
    uint32_t pwm_freq_hz; // PWM frequency applied to the EN pin
} l298n_motor_config_t;

/**
 * @brief MCPWM resource configuration shared by the internal timer/operator
 */
typedef struct {
    int group_id;           // MCPWM group ID, [0, SOC_MCPWM_GROUPS-1] 
    uint32_t resolution_hz; // MCPWM timer tick resolution, in Hz
} l298n_motor_mcpwm_config_t;

/**
 * @brief Create one L298N motor channel: two plain GPIOs for direction, and
 *        one MCPWM comparator/generator pair generating the PWM speed signal.
 *
 * @param config Electrical (GPIO) configuration
 * @param mcpwm_config MCPWM timer resource configuration
 * @param ret_motor Returned motor handle
 */
esp_err_t l298n_motor_new_mcpwm_device(const l298n_motor_config_t *config,
                                       const l298n_motor_mcpwm_config_t *mcpwm_config,
                                       l298n_motor_handle_t *ret_motor);

/**
 * @brief Drive IN1=1, IN2=0. Combine with l298n_motor_set_speed() to move.
 */
esp_err_t l298n_motor_set_forward(l298n_motor_handle_t motor);

/**
 * @brief Drive IN1=0, IN2=1. Combine with l298n_motor_set_speed() to move.
 */
esp_err_t l298n_motor_set_reverse(l298n_motor_handle_t motor);

/**
 * @brief IN1=0, IN2=0 and duty=0: motor terminals are left floating (coast).
 */
esp_err_t l298n_motor_set_coast(l298n_motor_handle_t motor);

/**
 * @brief IN1=1, IN2=1 and duty=max: motor terminals are shorted together
 *        (dynamic brake). Draws more current than coasting; use sparingly.
 */
esp_err_t l298n_motor_brake(l298n_motor_handle_t motor);

/**
 * @brief Set PWM duty applied to the EN pin, independent of direction motors are set to
 * @param percent Ranges from 0-100, clamped values
 */
esp_err_t l298n_motor_set_speed(l298n_motor_handle_t motor, uint32_t percent);

/**
 * @brief Convenience method: sign = direction, magnitude = speed
 * @param percent Ranges from -100 to 100 inclusive, 0 coasts the motor
 */
esp_err_t l298n_motor_set_signed_speed(l298n_motor_handle_t motor, int32_t percent);

/**
 * @brief (Re)start the EN pin's PWM output
 */
esp_err_t l298n_motor_enable(l298n_motor_handle_t motor);

/**
 * @brief Stop the EN pin's PWM output and release the direction pins
 */
esp_err_t l298n_motor_disable(l298n_motor_handle_t motor);

/**
 * @brief Destroy MCPWM resources and free the motor handle
 */
esp_err_t l298n_motor_del(l298n_motor_handle_t motor);

#ifdef __cplusplus
}
#endif
