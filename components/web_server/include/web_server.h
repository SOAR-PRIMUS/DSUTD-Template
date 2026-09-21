#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Struct to read data from the WebSocket connection to webpage/index.html
// Positive X is right; positive Y is down
typedef struct {
  float steer_x;    // Steering joystick X offset: -1 (left) to 1 (right)
  float steer_y;    // Steering joystick Y offset: unused
  float throttle_x; // Throttle joystick X offset: unused
  float throttle_y; // Throttle joystick Y offset: -1 (up) to 1 (down)
  bool  is_auton;   // True when "Autonomous" mode selected on controller webpage; forces
                    // above 4 values to -1.00 while true
} joystick_data_t;

// Starts the HTTP server; serves webpage/index.html on "/" and
// Accepts joystick data via WebSocket on "/ws". Call once after WiFi is up
// Requires CONFIG_HTTPD_WS_SUPPORT to be enabled in sdkconfig.
esp_err_t web_server_start(void);

// Stops the HTTP server.
esp_err_t web_server_stop(void);

// Copies most recent joystick values into a provided
// joystick_data_t pointer *out. Returns true if a client
// is currently connected (ignores abrupt disconnections)
bool web_server_get_latest_joystick(joystick_data_t *out);

// Time in milliseconds since last joystick data values were received.
// Intended for use in timeout mechanism to detect
// stale connections to the webpage
int64_t web_server_ms_since_latest_joystick(void);

#ifdef __cplusplus
}
#endif