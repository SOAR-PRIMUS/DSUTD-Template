/**
 * @file robot_main.cpp 
 * 
 * Contains the entry point for the code running on the ESP32. Modify this file to control the logic during
 * autonomous and driver control.
 * 
 * @note Requires "WebSocket server support" (`CONFIG_HTTPD_WS_SUPPORT`) to be enabled in sdkconfig.
 */

#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "web_server.h"
#include "l298n_motor.h"

static const char *TAG = "l298n_robot";

// GPIO pin numbers
#define LEFT_IN1_GPIO 13
#define LEFT_IN2_GPIO 14
#define RIGHT_IN1_GPIO 11
#define RIGHT_IN2_GPIO 12
#define ENA_GPIO 10
#define ENB_GPIO 9

#define LEFT_MOTOR_POLARITY 1 // Whether left motor is reversed
#define RIGHT_MOTOR_POLARITY -1 // Whether right motor is reversed

// Control loop timing constants
// Not recommended to change these; will be exposed nonetheless

#define CONTROL_LOOP_PERIOD_MS  20  // Approx. 50 Hz
#define CONTROL_TIMEOUT_MS      4000 // Coast motors if web page does not respond for these many millis 

// PWM tuning values
#define MOTOR_MCPWM_GROUP_ID             0
#define MOTOR_MCPWM_TIMER_RESOLUTION_HZ  1000000  // 1 MHz -> 1 tick = 1us
#define MOTOR_PWM_FREQ_HZ                1000     // 1 kHz

// Handles for our motors

static l298n_motor_handle_t left_motor;
static l298n_motor_handle_t right_motor;

// ----------- Method definitions -----------

// Convenience method: sets left & right motors to specified percentage values.
// Sign denotes direction, magnitude denotes speed.
static void drive(int32_t left_percent, int32_t right_percent) {
    l298n_motor_set_signed_speed(left_motor, left_percent);
    l298n_motor_set_signed_speed(right_motor, right_percent);
}

// Code to run while in autonomous mode.
static void auton(void) {
    ESP_LOGI(TAG, "Moving forward");
    drive(100, 100);
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "Coasting");
    drive(0, 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Reverse");
    drive(-100, -100);
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "Coast");
    drive(0, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Spin turn right");
    drive(100, -100);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Spin turn left");
    drive(-100, 100);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Coast, pause before repeating demo");
    drive(0, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
}



// Code to run while in driver control mode.
static void opcontrol(const joystick_data_t *js_data) {
    // Implementation of arcade drive

    float throttle = -js_data->throttle_y;
    float steer = js_data->steer_x;

    float left_norm = throttle + steer;
    float right_norm = throttle - steer;

    // Preserve steer:throttle ratio when clamping
    // to avoid loss of one when the other hits max values
    float max_magnitude = fmaxf(fabsf(left_norm), fabsf(right_norm));
    if (max_magnitude > 1.0f) {
        left_norm = left_norm / max_magnitude;
        right_norm = right_norm / max_magnitude;
    }

    // Multiply by 100 + cast to signed int32 before sending to drive() as percentages
    drive((int32_t)(LEFT_MOTOR_POLARITY * left_norm * 100), (int32_t)(RIGHT_MOTOR_POLARITY * right_norm * 100));
}

// ----------- Main entry point -----------
extern "C" void app_main(void)
{
    left_motor = create_motor("Left motor", LEFT_IN1_GPIO, LEFT_IN2_GPIO, ENA_GPIO);
    right_motor = create_motor("Right motor", RIGHT_IN1_GPIO, RIGHT_IN2_GPIO, ENB_GPIO);
    ESP_LOGI(TAG, "Motors successfully initialized");

    ESP_ERROR_CHECK(web_server_start());

    int64_t last_status_log_us = 0;

    while (1) {
        joystick_data_t js;
        bool disconnected = web_server_get_latest_joystick(&js);
        bool stale = web_server_ms_since_latest_joystick() > CONTROL_TIMEOUT_MS;

        if (disconnected || stale) {
            // Brake both motors as a precaution
            l298n_motor_brake(left_motor);
            l298n_motor_brake(right_motor);
        } else if (js.is_auton) {
            auton(); // Switch to autonomous if enabled from the controller web page
        } else{
            opcontrol(&js);
        }

        int64_t now_us = esp_timer_get_time(); // Time in microseconds
        if (now_us - last_status_log_us > 500000) { // Print logs twice a second at most
            last_status_log_us = now_us;
            ESP_LOGI(TAG, "disconnected=%d stale=%d auton=%d steer_x=%.2f throttle_y=%.2f",
                     disconnected, stale, js.is_auton, js.steer_x, js.throttle_y);
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS));
    }
}
