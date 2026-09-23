# DSUTD Template
cool code template for DSUTD robot I think

## Requirements
- Visual Studio Code
- ESP-IDF extension for VSCode
- Git and Python (if installation via the ESP-IDF extension fails)
- Robot kit (with one ESP32-S3 board)
- USB to micro-USB cable

## how 2 use in VS Code:
1. install the ESP-IDF extension for VS Code
2. follow the installation wizard in the Extension
3. clone the repo from github
4. modify robot_main.cpp to add the following:
  a. autonomous code in auton()
  b. driver control code in opcontrol()
5. open the SDK Configuration editor in the ESP-IDF extension
6. enable "WebSocket server support" and save
7. open the ESP-IDF Terminal in the ESP-IDF extension
8. once you've entered the virtual environment (you should see `(venv)` in your command-line prompt), run `idf.py build flash`