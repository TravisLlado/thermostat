// GRB format, oddly enough
#define COLOR_WHITE   0xFFFFFF00
#define COLOR_GREEN   0xFF000000
#define COLOR_RED     0x00FF0000
#define COLOR_BLUE    0x0000FF00
#define COLOR_YELLOW  0xFFFF0000
#define COLOR_CYAN    0xFF00FF00
#define COLOR_MAGENTA 0x00FFFF00
#define COLOR_ORANGE  0x60FF0000
#define COLOR_OFF     0x00000000

void init_ws2812(const uint32_t pin_num);

void update_ws2812(std::vector<uint32_t> led_values);

uint32_t brightness(const uint32_t color, const float level);

uint32_t blend(const uint32_t from, const uint32_t to, const float level);
