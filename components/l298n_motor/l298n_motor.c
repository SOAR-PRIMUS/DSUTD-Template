/**
 * @file l298n_motor.c
 * @brief Component code for the GPIO driver for L298N motor driver channels
 * 
 * @ingroup l298n_motor
 * 
 * @author Sidharth N
 * @date 23 September 2026
 * 
 * Source code for the L298N motor channel driver abstraction.  
 */

#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/mcpwm_prelude.h>
#include <esp_log.h>
#include <esp_check.h>

#include "l298n_motor.h"

// Component-specific tag for logging
static const char *TAG = "l298n_motor";

// PWM tuning values
#define MOTOR_MCPWM_GROUP_ID             0
#define MOTOR_MCPWM_TIMER_RESOLUTION_HZ  1000000  // 1 MHz -> 1 tick = 1us
#define MOTOR_PWM_FREQ_HZ                1000     // 1 kHz

struct l298n_motor_t {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t operator;
    mcpwm_cmpr_handle_t comparator;
    mcpwm_gen_handle_t generator;
    int in1_gpio_num;
    int in2_gpio_num;
    uint32_t period_ticks;
};

typedef struct {
    int in1_gpio_num;     // GPIO pin wired to the L298N's first IN pin
    int in2_gpio_num;     // GPIO pin wired to the L298N's second IN pin
    int en_gpio_num;      // GPIO pin wired to the L298N's EN pin, PWM-driven
    uint32_t pwm_freq_hz; // PWM frequency applied to the EN pin
} l298n_motor_config_t;

typedef struct {
    int group_id;           // MCPWM group ID, [0, SOC_MCPWM_GROUPS-1] 
    uint32_t resolution_hz; // MCPWM timer tick resolution, in Hz
} l298n_motor_mcpwm_config_t;

esp_err_t l298n_motor_new_mcpwm_device(const l298n_motor_config_t *config,
                                       const l298n_motor_mcpwm_config_t *mcpwm_config,
                                       l298n_motor_handle_t *ret_motor)
{
    ESP_RETURN_ON_FALSE(config && mcpwm_config && ret_motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    l298n_motor_handle_t motor = calloc(1, sizeof(struct l298n_motor_t));
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_NO_MEM, TAG, "no mem for motor");

    motor->in1_gpio_num = config->in1_gpio_num;
    motor->in2_gpio_num = config->in2_gpio_num;
    motor->period_ticks = mcpwm_config->resolution_hz / config->pwm_freq_hz;

    esp_err_t ret = ESP_OK;

    // Direction pins are plain digital outputs, per the L298N truth table
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << config->in1_gpio_num) | (1ULL << config->in2_gpio_num),
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "config direction gpio failed");
    gpio_set_level(config->in1_gpio_num, 0);
    gpio_set_level(config->in2_gpio_num, 0);

    // EN pin is driven by a single MCPWM comparator/generator: high from the
    // start of the period until the compare threshold, then low. That single
    // PWM duty is exactly what the L298N's enable pin expects.
    ESP_LOGI(TAG, "Create MCPWM timer, group %d", mcpwm_config->group_id);
    mcpwm_timer_config_t timer_config = {
        .group_id = mcpwm_config->group_id,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = mcpwm_config->resolution_hz,
        .period_ticks = motor->period_ticks,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_GOTO_ON_ERROR(mcpwm_new_timer(&timer_config, &motor->timer), err, TAG, "create timer failed");

    ESP_LOGI(TAG, "Create MCPWM operator");
    mcpwm_operator_config_t operator_config = {
        .group_id = mcpwm_config->group_id,
    };
    ESP_GOTO_ON_ERROR(mcpwm_new_operator(&operator_config, &motor->operator), err, TAG, "create operator failed");
    ESP_GOTO_ON_ERROR(mcpwm_operator_connect_timer(motor->operator, motor->timer), err, TAG, "connect timer failed");

    ESP_LOGI(TAG, "Create comparator");
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_GOTO_ON_ERROR(mcpwm_new_comparator(motor->operator, &comparator_config, &motor->comparator), err, TAG, "create comparator failed");
    ESP_GOTO_ON_ERROR(mcpwm_comparator_set_compare_value(motor->comparator, 0), err, TAG, "set init compare failed");

    ESP_LOGI(TAG, "Create PWM generator on GPIO%d (EN)", config->en_gpio_num);
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = config->en_gpio_num,
    };
    ESP_GOTO_ON_ERROR(mcpwm_new_generator(motor->operator, &generator_config, &motor->generator), err, TAG, "create generator failed");

    ESP_GOTO_ON_ERROR(mcpwm_generator_set_action_on_timer_event(motor->generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)),
        err, TAG, "set generator timer action failed");
    ESP_GOTO_ON_ERROR(mcpwm_generator_set_action_on_compare_event(motor->generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, motor->comparator, MCPWM_GEN_ACTION_LOW)),
        err, TAG, "set generator compare action failed");

    ESP_GOTO_ON_ERROR(mcpwm_timer_enable(motor->timer), err, TAG, "enable timer failed");
    ESP_GOTO_ON_ERROR(mcpwm_timer_start_stop(motor->timer, MCPWM_TIMER_START_NO_STOP), err, TAG, "start timer failed");

    *ret_motor = motor;
    return ESP_OK;

