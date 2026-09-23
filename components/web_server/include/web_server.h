/**
 * 
 * @file web_server.h
 * @brief Web server interface for serving the controller webpage and receiving joystick data over WebSocket.
 * 
 * @defgroup web_server Web Server
 * @ingroup web_server
 * 
 * @author Sidharth N
 * @date 22 September 2026
 * 
 * Header for the web server interface that serves the controller webpage and
 * receives joystick data over WebSocket.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Struct to read data from the WebSocket connection to webpage/index.html.
// Positive X is right, positive Y is down.
typedef struct {
  float steer_x;    // Steering joystick X offset: -1 (left) to 1 (right)
  float steer_y;    // Steering joystick Y offset: unused
  float throttle_x; // Throttle joystick X offset: unused
  float throttle_y; // Throttle joystick Y offset: -1 (up) to 1 (down)
  bool  is_auton;   // True when "Autonomous" mode selected on controller webpage; forces
                    // above 4 values to -1.00 while true
} joystick_data_t;


/**
 * @brief Starts the HTTP server; serves webpage/index.html on "/" and
 * accepts joystick data via WebSocket on "/ws". 
 * 
 * @note Requires `CONFIG_HTTPD_WS_SUPPORT` to be enabled in `sdkconfig`.
 * 
 * @param None
 * 
 * @returns An ESP error code, or `ESP_OK` if the operation was successful.
 */
esp_err_t web_server_start(void);

/**
 * @brief Stops the HTTP server.
 * 
 * @param None
 * 
 * @returns An ESP error code, or `ESP_OK` if the operation was successful.
 */
esp_err_t web_server_stop(void);

/**
 * @brief Copies most recent joystick values into a provided `joystick_data_t` pointer. 
 * 
 * @param None
 * 
 * @returns True, if a client is currently connected (ignores abrupt disconnections).
 * 
 * @note Connection detection is likely bugged; discard the return value. Use to retrieve
 * joystick values and nothing else.
 */
bool web_server_get_latest_joystick(joystick_data_t *out);

/**
 * @brief Time in milliseconds since last joystick data values were received.
 * Intended for use in a timeout mechanism to detect stale connections to the webpage.
 * 
 * @param None
 * 
 * @returns The elapsed time since the last update, in milliseconds.
 */
int64_t web_server_ms_since_latest_joystick(void);

#ifdef __cplusplus
}
#endif