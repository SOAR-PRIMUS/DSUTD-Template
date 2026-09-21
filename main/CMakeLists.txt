idf_component_register(SRCS "l298n_robot_main.c"
                        INCLUDE_DIRS "."
                        REQUIRES web_server esp_wifi esp_event esp_timer esp_netif nvs_flash esp_driver_gpio)
