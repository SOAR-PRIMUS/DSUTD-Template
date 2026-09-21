#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "web_server.h"

static const char *TAG = "web_server";

// Maximum size expected for a message from the WebSocket connection
// Assuming a longest plausible message of 11 characters ("-1.00,-1.00"),
// giving it some extra leeway before rejecting the message.
#define WEBSOCKET_RX_BUFFER_SIZE 32


// Embedded copy of webpage/index.html
// injected during linking by CMake via EMBED_TXTFILES
extern const uint8_t index_html_start[]   asm("_binary_index_html_start");
extern const uint8_t index_html_end[]     asm("_binary_index_html_end");

static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_lock = NULL;

// Shared state protected by s_lock. Written to by the WebSocket handler
// Read from whatever task uses the getter methods below
static joystick_data_t s_latest = {0};
static bool s_connected = false;
static int s_websocket_fd = -1;   // Socket file descriptor for the current client
static int64_t s_last_updated_us = 0;


// ----------- HTTP section or something -----------

static esp_err_t index_get_handler(httpd_req_t *req) {
  size_t len = index_html_end - index_html_start;
  httpd_resp_set_type(req, "text/html"); // Sets MIME type of the response; defaults to HTML anyways, just a precaution
  return httpd_resp_send(req, (const char*)index_html_start, len);
}

// Sets URI of webpage/index.html
static const httpd_uri_t index_uri = {
  .uri      = "/",
  .method   = HTTP_GET,
  .handler  = index_get_handler,
};


// ----------- WebSocket section or something -----------
static esp_err_t websocket_handler(httpd_req_t *req) {
  // One-off call to create WebSocket handshake when
  // a GET request is made for index.html
  if (req->method == HTTP_GET) {
    int fd = httpd_req_to_sockfd(req);
    ESP_LOGI(TAG, "WebSocket client connected: fd=%d", fd);
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
      s_websocket_fd = fd;
      s_connected = true;
      xSemaphoreGive(s_lock);
    }

    return ESP_OK;
  }

  httpd_ws_frame_t ws_packet;
    memset(&ws_packet, 0, sizeof(ws_packet));
    ws_packet.type = HTTPD_WS_TYPE_TEXT;

    // First call with max_len=0 reports the payload size in ws_packet.len
    esp_err_t return_val = httpd_ws_recv_frame(req, &ws_packet, 0);
    if (return_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame length: %d", return_val);
        return return_val;
    }
    if (ws_packet.len == 0 || ws_packet.len >= WEBSOCKET_RX_BUFFER_SIZE) {
        // Empty frame (e.g. a ping) or something bigger than "x,y" should be;
        // ignore instead of updating size & risking buffer overrun
        return ESP_OK;
    }
    
    // Char buffer to store WebSocket packet payload
    char buf[WEBSOCKET_RX_BUFFER_SIZE] = {0};
    ws_packet.payload = (uint8_t *)buf;

    // Second call to receive the actual frame from the HTML page
    return_val = httpd_ws_recv_frame(req, &ws_packet, ws_packet.len);
    if (return_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive frame: %d", return_val);
        return return_val;
    }
    buf[ws_packet.len] = '\0'; // httpd_ws_recv_frame doesn't null-terminate

    // Handler code for when joystick data is received from the HTML page
    if (ws_packet.type == HTTPD_WS_TYPE_TEXT) {
      float steer_x, steer_y, throttle_x, throttle_y;
      int auton_flag;

      // Retrieve x,y values as floats from the string data received via WebSocket
      if (sscanf(buf, "%f,%f,%f,%f,%d", &steer_x, &steer_y, &throttle_x, &throttle_y, &auton_flag) == 5) {
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
          s_latest.steer_x = steer_x;
          s_latest.steer_y = steer_y;
          s_latest.throttle_x = throttle_x;
          s_latest.throttle_y = throttle_y;
          s_latest.is_auton = (auton_flag != 0); // auton_flag == 0 -> is_auton = false, true otherwise
          s_last_updated_us = esp_timer_get_time();
          xSemaphoreGive(s_lock);
        }
      } else {
        // Fallback for malformed messages; will not stop execution
        ESP_LOGW(TAG, "Ignoring unparseable WS message: %s", buf);
      }
    }

    return ESP_OK;
}


static const httpd_uri_t websocket_uri = {
    .uri          = "/ws",
    .method       = HTTP_GET,
    .handler      = websocket_handler,
    .is_websocket = true,
};

// Called by httpd when a client socket closes, either
// via a clean closing handshake or abrupt drop. 
// Makes web_server_get_latest_joystick()'s "connected" flag trustworthy i think?
static void on_client_close(httpd_handle_t hd, int socket_fd)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        if (socket_fd == s_websocket_fd) {
            ESP_LOGI(TAG, "WebSocket client disconnected (fd=%d)", socket_fd);
            s_connected = false;
            s_websocket_fd = -1;
        }
        xSemaphoreGive(s_lock);
    }
    close(socket_fd); // I in fact, choose to claim responsibility for close_fn with this
}

// ---------------- Public API functions or something ----------------

// Starts the web server. Don't think I need to explain this one
esp_err_t web_server_start(void)
{
  // Uses mutex to ensure thread-safe mutations
  // since I have no experience with how ESP's dual-core processor
  // will handle my precious variable
  s_lock = xSemaphoreCreateMutex();
  if (s_lock == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }

  // Set httpd config
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.close_fn = on_client_close;

  // Attempt to start the server
  esp_err_t ret = httpd_start(&s_server, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %d", ret);
    return ret;
  }

  // Register URI handlers for the WebSocket and index page
  httpd_register_uri_handler(s_server, &index_uri);
  httpd_register_uri_handler(s_server, &websocket_uri);

  ESP_LOGI(TAG, "Web server started");
  return ESP_OK;
}

// Stops the web server, for whatever reason you'd want to do this
esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}

// Retrieves latest joystick data; to be used along with web_server_ms_since_last_joystick
bool web_server_get_latest_joystick(joystick_data_t *out)
{
  bool connected = false;
  if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
    *out = s_latest;
    connected = s_connected;
    xSemaphoreGive(s_lock);
  }
  return connected;
}

// Retrieves time in milliseconds since last joystick data update
int64_t web_server_ms_since_latest_joystick(void)
{
  int64_t last = 0;
  if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
    // Sets time from last update only if lock successfully obtained
    last = s_last_updated_us;
    xSemaphoreGive(s_lock);
  }
  if (last == 0) {
    // Fallback; assume no message received yet
    return INT64_MAX;
  }

  // Convert from microseconds to milliseconds before returning
  return (esp_timer_get_time() - last) / 1000;
}