#include <WiFi.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>

#include "credentials.h"

// ========================== Constants ==========================

// Time Units
constexpr int SEC = 1;
constexpr int MIN = 60 * SEC;
constexpr int HR  = 60 * MIN;

// GPIO Pins
constexpr int PIN_BUTTON = 9;
constexpr int PIN_WS2812 = 4;
constexpr int PIN_RELAY1 = 3;
constexpr int PIN_RELAY2 = 2;

// Relay Logic
constexpr int RELAY_ON = HIGH;
constexpr int RELAY_OFF = LOW;

// WS2812 LED Settings
constexpr int NUM_LEDS = 1;
Adafruit_NeoPixel global_leds(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);

// LED Colors (0xRRGGBB)
constexpr uint32_t COLOR_ON       = 0x002000; // Green
constexpr uint32_t COLOR_OFF      = 0x251200; // Orange
constexpr uint32_t COLOR_DISABLED = 0x150000; // Red
constexpr uint32_t COLOR_STARTUP  = 0x400040; // Magenta
constexpr uint32_t COLOR_BUTTON   = 0x303030; // White

// Button Timing
constexpr unsigned long LONG_PRESS_DURATION_MS = 1500;
constexpr unsigned long DEBOUNCE_TIME_MS = 50;

// NTP Sync
constexpr time_t NTP_RESYNC_INTERVAL_S = 24*HR;
constexpr int RESYNC_HOUR = 2;
constexpr int WIFI_TIMEZONE_OFFSET_S = -8*HR;
constexpr int WIFI_DAYLIGHT_OFFSET_S = 1*HR;

// Pulse Settings
constexpr float PULSE_FREQUENCY_HZ = 0.5F;
constexpr float PULSE_PERIOD_MS = 1000.0F/PULSE_FREQUENCY_HZ;
constexpr float PULSE_MIN_BRIGHTNESS = 0.5F;
constexpr float PULSE_MAX_BRIGHTNESS = 1.0F;

// Nighttime Brightness
constexpr float NIGHT_BRIGHTNESS_FACTOR = 0.25F;
constexpr int NIGHT_START_HOUR = 22;
constexpr int NIGHT_END_HOUR = 7;

// Timing Constants
constexpr unsigned long MILLIS_PER_SECOND = 1000;
constexpr unsigned long LOOP_DELAY_MS = 20;

// Color extraction macro
#define RGB(x) (((x) >> 16) & 0xFF), (((x) >> 8) & 0xFF), ((x) & 0xFF)

// Schedule Times (seconds since midnight)
struct RunPeriod {
  int start;
  int end;

  constexpr bool contains(int t) const {
    return (t >= start && t < end);
  }
};

constexpr RunPeriod RUN_PERIODS[] = {
  { 5*HR + 30*MIN, 8*HR },
  { 17*HR,         22*HR }
};

// ========================== Globals ==========================

bool global_schedule_on = false;
bool global_manual_override = false;
bool global_program_cancelled = false;

unsigned long global_last_ntp_sync = 0U;

// ========================== Helper Functions ==========================

int seconds_since_midnight() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  return timeinfo.tm_hour*HR + timeinfo.tm_min*MIN + timeinfo.tm_sec*SEC;
}

bool is_schedule_on_now() {
  const int now = seconds_since_midnight();
  for (const auto& period : RUN_PERIODS) {
    if (period.contains(now)) return true;
  }
  return false;
}

float get_brightness_factor() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  const int hour = timeinfo.tm_hour;
  return (((hour >= NIGHT_START_HOUR) || (hour < NIGHT_END_HOUR)) ? NIGHT_BRIGHTNESS_FACTOR : 1.0F);
}

uint32_t brightness_modulated_color(uint32_t color, float brightness) {
  return global_leds.Color(
    static_cast<uint8_t>(((color >> 16) & 0xFF)*brightness),
    static_cast<uint8_t>(((color >> 8) & 0xFF)*brightness),
    static_cast<uint8_t>((color & 0xFF)*brightness)
  );
}

void sync_ntp() {
  WiFi.begin(global_ssid, global_password);
  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    configTime(WIFI_TIMEZONE_OFFSET_S, WIFI_DAYLIGHT_OFFSET_S, "pool.ntp.org");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) delay(100);
    WiFi.disconnect(true);
  }
  global_last_ntp_sync = time(nullptr);
}

void handle_button_press() {
  global_leds.setPixelColor(0, global_leds.Color(RGB(brightness_modulated_color(COLOR_BUTTON, get_brightness_factor()))));
  global_leds.show();

  const auto start_time = millis();
  while ((millis() - start_time) < LONG_PRESS_DURATION_MS) {
    if (digitalRead(PIN_BUTTON)) {
      break; // Button released early
    }
    delay(LOOP_DELAY_MS);
  }

  const auto press_duration = millis() - start_time;

  if (press_duration >= LONG_PRESS_DURATION_MS) {
    global_program_cancelled = !global_program_cancelled;
    global_manual_override = false;
  } else {
    if (!global_program_cancelled) global_manual_override = !global_manual_override;
  }

  apply_state();
}

void apply_state() {
  const bool heater_on = !global_program_cancelled && (global_schedule_on ^ global_manual_override);

  const uint32_t base_color = (global_program_cancelled ? COLOR_DISABLED :
                              (heater_on ? COLOR_ON : COLOR_OFF));

  digitalWrite(PIN_RELAY1, (heater_on ? RELAY_ON : RELAY_OFF));
  digitalWrite(PIN_RELAY2, (heater_on ? RELAY_ON : RELAY_OFF));

  float brightness = get_brightness_factor();

  if (global_manual_override && !global_program_cancelled) {
    unsigned long phase = millis() % static_cast<unsigned long>(PULSE_PERIOD_MS);
    float pulse = static_cast<float>(phase) / (PULSE_PERIOD_MS / 2.0F);
    if (pulse > 1.0F) pulse = 2.0F - pulse;
    brightness *= (PULSE_MIN_BRIGHTNESS + (PULSE_MAX_BRIGHTNESS - PULSE_MIN_BRIGHTNESS) * pulse);
  }

  global_leds.setPixelColor(0, brightness_modulated_color(base_color, brightness));
  global_leds.show();
}

void update_ntp_time() {
  global_leds.setPixelColor(0, global_leds.Color(RGB(COLOR_STARTUP)));
  global_leds.show();
  sync_ntp();
}

// ========================== Main Functions ==========================

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);

  global_leds.begin();
  global_leds.setBrightness(255);
  global_leds.clear();
  global_leds.show();

  global_leds.setPixelColor(0, global_leds.Color(RGB(COLOR_STARTUP)));
  global_leds.show();

  update_ntp_time();
}

void loop() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  if (timeinfo.tm_hour == RESYNC_HOUR && (time(nullptr) - global_last_ntp_sync) > 1 * HR) {
    update_ntp_time();
  }

  static bool last_button_state = HIGH;
  const bool current_button_state = digitalRead(PIN_BUTTON);

  if (current_button_state == LOW && last_button_state == HIGH) {
    handle_button_press();
  }
  last_button_state = current_button_state;

  static bool last_scheduled = false;
  const bool current_schedule = is_schedule_on_now();

  if (current_schedule != last_scheduled) {
    global_manual_override = false;
    global_schedule_on = current_schedule;
  }
  last_scheduled = current_schedule;

  apply_state();
  delay(LOOP_DELAY_MS);
}
