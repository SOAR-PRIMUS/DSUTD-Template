idf_component_register(SRCS "robot_main.cpp"
                        INCLUDE_DIRS "."
                        REQUIRES web_server l298n_motor esp_timer)
