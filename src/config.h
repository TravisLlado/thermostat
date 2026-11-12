// config.h ////////////////////////////////////////////////////////////////////

#pragma once

#include <Arduino.h>

// Time units //////////////////////////////////////////////////////////////////

constexpr int32_t SECOND{1};
constexpr int32_t MINUTE{60 * SECOND};
constexpr int32_t HOUR{60 * MINUTE};
constexpr int32_t DAY{24 * HOUR};

// Network /////////////////////////////////////////////////////////////////////

constexpr const char* MDNS_HOSTNAME {"thermostat"};
constexpr uint16_t    SERVER_PORT   {80};

// Serial baud rate
constexpr uint32_t SERIAL_BAUD_RATE {115200};

// Retry delay (ms) for WiFi, geo, and time-sync failures
constexpr uint32_t RETRY_DELAY_MS {5000};

// Interval (ms) between status log messages in the main loop
constexpr uint32_t STATUS_INTERVAL_MS {10000};

// Daily NTP/weather refresh window (24-hour clock): [START, END)
constexpr int DAILY_REFRESH_START_HOUR {2};
constexpr int DAILY_REFRESH_END_HOUR   {4};

// Decimal places used when displaying GPS coordinates
constexpr int GEO_COORD_DECIMALS {4};

// How often (ms) to check WiFi connectivity and re-join if disconnected
constexpr uint32_t WIFI_CHECK_INTERVAL_MS {60000};

// GPIO Pins ///////////////////////////////////////////////////////////////////

constexpr uint8_t PIN_BTN    {9U};
constexpr uint8_t PIN_WS2812 {4U};
constexpr uint8_t PIN_RELAY1 {3U};
constexpr uint8_t PIN_RELAY2 {2U};

// Relay Logic
constexpr uint8_t RELAY_ON  {HIGH};
constexpr uint8_t RELAY_OFF {LOW};

// WS2812 LED //////////////////////////////////////////////////////////////////

constexpr uint8_t NUM_LEDS {1};

// LED Colors (0xRRGGBB)
constexpr uint32_t COLOR_ON       {0x002000}; // Green   - heater on
constexpr uint32_t COLOR_OFF      {0x251200}; // Orange  - heater off (enabled)
constexpr uint32_t COLOR_DISABLED {0x150000}; // Red     - heater disabled
constexpr uint32_t COLOR_STARTUP  {0x400040}; // Magenta - startup/WiFi activity
constexpr uint32_t COLOR_BUTTON   {0x303030}; // White   - button press feedback

// LED Pulse (manual override indication)
constexpr float PULSE_FREQUENCY_HZ    {0.5F};
constexpr float PULSE_PERIOD_MS       {1000.0F / PULSE_FREQUENCY_HZ};
constexpr float PULSE_MIN_BRIGHTNESS  {0.5F};
constexpr float PULSE_MAX_BRIGHTNESS  {1.0F};

// Nighttime brightness reduction
constexpr float NIGHT_BRIGHTNESS_FACTOR {0.25F};
constexpr int   NIGHT_START_HOUR        {22};
constexpr int   NIGHT_END_HOUR          {7};

// Heater Schedule /////////////////////////////////////////////////////////////

struct RunPeriod {
  int32_t start;
  int32_t end;
  constexpr bool contains(int32_t t) const { return (t >= start && t < end); }
};

constexpr RunPeriod RUN_PERIODS[] = {
  { 5*HOUR + 30*MINUTE,  8*HOUR },  // 5:30 AM - 8:00 AM
  { 17*HOUR,            22*HOUR },   // 5:00 PM - 10:00 PM
};

constexpr int NUM_RUN_PERIODS {sizeof(RUN_PERIODS) / sizeof(RUN_PERIODS[0])};

// Temperature threshold (°F) — if forecast is below this during any run
// period, the heater follows the schedule for that day
constexpr float TEMP_THRESHOLD_F {52.0F};

// Program /////////////////////////////////////////////////////////////////////

constexpr uint32_t TICK_PERIOD {10U}; // ms

// Button //////////////////////////////////////////////////////////////////////

constexpr uint32_t BUTTON_DEBOUNCE_MS    {50U};
constexpr uint32_t BUTTON_SHORT_PRESS_MS {500U};
constexpr uint32_t BUTTON_LONG_PRESS_MS  {1000U};

// End of file /////////////////////////////////////////////////////////////////
