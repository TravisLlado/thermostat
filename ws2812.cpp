#include <vector>
#include "WS2812.pio.h"

void init_ws2812(const uint32_t pin_num) {
  uint offset = pio_add_program(pio0, &ws2812_program);
  ws2812_program_init(pio0, 0, offset, pin_num, 800000, 24);
}

void update_ws2812(std::vector<uint32_t> led_values) {
  for (auto led : led_values) {
    pio_sm_put_blocking(pio0, 0, led);
  }
}

uint32_t brightness(const uint32_t color, const float level) {
  if ((level > 1.0F) || (level < 0.0F)) {
    return 0U;
  }

  const uint8_t g = (color & 0xFF000000) >> 24U;
  const uint8_t r = (color & 0x00FF0000) >> 16U;
  const uint8_t b = (color & 0x0000FF00) >> 8U;

  const uint8_t g2 = g * level;
  const uint8_t r2 = r * level;
  const uint8_t b2 = b * level;

  const uint32_t new_color = (g2 << 24U) + (r2 << 16U) + (b2 << 8U);

  return new_color;
}

uint32_t blend(const uint32_t from, const uint32_t to, const float level) {
  if ((level > 1.0F) || level < 0.0F) {
    return 0U;
  }

  return brightness(from, (1.0F - level)) + brightness(to, level);
}
