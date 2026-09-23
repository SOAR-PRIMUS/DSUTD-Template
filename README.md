# DSUTD Template
cool code template for DSUTD robot I think

## Requirements
### Visual Studio Code
- Visual Studio Code
- ESP-IDF extension for VSCode
- Git and Python (if installation via the ESP-IDF extension fails to do so)
- 1x robot kit
- 1x USB-A to micro-USB cable

### Command Line
- PC (Windows, macOS or Linux)
- Git and Python (if installation via EIM fails to do so)
- 1x robot kit
- 1x USB-A to micro-USB cable

## Installing EIM
### Visual Studio Code
Follow the steps in [this guide](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html#).

### Command Line
Follow the steps as per your operating system:
- [Windows](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/windows-setup.html)
- [macOS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/macos-setup.html)
- [Linux](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-setup.html)

Once you have EIM GUI or CLI installed, proceed below. 

## Flashing to ESP32
### Visual Studio Code
1. clone this repository from GitHub and open it in Visual Studio Code.
2. Modify `robot_main.cpp` to add the following:
    1. autonomous code in `auton()`; and
    2. driver control code in `opcontrol()`.
3. Open the SDK Configuration editor in the ESP-IDF extension menu.
4. Enable `WebSocket server support` and press Save.
5. Open the ESP-IDF Terminal in the ESP-IDF extension menu.
6. Once you've entered the virtual environment (you should see `(venv)` in your command-line prompt), run `idf.py flash`.

### Terminal
1. Clone this repository from GitHub and note the path.
2. Open the IDF Terminal.
    1. For EIM GUI,
        1. Open the ESP-IDF Installation Manager `eim`.
        2. Under `Manage Installations`, click `Open Dashboard`.
        3. Select your desired ESP-IDF version (ideally the latest available) and click `Open IDF Terminal` to launch a terminal session with the ESP-IDF environment activated.
    2. For EIM CLI,
        1. On Windows, click the `IDF_v5.4.2_Powershell` shortcut on your desktop to open a PowerShell session with the environment activated. 
        2. On Linux or macOS, you should see a command to activate the ESP-IDF environment after successfully installing EIM CLI, such as `source "/Users/username/.espressif/tools/activate_idf_v5.4.2.sh"`. Run said command in a terminal session.
3. Navigate to the directory where you cloned this repository.
4. Run `idf.py set-target esp32` to set your build target to ESP32.
5. Run `idf.py menuconfig` to open the SDK Configuration menu.
6. Navigate to `Component config` > `HTTP Server`.
7. Ensure `WebSocket server support` is enabled (it should look like: `[*] WebSocket server support`).
8. Save and quit the SDK Configuration menu (`s` > `q`), returning to your terminal window.
9. Run `idf.py flash` to build and flash the ESP32-S3 binary image to your board.
    1. If you plugged in the USB cable to your ESP32, then specifying the port isn't necessary; `idf.py` will automatically detect it for you.


## Troubleshooting
#### My board is flashing different colors and won't let me flash!
Hold the **BOOT** button and press **RESET**, then release **BOOT**. Once you've confirmed the light has stopped, try flashing again.

#### It says 'no serial ports found'!
Check the USB connection between your computer and the ESP32 board. Try not to connect multiple boards at a time, else you'll have to specify the port since `idf.py` won't know which board you want to flash to.

#### My code builds and flashes, but doesn't run properly on the board!
Reconnect your ESP32 to your computer via the USB cable and monitor your board's output logs.
1. For Visual Studio Code users, navigate to your ESP-IDF extension menu and click `Monitor Device`.
2. For EIM GUI/CLI users, open an IDF terminal and run `idf.py monitor`.

The issue should become apparent in the logs.

#### How do I exit the monitor session?
Press `Ctrl+]` (likely `⌘+]` on macOS) or mash your keyboard interrupt key combination (`Ctrl+C` on Windows) until the logs stop printing.