err:
    // Simplified cleanup for example code: any MCPWM resource already
    // created above the failing call is intentionally left in place, since
    // there is no live motor handle for the caller to tear down otherwise.
    free(motor);
    return ret;
}

l298n_motor_handle_t create_motor(const char *label, int in1, int in2, int en)
{
    ESP_LOGI(TAG, "Creating %s motor (IN1=%d, IN2=%d, EN=%d)", label, in1, in2, en);
    l298n_motor_config_t config = {
        .in1_gpio_num = in1,
        .in2_gpio_num = in2,
        .en_gpio_num = en,
        .pwm_freq_hz = MOTOR_PWM_FREQ_HZ,
    };
    l298n_motor_mcpwm_config_t mcpwm_config = {
        .group_id = MOTOR_MCPWM_GROUP_ID,
        .resolution_hz = MOTOR_MCPWM_TIMER_RESOLUTION_HZ,
    };
    l298n_motor_handle_t motor = NULL;
    ESP_ERROR_CHECK(l298n_motor_new_mcpwm_device(&config, &mcpwm_config, &motor));
    ESP_ERROR_CHECK(l298n_motor_enable(motor));
    return motor;
}

esp_err_t l298n_motor_set_forward(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    gpio_set_level(motor->in1_gpio_num, 1);
    gpio_set_level(motor->in2_gpio_num, 0);
    return ESP_OK;
}

esp_err_t l298n_motor_set_reverse(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    gpio_set_level(motor->in1_gpio_num, 0);
    gpio_set_level(motor->in2_gpio_num, 1);
    return ESP_OK;
}

esp_err_t l298n_motor_set_coast(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    gpio_set_level(motor->in1_gpio_num, 0);
    gpio_set_level(motor->in2_gpio_num, 0);
    return mcpwm_comparator_set_compare_value(motor->comparator, 0);
}

esp_err_t l298n_motor_brake(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    gpio_set_level(motor->in1_gpio_num, 1);
    gpio_set_level(motor->in2_gpio_num, 1);
    return mcpwm_comparator_set_compare_value(motor->comparator, motor->period_ticks);
}

esp_err_t l298n_motor_set_speed(l298n_motor_handle_t motor, uint32_t percent)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty_ticks = (motor->period_ticks * percent) / 100;
    return mcpwm_comparator_set_compare_value(motor->comparator, duty_ticks);
}

esp_err_t l298n_motor_set_signed_speed(l298n_motor_handle_t motor, int32_t percent)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    if (percent == 0) {
        return l298n_motor_set_coast(motor);
    }
    if (percent > 0) {
        l298n_motor_set_forward(motor);
    } else {
        l298n_motor_set_reverse(motor);
        percent = -percent;
    }
    if (percent > 100) {
        percent = 100;
    }
    return l298n_motor_set_speed(motor, (uint32_t)percent);
}

esp_err_t l298n_motor_enable(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return mcpwm_timer_start_stop(motor->timer, MCPWM_TIMER_START_NO_STOP);
}

esp_err_t l298n_motor_disable(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    gpio_set_level(motor->in1_gpio_num, 0);
    gpio_set_level(motor->in2_gpio_num, 0);
    return mcpwm_timer_start_stop(motor->timer, MCPWM_TIMER_STOP_EMPTY);
}

esp_err_t l298n_motor_del(l298n_motor_handle_t motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    mcpwm_del_generator(motor->generator);
    mcpwm_del_comparator(motor->comparator);
    mcpwm_del_operator(motor->operator);
    mcpwm_timer_disable(motor->timer);
    mcpwm_del_timer(motor->timer);
    free(motor);
    return ESP_OK;
}