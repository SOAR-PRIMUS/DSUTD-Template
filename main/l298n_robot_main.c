/*
 * l298n_robot_main.c
 *
 * stub descriptor
 */
// TODO: update above text
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "web_server.h"

static const char *TAG = "l298n_robot";

// Defining pins ahead of time
// TODO: Confirm pins for testing kit & replace these values
// #define LEFT_IN1_GPIO   46
// #define LEFT_IN2_GPIO   8

// #define RIGHT_IN1_GPIO  5
// #define RIGHT_IN2_GPIO  6
#define LEFT_IN1_GPIO 13
#define LEFT_IN2_GPIO 14

#define RIGHT_IN1_GPIO 11
#define RIGHT_IN2_GPIO 12

// Adding this because of how many times I've had to flip the signs for reversed motors
#define LEFT_POL 1 // Whether left motor is reversed
#define RIGHT_POL -1 // Whether right motor is reversed

// L298n left motor (SINGULAR) pins: OUT1, OUT2
// L298n right motor (SINGULAR) pins: OUT3, OUT4

// PWM tuning values
#define MOTOR_MCPWM_GROUP_ID             0
#define MOTOR_MCPWM_TIMER_RESOLUTION_HZ  1000000  // 1 MHz -> 1 tick = 1us
#define MOTOR_PWM_FREQ_HZ                1000     // 1 kHz

// WiFi AP config n shiz
// TODO: change the password for deployment, maybe make it unique to each robot
#define WIFI_AP_SSID       "RobotControl"
#define WIFI_AP_PASSWORD   "robot1234"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CONN   2

// Control loop timing n shiz
#define CONTROL_LOOP_PERIOD_MS  20  // Approx. 50 Hz
#define CONTROL_TIMEOUT_MS      4000 // Coast motors if web page does not respond for these many millis 


// ----------- Method definitions -----------

// Initialize motor
static void motor_init(int in1, int in2) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << in1) | (1ULL << in2),
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(in1, 0);
    gpio_set_level(in2, 0);
    ESP_LOGI(TAG, "Initialized motor: IN1 = %d, IN2 = %d", in1, in2);
}

// Positive direction = forward; negative = backward; 0 = coast
// Ignores magnitude as no PWM speed control w/out ENA/ENB pins
// so we just hit it at the max value
static void motor_set(int in1, int in2, int32_t direction) {
    if (direction > 0) {
        gpio_set_level(in1, 1);
        gpio_set_level(in2, 0);
    } else if (direction < 0) {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 1);
    } else {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 0);
    }
}

// Applies independently to each motor, so can cover straight driving,
// turns, yada yada. Will eventually use the left/right percent later
static void drive(int32_t left_percent, int32_t right_percent) {
    motor_set(LEFT_IN1_GPIO, LEFT_IN2_GPIO, left_percent * LEFT_POL);
    motor_set(RIGHT_IN1_GPIO, RIGHT_IN2_GPIO, right_percent * RIGHT_POL);
}

// Starts the Wifi access point the control page connects to.
// Must be called before web_server_start() else httpd doesn't have a network interface to bind to
// which makes httpd sad, and therefore me :(
static void wifi_initialize_softap(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set the Wifi configuration
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    // If no password, set the network to open
    if (strlen(WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started; SSID:%s password:%s channel:%d",
             WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
}

// Code to run while in autonomous mode
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

// Code to run while in driver control mode
static void opcontrol(const joystick_data_t *js_data) {
    // Implements arcade drive
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
    drive((int32_t)(left_norm * 100), (int32_t)(right_norm * 100));
}

// ----------- Main code -----------

void app_main(void)
{
    // left_motor = create_motor("left (L298N channel #1)", LEFT_IN1_GPIO, LEFT_IN2_GPIO, LEFT_EN_GPIO);
    // right_motor = create_motor("right (L298N channel #2)", RIGHT_IN1_GPIO, RIGHT_IN2_GPIO, RIGHT_EN_GPIO);
    motor_init(LEFT_IN1_GPIO, LEFT_IN2_GPIO);
    motor_init(RIGHT_IN1_GPIO, RIGHT_IN2_GPIO);
    ESP_LOGI(TAG, "Motors successfully initialized");

    wifi_initialize_softap();
    ESP_ERROR_CHECK(web_server_start());

    int64_t last_status_log_us = 0;

    // ESP_LOGI(TAG, "Probably a bad idea but let's loop indefinitely through auton for now");
    while (1) {
        joystick_data_t js;
        bool disconnected = web_server_get_latest_joystick(&js);
        bool stale = web_server_ms_since_latest_joystick() > CONTROL_TIMEOUT_MS;

        if (disconnected || stale) {
            drive(0, 0);
        } else if (js.is_auton) {
            auton();
        } else{
            opcontrol(&js);
        }
        // auton();

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_status_log_us > 500000) { // at most twice a second
            last_status_log_us = now_us;
            ESP_LOGI(TAG, "disconnected=%d stale=%d auton=%d steer_x=%.2f throttle_y=%.2f",
                     disconnected, stale, js.is_auton, js.steer_x, js.throttle_y);
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS));
    }
}
