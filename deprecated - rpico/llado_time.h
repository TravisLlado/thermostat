#define SECOND  (1U)
#define MINUTE  (60U*SECOND)
#define HOUR    (60U*MINUTE)

uint32_t time_to_int(const uint32_t hours, const uint32_t minutes, const uint32_t seconds) {
  return hours*HOUR + minutes*MINUTE + seconds*SECOND;
}

uint32_t time_to_int(const uint32_t hours, const uint32_t minutes) {
  return hours*HOUR + minutes*MINUTE;
}

uint32_t time_to_int(const uint32_t hours) {
  return hours*HOUR;
}
