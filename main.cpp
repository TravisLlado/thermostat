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

////////////////////////////////////////////////////////////////////////////////

#define SECOND  (1U)
#define MINUTE  (60U*SECOND)
#define HOUR    (60U*MINUTE)

#define START_TIME  (16U*HOUR + 14U*MINUTE + 0U*SECOND)  // time of day at boot

#define PIN_BUTTON  22U
#define PIN_LEDS    26U

bool global_schedule_on{false};
bool global_manual_override{false};

struct RunPeriod {
  uint32_t start_time;
  uint32_t stop_time;
};

std::vector<RunPeriod> heater_run_times{
  {.start_time = 5U*HOUR + 30U*MINUTE,
   .stop_time  = 8U*HOUR},
  {.start_time = 17U*HOUR,
   .stop_time  = 22U*HOUR}
};

enum LogicType {direct, inverted};

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

  const bool output = (button_pressed_this_tick && !button_pressed_last_tick);\
  button_pressed_last_tick = button_pressed_this_tick;

  return output;
}

uint32_t get_color_from_state() {
  uint32_t output{0U};
  if (global_schedule_on) {
    if (global_manual_override) {
      output = COLOR_ORANGE;
    } else {
      output = COLOR_GREEN;
    }
  } else {
    if (global_manual_override) {
      output = COLOR_YELLOW;
    } else {
      output = COLOR_RED;
    }
  }
  return output;
}

void heater_update(uint32_t now) {
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

uint32_t time_of_day_sec(datetime_t& now) {
  return now.hour*HOUR + now.min*MINUTE + now.sec*SECOND;
}

////////////////////////////////////////////////////////////////////////////////

int main() {
  // datetime_t must be initialized with year, month, and day or it will not run
  datetime_t now = {
    .year  = 0U,
    .month = 1U,
    .day   = 1U,
    .dotw  = 0U,
    .hour  = (START_TIME/HOUR),
    .min   = ((START_TIME%HOUR)/MINUTE),
    .sec   = (START_TIME%MINUTE)
  };

  rtc_init();
  rtc_set_datetime(&now);
  uint32_t prev_sec{now.sec};

  init_button();
  init_ws2812(PIN_LEDS);

  while (true) {
    rtc_get_datetime(&now);

    const bool button_pressed_now = button_pressed_raw();

    if (button_pressed_debounced(button_pressed_now)) {
      global_manual_override = !global_manual_override;
    }

    update_ws2812({button_pressed_now ? COLOR_WHITE : get_color_from_state()});
    heater_update(time_of_day_sec(now));

    sleep_ms(100U);
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
