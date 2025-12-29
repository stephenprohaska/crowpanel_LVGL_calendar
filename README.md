# CrowPanel Smart Calendar

A standalone, touch-enabled smart calendar for the Elecrow CrowPanel 7.0" ESP32 Display. This project fetches events from a Google Calendar (via iCal URL) and displays them in a clean, scrollable list using the LVGL graphics library.

## Features

Easy Configuration: No dedicated app required. Configure WiFi and your Calendar URL directly from your phone or computer

Smart Filtering: Displays upcoming events only (from today -> next 90 days).

Auto-Sorting: Automatically sorts events chronologically (soonest to latest), even if the calendar feed is out of order.

Touch Interface: Scrollable event list and interface built with LVGL widgets.

Auto-Sync: Updates automatically every 15 minutes.

Display Driver: Includes a custom LovyanGFX configuration tuned specifically for the CrowPanel 7.0" RGB interface to reduce screen tearing and jitter.

## Software Requirements

Arduino IDE (v2.0 or newer recommended)

ESP32 Board Support Package (v2.0.17 or newer)

Board Manager URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

Required Libraries

Install these via the Arduino Library Manager:

LovyanGFX (by Lovyan03)

lvgl (v8.4) - Do not install v9.x yet as this code is optimized for v8

WiFiManager (by tzapu) - Used for the setup portal

Note: WiFi, HTTPClient, WiFiClientSecure, and Preferences are built-in to the ESP32 core.

## Installation

1. Library Configuration (Crucial)

    a. Before compiling, you must configure the LVGL library:

    b. Navigate to your Arduino libraries folder (usually Documents/Arduino/libraries).

    c. Locate the lv_conf.h file provided in this repository.

    d. Copy it to the root of the libraries folder (next to the lvgl folder, not inside it).

2. Uploading

    a. Open CrowPanel_LVGL_Calendar.ino in Arduino IDE.

    b. Select your board: ESP32S3 Dev Module.

    c. Configure settings in the Tools menu:

        * PSRAM: OPI PSRAM

        * Flash Mode: QIO 80MHz

        * Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS) - Critical for LVGL size

    d. Click Upload.

## First Time Setup

1. Flash the sketch to the device via the Arduino IDE. Once the code is flashed to the device, the screen will turn on and enter Setup Mode.

2. The screen will display a QR Code and credentials (SSID: CrowPanel-Setup and a randomly generated Password).

3. Connect to this WiFi network using your phone or computer.

4. A "Sign In to Network" window should appear automatically. If not, open a browser and go to 192.168.4.1.

5. Click Configure WiFi.

6. Select your home WiFi network and enter the password.

7. Enter your Calendar URL: Paste your private .ics URL (from Google Calendar settings) into the "Google Calendar URL" field.

8. Click Save.

9. The device will restart, connect to your WiFi, and begin displaying your events.

## Troubleshooting

    * "No upcoming events": The calendar feed was fetched successfully, but no events were found between "Now" and 90 days from now.

    * Screen Flickers: The pixel clock is tuned to 12MHz in LGFX_Setup.h. Do not increase this to 16MHz as it causes sync issues on this specific panel.

    * Text is Blurry/Blocky: Ensure you have copied lv_conf.h correctly. The project relies on LV_FONT_MONTSERRAT_20 which is enabled in that configuration file.

    * Resetting WiFi: To clear the saved WiFi and URL settings, long press the "Upcoming Events" button/header at the top of the screen. The device will reset and reboot into Setup Mode.

### License

    This project is open source. Feel free to modify and improve!