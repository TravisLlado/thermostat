/*
 * ESP32-C3 Thermostat Controller
 *
 * Controls a heater via relay based on a time-of-day schedule and weather
 * forecast.  Each night the device fetches hourly temperatures from
 * Open-Meteo; if any scheduled run-period hour is forecast below the
 * configured threshold, the heater follows its schedule that day.
 *
 * A single button provides manual control:
 *   short press — toggle heater on/off (overrides schedule)
 *   long  press — enable / disable the heater entirely
 *
 * A WS2812 LED indicates state:
 *   green  = heater on
 *   orange = heater off (enabled)
 *   red    = heater disabled
 *   pulsing = manual override active
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include "config.h"
#include "credentials.h"
#include "button.h"
#include "WebInfo.h"

// Function prototypes
void buttonShortPress();
void buttonLongPress();
void applyState();
void fetchGeoBlocking();
bool syncTimeAndForecast();
bool evaluatePeriodForecast(int periodIdx);
float getMinForecastForPeriod(int periodIdx);
int   currentPeriodIndex();
const char* currentColorName();
String nextScheduleTransition();
void printFullStatus(Print& out);
void onWebSerialMessage(uint8_t* data, size_t len);
void formatTimeOfDay(int32_t timeOfDay, char* buffer);

// HTTP server
AsyncWebServer server(SERVER_PORT);

// WS2812 LED
Adafruit_NeoPixel leds(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);

// Button instance
Button button(PIN_BTN, BUTTON_DEBOUNCE_MS, BUTTON_SHORT_PRESS_MS, BUTTON_LONG_PRESS_MS,
              Button::ACTIVE_LOW, buttonShortPress, buttonLongPress);

// Heater state
bool g_schedule_on       {false}; // Schedule says heater should be on right now
bool g_manual_override   {false}; // XOR toggle via short press
bool g_program_cancelled {false}; // Long press disables everything

// Time tracking
int32_t g_currentTime {0};

// Hourly temperature forecast (°F, indices 0-23)
float g_forecast[24] {0};
bool  g_forecastValid {false};

// Geolocation and time state
float geoLat          {NAN};
float geoLon          {NAN};
bool  geoValid        {false};
int   tzOffsetMinutes {0};
bool  timeValid       {false};

unsigned long lastWifiCheckMs     {0};
unsigned long lastPrintTime       {0};
int           lastDailyRefreshDay {-1};

// ============================================================================
// Helper Functions
// ============================================================================

bool isScheduleOnNow() {
  for (int i = 0; i < NUM_RUN_PERIODS; i++) {
    if (RUN_PERIODS[i].contains(g_currentTime)) return true;
  }
  return false;
}

float getBrightnessFactor() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return 1.0F;
  const int hour = timeinfo.tm_hour;
  return ((hour >= NIGHT_START_HOUR) || (hour < NIGHT_END_HOUR))
         ? NIGHT_BRIGHTNESS_FACTOR : 1.0F;
}

uint32_t brightnessModulatedColor(uint32_t color, float brightness) {
  return leds.Color(
    static_cast<uint8_t>(((color >> 16) & 0xFF) * brightness),
    static_cast<uint8_t>(((color >>  8) & 0xFF) * brightness),
    static_cast<uint8_t>(( color        & 0xFF) * brightness)
  );
}

// Check whether forecast temps during a specific run period fall below threshold
bool evaluatePeriodForecast(int periodIdx) {
  if (!g_forecastValid) return true; // heat by default if no forecast available
  int startHour = RUN_PERIODS[periodIdx].start / HOUR;
  int endHour   = (RUN_PERIODS[periodIdx].end + HOUR - 1) / HOUR;
  if (endHour > 24) endHour = 24;
  for (int h = startHour; h < endHour; h++) {
    if (g_forecast[h] < TEMP_THRESHOLD_F) return true;
  }
  return false;
}

// Check if we're currently in a scheduled period AND that period's forecast is cold
bool isScheduleOnWithForecast() {
  for (int i = 0; i < NUM_RUN_PERIODS; i++) {
    if (RUN_PERIODS[i].contains(g_currentTime)) {
      return evaluatePeriodForecast(i);
    }
  }
  return false;
}

// Find the minimum forecast temperature during a specific run period
float getMinForecastForPeriod(int periodIdx) {
  float minTemp = 999.0F;
  int startHour = RUN_PERIODS[periodIdx].start / HOUR;
  int endHour   = (RUN_PERIODS[periodIdx].end + HOUR - 1) / HOUR;
  if (endHour > 24) endHour = 24;
  for (int h = startHour; h < endHour; h++) {
    if (g_forecast[h] < minTemp) minTemp = g_forecast[h];
  }
  return minTemp;
}

// Find the current run period index, or -1 if outside all periods
int currentPeriodIndex() {
  for (int i = 0; i < NUM_RUN_PERIODS; i++) {
    if (RUN_PERIODS[i].contains(g_currentTime)) return i;
  }
  return -1;
}

const char* currentColorName() {
  if (button.isPressed()) return "WHITE (button)";
  if (g_program_cancelled) return "RED (disabled)";
  const bool heater_on = (g_schedule_on ^ g_manual_override);
  if (heater_on) return "GREEN (on)";
  return "ORANGE (off)";
}

// Find time of next schedule on/off transition
String nextScheduleTransition() {
  // If currently in a run period, find when it ends
  for (int i = 0; i < NUM_RUN_PERIODS; i++) {
    if (RUN_PERIODS[i].contains(g_currentTime)) {
      char buf[9];
      formatTimeOfDay(RUN_PERIODS[i].end, buf);
      return String("OFF at ") + buf;
    }
  }
  // Not in a period — find next start
  int32_t nearest = -1;
  for (int i = 0; i < NUM_RUN_PERIODS; i++) {
    if (RUN_PERIODS[i].start > g_currentTime) {
      if (nearest < 0 || RUN_PERIODS[i].start < nearest) {
        nearest = RUN_PERIODS[i].start;
      }
    }
  }
  if (nearest >= 0) {
    char buf[9];
    formatTimeOfDay(nearest, buf);
    return String("ON at ") + buf;
  }
  // Past all periods today
  char buf[9];
  formatTimeOfDay(RUN_PERIODS[0].start, buf);
  return String("ON tomorrow at ") + buf;
}

void printFullStatus(Print& out) {
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);

  const bool heater_on = !g_program_cancelled &&
                          (g_schedule_on ^ g_manual_override);

  out.println("===== THERMOSTAT STATUS =====");
  out.printf("Time:       %s\n", timeStr);
  out.printf("Heater:     %s\n", heater_on ? "ON" : "OFF");
  out.printf("Relay:      %s\n", heater_on ? "ENERGIZED" : "OFF");
  out.printf("LED:        %s%s\n", currentColorName(),
             (g_manual_override && !g_program_cancelled) ? " (pulsing)" : "");
  out.printf("Brightness: %s\n", getBrightnessFactor() < 1.0F ? "NIGHT (dimmed)" : "DAY (full)");
  out.println("--- Flags ---");
  out.printf("schedule_on:       %s  (currently %s a run period)\n",
             g_schedule_on ? "true" : "false",
             isScheduleOnNow() ? "INSIDE" : "OUTSIDE");
  out.printf("manual_override:   %s\n", g_manual_override ? "true" : "false");
  out.printf("program_cancelled: %s\n", g_program_cancelled ? "true" : "false");
  int curPeriod = currentPeriodIndex();
  if (curPeriod >= 0) {
    bool cold = evaluatePeriodForecast(curPeriod);
    out.printf("current_period:    %d [%s]\n", curPeriod + 1, cold ? "COLD -> heat" : "WARM -> skip");
  } else {
    out.printf("current_period:    none\n");
  }
  out.println("--- Schedule ---");
  for (int i = 0; i < NUM_RUN_PERIODS; i++) {
    char startBuf[9], endBuf[9];
    formatTimeOfDay(RUN_PERIODS[i].start, startBuf);
    formatTimeOfDay(RUN_PERIODS[i].end, endBuf);
    bool periodCold = evaluatePeriodForecast(i);
    out.printf("  Period %d: %s - %s [%s]", i + 1, startBuf, endBuf,
               periodCold ? "COLD" : "WARM");
    if (RUN_PERIODS[i].contains(g_currentTime)) out.print(" << ACTIVE");
    out.println();
  }
  out.printf("Next transition: %s\n", nextScheduleTransition().c_str());
  out.println("--- Forecast ---");
  out.printf("Threshold:  %.0f°F\n", TEMP_THRESHOLD_F);
  if (g_forecastValid) {
    for (int i = 0; i < NUM_RUN_PERIODS; i++) {
      float minP = getMinForecastForPeriod(i);
      out.printf("Period %d low: %.1f°F %s\n", i + 1, minP,
                 minP < TEMP_THRESHOLD_F ? "(BELOW -> heat)" : "(ABOVE -> skip)");
    }
    out.print("Hourly (°F):  ");
    for (int i = 0; i < 24; i++) {
      out.printf("%02d:%4.0f  ", i, g_forecast[i]);
      if (i == 11) { out.println(); out.print("              "); }
    }
    out.println();
    // Highlight temps during schedule periods
    out.print("Schedule hrs: ");
    for (int p = 0; p < NUM_RUN_PERIODS; p++) {
      int sh = RUN_PERIODS[p].start / HOUR;
      int eh = (RUN_PERIODS[p].end + HOUR - 1) / HOUR;
      if (eh > 24) eh = 24;
      for (int h = sh; h < eh; h++) {
        out.printf("[%02d]=%.0f°F ", h, g_forecast[h]);
      }
      if (p < NUM_RUN_PERIODS - 1) out.print("| ");
    }
    out.println();
  } else {
    out.println("Forecast: NOT AVAILABLE (defaulting to heat)");
  }
  out.println("--- Network ---");
  out.printf("WiFi:     %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  out.printf("IP:       %s\n", WiFi.localIP().toString().c_str());
  out.printf("mDNS:     %s.local\n", MDNS_HOSTNAME);
  if (geoValid) {
    out.printf("Location: %.4f, %.4f (UTC%+.1fh)\n", geoLat, geoLon, tzOffsetMinutes / 60.0F);
  }
  out.println("=============================");
}

// ============================================================================
// LED & Relay State
// ============================================================================

void applyState() {
  // While button is physically held (before long press fires), show button-press feedback color
  if (button.isPressed() && !button.longPressFired()) {
    leds.setPixelColor(0, brightnessModulatedColor(COLOR_BUTTON, getBrightnessFactor()));
    leds.show();
    return;
  }

  const bool heater_on = !g_program_cancelled &&
                          (g_schedule_on ^ g_manual_override);

  const uint32_t base_color = g_program_cancelled ? COLOR_DISABLED
                            : (heater_on ? COLOR_ON : COLOR_OFF);

  digitalWrite(PIN_RELAY1, heater_on ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY2, heater_on ? RELAY_ON : RELAY_OFF);

  float brightness = getBrightnessFactor();

  if (g_manual_override && !g_program_cancelled) {
    unsigned long phase = millis() % static_cast<unsigned long>(PULSE_PERIOD_MS);
    float pulse = static_cast<float>(phase) / (PULSE_PERIOD_MS / 2.0F);
    if (pulse > 1.0F) pulse = 2.0F - pulse;
    brightness *= (PULSE_MIN_BRIGHTNESS +
                   (PULSE_MAX_BRIGHTNESS - PULSE_MIN_BRIGHTNESS) * pulse);
  }

  leds.setPixelColor(0, brightnessModulatedColor(base_color, brightness));
  leds.show();
}

// ============================================================================
// Button Callbacks
// ============================================================================

void buttonShortPress() {
  if (!g_program_cancelled) {
    g_manual_override = !g_manual_override;
    Serial.printf("Button: short press, manual_override=%d\n", g_manual_override);
  }
}

void buttonLongPress() {
  g_program_cancelled = !g_program_cancelled;
  g_manual_override = false;
  Serial.printf("Button: long press, program_cancelled=%d\n", g_program_cancelled);
}

// ============================================================================
// WiFi / Geo / Time / Forecast
// ============================================================================

void fetchGeoBlocking() {
  while (!geoValid) {
    geoValid = WebInfo::fetchGeo(geoLat, geoLon, tzOffsetMinutes);
    if (geoValid) {
      Serial.printf("Geo approx: %.4f, %.4f\n", geoLat, geoLon);
    } else {
      Serial.println("Geo failed, retrying in 5s...");
      delay(RETRY_DELAY_MS);
    }
  }
}

bool syncTimeAndForecast() {
  if (!WebInfo::initTime(tzOffsetMinutes)) {
    Serial.println("Time sync failed.");
    return false;
  }
  timeValid = true;

  float temps[24];
  if (WebInfo::fetchHourlyForecast(geoLat, geoLon, temps)) {
    memcpy(g_forecast, temps, sizeof(g_forecast));
    g_forecastValid = true;

    float minTemp = temps[0], maxTemp = temps[0];
    for (int i = 1; i < 24; i++) {
      if (temps[i] < minTemp) minTemp = temps[i];
      if (temps[i] > maxTemp) maxTemp = temps[i];
    }
    Serial.printf("Forecast: %.0f-%.0f°F\n", minTemp, maxTemp);
    for (int i = 0; i < NUM_RUN_PERIODS; i++) {
      float pMin = getMinForecastForPeriod(i);
      Serial.printf("  Period %d low: %.0f°F -> %s\n", i + 1, pMin,
                     pMin < TEMP_THRESHOLD_F ? "HEAT" : "SKIP");
    }
  } else {
    Serial.println("Forecast fetch failed, defaulting to heat");
  }
  return true;
}

// ============================================================================
// WebSerial command handler
// ============================================================================

void onWebSerialMessage(uint8_t* data, size_t len) {
  String cmd;
  for (size_t i{0}; i < len; i++) cmd += (char)data[i];
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "on") {
    g_manual_override = !g_schedule_on; // force heater on
    g_program_cancelled = false;
    applyState();
    WebSerial.println("Heater forced ON");
  } else if (cmd == "off") {
    g_manual_override = g_schedule_on; // force heater off
    g_program_cancelled = false;
    applyState();
    WebSerial.println("Heater forced OFF");
  } else if (cmd == "enable") {
    g_program_cancelled = false;
    g_manual_override = false;
    applyState();
    WebSerial.println("Heater enabled");
  } else if (cmd == "disable") {
    g_program_cancelled = true;
    g_manual_override = false;
    applyState();
    WebSerial.println("Heater disabled");
  } else if (cmd == "status") {
    printFullStatus(WebSerial);
  } else if (cmd == "help" || cmd == "?") {
    WebSerial.println("Commands: on, off, enable, disable, status, help");
  } else if (cmd.length() > 0) {
    WebSerial.println("Unknown command. Type 'help' for commands.");
  }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println("\r\n\r\nThermostat Controller");

  // Initialize relay outputs
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);
  digitalWrite(PIN_RELAY1, RELAY_OFF);
  digitalWrite(PIN_RELAY2, RELAY_OFF);

  // Initialize WS2812 LED
  leds.begin();
  leds.setBrightness(255);
  leds.setPixelColor(0, brightnessModulatedColor(COLOR_STARTUP, 1.0F));
  leds.show();

  // Initialize button
  button.begin();

  // Connect to WiFi — retry indefinitely
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi failed, retrying in 5s...");
    delay(RETRY_DELAY_MS);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", SERVER_PORT);
    Serial.println(String("mDNS: http://") + MDNS_HOSTNAME + ".local/");
  } else {
    Serial.println("mDNS failed");
  }

  // Fetch geolocation — required for weather forecast
  fetchGeoBlocking();

  // Sync clock and fetch weather forecast
  while (!syncTimeAndForecast()) {
    Serial.println("Retrying in 5s...");
    delay(RETRY_DELAY_MS);
  }

  // Record today's date and set initial schedule state
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.printf("Time set: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    lastDailyRefreshDay = timeinfo.tm_mday;
    g_currentTime = timeinfo.tm_hour * HOUR +
                    timeinfo.tm_min * MINUTE +
                    timeinfo.tm_sec;
    g_schedule_on = isScheduleOnWithForecast();
  }

  // Start WebSerial
  WebSerial.onMessage(onWebSerialMessage);
  WebSerial.begin(&server, "/");
  server.begin();

  WebSerial.println("Thermostat ready.");
  WebSerial.println(String("URL: http://") + MDNS_HOSTNAME + ".local/");
  WebSerial.println("Commands: on, off, enable, disable, status, help");

  Serial.println("Ready.");
  printFullStatus(Serial);
  printFullStatus(WebSerial);
  applyState();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  const unsigned long now{millis()};

  // Update button (handles debouncing and triggers callbacks)
  button.update();

  // Periodic WiFi watchdog
  if (now - lastWifiCheckMs >= WIFI_CHECK_INTERVAL_MS) {
    lastWifiCheckMs = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost, reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // Get current time from RTC
  struct tm timeinfo;
  bool haveTime{getLocalTime(&timeinfo)};

  if (haveTime) {
    g_currentTime = timeinfo.tm_hour * HOUR +
                    timeinfo.tm_min * MINUTE +
                    timeinfo.tm_sec;

    // Daily refresh window (2-4 AM): re-fetch geo, sync time, fetch forecast
    if (timeinfo.tm_hour >= DAILY_REFRESH_END_HOUR &&
        timeinfo.tm_mday != lastDailyRefreshDay) {
      Serial.println("Daily refresh timed out, skipping until tomorrow.");
      lastDailyRefreshDay = timeinfo.tm_mday;
    } else if (timeinfo.tm_hour >= DAILY_REFRESH_START_HOUR &&
               timeinfo.tm_hour < DAILY_REFRESH_END_HOUR &&
               timeinfo.tm_mday != lastDailyRefreshDay) {
      Serial.println("Daily refresh...");
      leds.setPixelColor(0, brightnessModulatedColor(COLOR_STARTUP, getBrightnessFactor()));
      leds.show();
      WebInfo::fetchGeo(geoLat, geoLon, tzOffsetMinutes);
      if (syncTimeAndForecast()) {
        lastDailyRefreshDay = timeinfo.tm_mday;
        if (getLocalTime(&timeinfo)) {
          g_currentTime = timeinfo.tm_hour * HOUR +
                          timeinfo.tm_min * MINUTE +
                          timeinfo.tm_sec;
        }
        Serial.println("Daily refresh complete.");
        printFullStatus(Serial);
        printFullStatus(WebSerial);
      } else {
        Serial.println("Daily refresh failed, will retry next loop.");
      }
    }

    // Update schedule state — reset manual override on transitions
    const bool currentSchedule = isScheduleOnWithForecast();
    if (currentSchedule != g_schedule_on) {
      g_manual_override = false;
      g_schedule_on = currentSchedule;
      Serial.printf("Schedule transition: schedule_on=%d\n", g_schedule_on);
    }
  }

  // Update LED and relay outputs
  applyState();

  // Periodic status log
  if (now - lastPrintTime >= STATUS_INTERVAL_MS) {
    lastPrintTime = now;
    const bool heater_on = !g_program_cancelled &&
                            (g_schedule_on ^ g_manual_override);
    char timeStr[9];
    formatTimeOfDay(g_currentTime, timeStr);

    // Compact line for Serial
    String serialMsg;
    serialMsg += timeStr;
    serialMsg += " | heater=";
    serialMsg += heater_on ? "ON" : "OFF";
    if (g_program_cancelled) serialMsg += " [DISABLED]";
    if (g_manual_override) serialMsg += " [OVERRIDE]";
    int cp = currentPeriodIndex();
    if (cp >= 0 && !evaluatePeriodForecast(cp)) serialMsg += " [WARM PERIOD]";
    Serial.println(serialMsg);

    // Verbose output for WebSerial
    String ws;
    ws += timeStr;
    ws += " | heater=";
    ws += heater_on ? "ON" : "OFF";
    ws += " | relay=";
    ws += heater_on ? "ON" : "OFF";
    ws += " | LED=";
    ws += currentColorName();
    if (g_manual_override && !g_program_cancelled) ws += " (pulsing)";
    ws += " | sched=";
    ws += g_schedule_on ? "IN_PERIOD" : "OUTSIDE";
    ws += " | override=";
    ws += g_manual_override ? "Y" : "N";
    ws += " | cancelled=";
    ws += g_program_cancelled ? "Y" : "N";
    int wcp = currentPeriodIndex();
    if (wcp >= 0) {
      bool cold = evaluatePeriodForecast(wcp);
      ws += " | period=";
      ws += String(wcp + 1);
      ws += cold ? " [COLD]" : " [WARM]";
      if (g_forecastValid) {
        char buf[24];
        snprintf(buf, sizeof(buf), " | period_low=%.0f°F", getMinForecastForPeriod(wcp));
        ws += buf;
      }
    } else {
      ws += " | period=none";
    }
    ws += " | next=";
    ws += nextScheduleTransition();
    WebSerial.println(ws);
  }

  delay(TICK_PERIOD);
}

void formatTimeOfDay(int32_t timeOfDay, char* buffer) {
  if (timeOfDay < 0) { strcpy(buffer, "??:??:??"); return; }
  const int hours   = timeOfDay / 3600;
  const int minutes = (timeOfDay % 3600) / 60;
  const int seconds = timeOfDay % 60;
  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// End of file /////////////////////////////////////////////////////////////////
