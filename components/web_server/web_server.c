/**
 * @file l298n_motor.c
 * @brief Component code to manage the web server
 * 
 * @ingroup web_server
 * 
 * Source code for the web server used for delivering the controller page over the ESP's SoftAP, as well as
 * fetching joystick data from the user controller page via WebSocket.
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "web_server.h"

// Component-specific tag for logging
static const char *TAG = "web_server";

// Maximum size expected for a message from the WebSocket connection
// Assuming a longest plausible message of 11 characters ("-1.00,-1.00"),
// giving it some extra leeway before rejecting the message.
#define WEBSOCKET_RX_BUFFER_SIZE 32

// Size for MAC address buffer
#define MAC_ADDR_SIZE 6
// Number of retry attempts for initializing Soft-AP when retrieving MAC address
#define RETRY_ATTEMPTS 3
// Max size for WiFi SSID
#define SSID_LEN 50
// Max size for WiFi password
#define PASSWORD_LEN 50

// WiFi AP configuration
#define DEFAULT_WIFI_AP_SSID       "RobotControl_DEFAULT"
#define DEFAULT_WIFI_AP_PASSWORD   "robot1234"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CONN   2


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

// MAC address of ESP32
static uint8_t mac[MAC_ADDR_SIZE];


// ----------- Misc functions -----------
static esp_err_t load_mac_address(void) {
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
  ESP_LOGI("MAC address", "MAC address: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return ret;
}

// ----------- HTTP section or something -----------

// Handler code to serve the index page upon receiving a `GET /` request.
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

// Handler code to establish a WebSocket connection between the server and client,
// then receive joystick data from the client's controller page.
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
        ESP_LOGW(TAG, "Ignoring unparseable websock msg: %s", buf);
      }
    }

    return ESP_OK;
}

// URI endpoint for the WebSocket connection
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

    // Set up WiFi SSID and password
    char ssid[SSID_LEN] = {0};
    char password[PASSWORD_LEN] = {0};

    ret = load_mac_address();
    uint8_t attempts = 0;

    if (ret == ESP_OK) {
      snprintf(ssid, sizeof(ssid), "%s_%02X%02X", "RobotControl", mac[0], mac[1]);
      snprintf(password, sizeof(password), "%02X%02X_%02X%02X", mac[0], mac[1], mac[0], mac[1]);
    }
    // Attempt to reinitialize the Soft-AP if received ERR_WIFI_NOT_INIT
    else if (ret == ESP_ERR_WIFI_NOT_INIT) {
      ESP_LOGW(TAG, "Failed to load MAC address: Soft-AP not initialized");

      if (attempts > RETRY_ATTEMPTS) {
        ESP_LOGE(TAG, "Failed to initialize Soft-AP after %d attempts; aborting", RETRY_ATTEMPTS);
        ESP_LOGI(TAG, "Loading default SSID: %s", DEFAULT_WIFI_AP_SSID);
        strlcpy(ssid, DEFAULT_WIFI_AP_SSID, sizeof(ssid));
        strlcpy(password, DEFAULT_WIFI_AP_PASSWORD, sizeof(password));
      }

      attempts++;
      ESP_LOGW(TAG, "Retry initialize Soft-AP again: attempt %d", attempts);
    }
    // Default behavior in case of misc error
    else {
      ESP_LOGE(TAG, "Failed to load MAC address: %d", ret);
      ESP_LOGI(TAG, "Loading default SSID: %s", DEFAULT_WIFI_AP_SSID);
      strlcpy(ssid, DEFAULT_WIFI_AP_SSID, sizeof(ssid));
      strlcpy(password, DEFAULT_WIFI_AP_PASSWORD, sizeof(password));
    }

    // Set the Wifi configuration
    wifi_config_t wifi_config = {0};
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    strlcpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    strlcpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);

    // If no password, set the network to open
    if (strlen(DEFAULT_WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi Soft-AP started; SSID:%s password:%s channel:%d",
             wifi_config.ap.ssid, wifi_config.ap.password, WIFI_AP_CHANNEL);
}

// ---------------- Public API functions or something ----------------

esp_err_t web_server_start(void)
{
  // Initialize the wifi access point before starting HTTP server
  wifi_initialize_softap();

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


esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    ESP_LOGI(TAG, "Web server stopped");

    return ESP_OK;
}

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