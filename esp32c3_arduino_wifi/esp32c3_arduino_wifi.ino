#include <WiFi.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>

#include "credentials.h"

// GPIO pins
constexpr int PIN_BUTTON = 9;
constexpr int PIN_WS2812 = 4;
constexpr int PIN_RELAY1 = 3;
constexpr int PIN_RELAY2 = 2;

// WS2812 settings
constexpr int NUM_LEDS = 1;
Adafruit_NeoPixel global_leds(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);

// Relay logic
constexpr int RELAY_ON = HIGH;
constexpr int RELAY_OFF = LOW;

// Color definitions (full brightness, 0xRRGGBB format)
constexpr uint32_t COLOR_SCHEDULED_ON  = 0x002600;
constexpr uint32_t COLOR_SCHEDULED_OFF = 0x400000;
constexpr uint32_t COLOR_MANUAL_ON     = 0x321900;
constexpr uint32_t COLOR_MANUAL_OFF    = 0x401000;
constexpr uint32_t COLOR_PRESSED       = 0x808080;
constexpr uint32_t COLOR_CANCELLED     = 0x080000;

// Color extraction macros
#define R(x) (((x) >> 16) & 0xFF)
#define G(x) (((x) >> 8) & 0xFF)
#define B(x) ((x) & 0xFF)

// Global variables
bool global_schedule_on = false;
bool global_manual_override = false;
bool global_program_cancelled = false;

unsigned long global_last_ntp_sync = 0;
constexpr time_t global_NTP_RESYNC_INTERVAL = 24 * 60 * 60;
constexpr time_t global_RESYNC_HOUR = 2;

// Schedule periods
struct RunPeriod {
  int start;
  int end;
};

constexpr RunPeriod global_run_periods[] = {
    //  On.              End
  { 5 * 3600 + 30 * 60,  8 * 3600  },
  { 17 * 3600,           22 * 3600 }
};

// Button press tracking
unsigned long global_button_down_time = 0;
constexpr unsigned long global_long_press_duration = 2000;

enum ButtonEvent {
  None,
  ShortPress,
  LongPress
};

ButtonEvent check_button_event() {
  static bool last_state = false;
  static unsigned long last_debounce_time = 0;
  static bool pressed_flag = false;

  const bool current_state = !digitalRead(PIN_BUTTON);
  const unsigned long now = millis();

  if (current_state != last_state) {
    last_debounce_time = now;
  }

  if ((now - last_debounce_time) > 50) {
    if (current_state && !pressed_flag) {
      global_button_down_time = now;
      pressed_flag = true;
    } else if (!current_state && pressed_flag) {
      pressed_flag = false;
      if ((now - global_button_down_time) >= global_long_press_duration) {
        return LongPress;
      } else {
        return ShortPress;
      }
    }
  }

  last_state = current_state;
  return None;
}

int seconds_since_midnight() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  return timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
}

bool is_schedule_on_now() {
  const int now = seconds_since_midnight();
  for (const auto& period : global_run_periods) {
    if (now >= period.start && now < period.end) return true;
  }
  return false;
}

void update_ntp_time() {
  global_leds.setPixelColor(0, global_leds.Color(64, 0, 64));
  global_leds.show();

  WiFi.begin(global_ssid, global_password);
  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    configTime(-8 * 3600, 3600, "pool.ntp.org");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) delay(100);
    WiFi.disconnect(true);
  }

  global_last_ntp_sync = time(nullptr);
}

float get_brightness_factor() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  const int hour = timeinfo.tm_hour;
  if (hour >= 22 || hour < 6) {
    return 0.25f;
  }
  return 1.0f;
}

void apply_state() {
  bool heater_on = false;
  uint32_t color = 0;

  if (global_program_cancelled) {
    color = COLOR_CANCELLED;
  } else if (global_schedule_on ^ global_manual_override) {
    heater_on = true;
    color = global_manual_override ? COLOR_MANUAL_ON : COLOR_SCHEDULED_ON;
  } else {
    color = global_manual_override ? COLOR_MANUAL_OFF : COLOR_SCHEDULED_OFF;
  }

  digitalWrite(PIN_RELAY1, heater_on ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY2, heater_on ? RELAY_ON : RELAY_OFF);

  const float brightness = global_program_cancelled ? 1.0F : get_brightness_factor();
  global_leds.setPixelColor(0, global_leds.Color(R(color) * brightness, G(color) * brightness, B(color) * brightness));
  global_leds.show();
}

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);

  global_leds.begin();
  global_leds.setBrightness(255);
  global_leds.clear();
  global_leds.show();

  global_leds.setPixelColor(0, global_leds.Color(64, 0, 64));
  global_leds.show();

  update_ntp_time();
}

void loop() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  if (timeinfo.tm_hour == global_RESYNC_HOUR && (time(nullptr) - global_last_ntp_sync) > 3600) {
    update_ntp_time();
  }

  const ButtonEvent evt = check_button_event();
  if (evt == ShortPress) {
    if (!global_program_cancelled) global_manual_override = !global_manual_override;
  } else if (evt == LongPress) {
    global_program_cancelled = !global_program_cancelled;
    global_manual_override = false;
  }

  static bool last_scheduled = false;
  const bool current_schedule = is_schedule_on_now();
  if (current_schedule != last_scheduled) {
    global_manual_override = false;
    global_schedule_on = current_schedule;
  }
  last_scheduled = current_schedule;

  if (!digitalRead(PIN_BUTTON)) {
    global_leds.setPixelColor(0, global_leds.Color(R(COLOR_PRESSED), G(COLOR_PRESSED), B(COLOR_PRESSED)));
    global_leds.show();
  } else {
    apply_state();
  }

  delay(20);
}
