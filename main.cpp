////////////////////////////////////////////////////////////////////////////////
// Thermostat Control Code
// written by Travis Llado
// last edited 2025-01-14
////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
#include "ws2812.h"
#include "llado_time.h"

////////////////////////////////////////////////////////////////////////////////

#define START_TIME  time_to_int(12U, 0U, 0U)  // time of day at boot

#define PIN_BUTTON  22U
#define PIN_LEDS    26U

#define COLOR_SCHEDULED_ON  brightness(COLOR_GREEN,   0.3F)
#define COLOR_SCHEDULED_OFF brightness(COLOR_RED,     0.5F)
#define COLOR_MANUAL_ON     brightness(COLOR_YELLOW,  0.4F)
#define COLOR_MANUAL_OFF    brightness(COLOR_ORANGE,  0.5F)


bool global_schedule_on{false};
bool global_manual_override{false};

struct RunPeriod {
  uint32_t start_time;
  uint32_t stop_time;
};

std::vector<RunPeriod> heater_run_times{
  {.start_time = time_to_int( 5U, 30U),
   .stop_time  = time_to_int( 8U,  0U)},
  {.start_time = time_to_int(17U,  0U),
   .stop_time  = time_to_int(22U,  0U)}
};

enum LogicType {
  direct,
  inverted
};

class Load {
public:
  Load(uint32_t pin_number, LogicType logic) : pin_number(pin_number), logic(logic) {
    init();
  }
  void init() {
    gpio_init(pin_number);
    gpio_set_dir(pin_number, GPIO_OUT);

    switch(logic) {
      case LogicType::direct:
        gpio_pull_down(pin_number);
        break;
      case LogicType::inverted:
        gpio_pull_up(pin_number);
        break;
      default:
        // do nothing
        break;
    }

    off();
  }
  void on() {
    gpio_put(pin_number, (logic == LogicType::direct));
  }
  void off() {
    gpio_put(pin_number, (logic == LogicType::inverted));
  }
  const uint32_t pin_number;
  const LogicType logic;
};

Load relay1(28U, LogicType::direct);
Load relay2(27U, LogicType::direct);

////////////////////////////////////////////////////////////////////////////////

void init_button(void) {
  gpio_init(PIN_BUTTON);
  gpio_set_dir(PIN_BUTTON, GPIO_IN);
  gpio_pull_up(PIN_BUTTON);
}

bool button_pressed_raw(void) {
  return !gpio_get(PIN_BUTTON);
}

bool button_pressed_debounced(const bool button_pressed_this_tick) {
  static bool button_pressed_last_tick{false};

  const bool output = (button_pressed_this_tick && !button_pressed_last_tick);
  button_pressed_last_tick = button_pressed_this_tick;

  return output;
}

uint32_t get_color_from_state() {
  uint32_t output{0U};

  if (global_schedule_on) {
    if (global_manual_override) {
      output = COLOR_MANUAL_OFF;
    } else {
      output = COLOR_SCHEDULED_ON;
    }
  } else {
    if (global_manual_override) {
      output = COLOR_MANUAL_ON;
    } else {
      output = COLOR_SCHEDULED_OFF;
    }
  }
  
  return output;
}

void heater_update(const uint32_t now) {
  static bool heaters_scheduled_on_last_cycle{false};

  bool heaters_scheduled_on_this_cycle{false};

  for (auto heater_run_time : heater_run_times) {
    if ((now >= heater_run_time.start_time) && (now < heater_run_time.stop_time)) {
      heaters_scheduled_on_this_cycle = true;
    }
  }

  if (heaters_scheduled_on_this_cycle != heaters_scheduled_on_last_cycle) {
    global_manual_override = false;
    global_schedule_on = heaters_scheduled_on_this_cycle;
  }

  if (global_schedule_on ^ global_manual_override) {
    relay1.on();
    relay2.on();
  } else {
    relay1.off();
    relay2.off();
  }

  heaters_scheduled_on_last_cycle = heaters_scheduled_on_this_cycle;
}

////////////////////////////////////////////////////////////////////////////////

int main() {
  // datetime_t must be initialized with year, month, and day or it will not run
  datetime_t now = {
    .year  = 0U,
    .month = 1U,
    .day   = 1U,
    .dotw  = 0U,
    .hour  = (START_TIME / HOUR),
    .min   = ((START_TIME % HOUR) / MINUTE),
    .sec   = (START_TIME % MINUTE)
  };

  rtc_init();
  rtc_set_datetime(&now);

  init_button();
  init_ws2812(PIN_LEDS);

  while (true) {
    rtc_get_datetime(&now);

    const bool button_pressed_now = button_pressed_raw();

    if (button_pressed_debounced(button_pressed_now)) {
      global_manual_override = !global_manual_override;
    }

    update_ws2812({button_pressed_now ? COLOR_WHITE : get_color_from_state()});
    heater_update(time_to_int(now.hour, now.min, now.sec));

    sleep_ms(100U);
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
