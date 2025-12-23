# CrowPanel Smart Calendar

A standalone, touch-enabled smart calendar for the Elecrow CrowPanel 7.0" ESP32 Display. This project fetches events from a Google Calendar (via iCal URL) and displays them in a clean, scrollable list using the LVGL graphics library.

## Features

Google Calendar Integration: Fetches events directly from your Google Calendar's private .ics link.

Smart Filtering: Displays past events (last 7 days) and upcoming events (next 90 days).

Touch Interface: Scrollable event list built with LVGL widgets.

Auto-Sync: Updates automatically every 15 minutes.

Display Driver: Includes a custom LovyanGFX configuration tuned specifically for the CrowPanel 7.0" RGB interface to reduce screen tearing and jitter. (some additional tuning may be required)

## Software Requirements

Arduino IDE (v2.0 or newer recommended)

ESP32 Board Support Package (v2.0.17 or newer)

Board Manager URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

### Required Libraries:

LovyanGFX (by Lovyan03)

lvgl (v8.3.x)

WiFi, HTTPClient, WiFiClientSecure (Built-in to ESP32 core)

## Installation

1. Library Configuration

   Crucial Step: Before compiling, you must configure the LVGL library by placing the lv_conf.h from this project into the arduino libraries folder (e.g., Documents/Arduino/libraries)
   
2. Secrets File

    a. Duplicate the secrets_example.h
    b. Rename the copy to secrets.h
    c. Replace the templated values with the actual values for your environment (SSID, password, and calendar URL)

3. Uploading

    a. Open CrowPanel_Calendar.ino in Arduino IDE.

    b. Select your board: ESP32S3 Dev Module.

    c. Configure settings:

        * PSRAM: OPI PSRAM

        * Flash Mode: QIO 80MHz

        * Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS) - Important for LVGL size

    d. Click Upload.

## Troubleshooting

  Screen Flickers: The pixel clock is tuned to 12MHz in LGFX_Setup.h. Do not increase this to 16MHz as it causes sync issues on this specific panel.

  Text is Blurry/Blocky: Ensure you are using LV_FONT_DEFAULT or have enabled specific font sizes (like LV_FONT_MONTSERRAT_14) in your lv_conf.h.

  "No events found": Check the Serial Monitor (115200 baud). The code prints every event date it finds. Ensure your filter window logic in fetchCalendar() matches your actual event dates.

  Touch not working: Ensure the lv_timer_handler() is being called in the loop().

## License

This project is open source. Feel free to modify and improve!
