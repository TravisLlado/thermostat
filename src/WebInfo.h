#pragma once

#include <Arduino.h>

namespace WebInfo {

  // Fetch approximate geolocation and current UTC offset from the device's public IP.
  // Returns true on success; fills lat, lon in decimal degrees, tzOffsetMinutes as the
  // current UTC offset in minutes (DST-aware, e.g. -480 for PST, -420 for PDT).
  bool fetchGeo(float& lat, float& lon, int& tzOffsetMinutes);

  // Configure the ESP32 system clock via NTP using the given UTC offset.
  // Returns true if NTP sync succeeded.
  bool initTime(int tzOffsetMinutes);

  // Fetch today's hourly temperature forecast (24 values, °F) for the given location.
  // Uses the Open-Meteo API (free, no API key required).
  // Returns true on success; fills temps[0..23] with hourly temperatures.
  bool fetchHourlyForecast(float lat, float lon, float temps[24]);

  // Format a local seconds-since-midnight value as "HH:MM:SS".
  // Returns "??:??:??" for negative input.
  String formatLocalTime(int seconds);

}  // namespace WebInfo
