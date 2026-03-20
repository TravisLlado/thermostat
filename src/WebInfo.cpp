#include "WebInfo.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace WebInfo {

bool fetchGeo(float& lat, float& lon, int& tzOffsetMinutes) {
  HTTPClient http;
  if (!http.begin("http://ip-api.com/line/?fields=status,lat,lon,offset")) return false;

  const int code{http.GET()};
  if (code != 200) { http.end(); return false; }

  String body{http.getString()};
  http.end();

  const int firstNL{body.indexOf('\n')};
  if (firstNL <= 0) return false;

  String status{body.substring(0, firstNL)};
  status.trim();
  if (status != "success") return false;

  const int secondNL{body.indexOf('\n', firstNL + 1)};
  if (secondNL <= firstNL) return false;

  const int thirdNL{body.indexOf('\n', secondNL + 1)};
  if (thirdNL <= secondNL) return false;

  String latStr{body.substring(firstNL + 1, secondNL)};
  String lonStr{body.substring(secondNL + 1, thirdNL)};
  String offsetStr{body.substring(thirdNL + 1)};
  latStr.trim();
  lonStr.trim();
  offsetStr.trim();

  const float parsedLat{latStr.toFloat()};
  const float parsedLon{lonStr.toFloat()};
  if (isnan(parsedLat) || isnan(parsedLon)) return false;

  lat = parsedLat;
  lon = parsedLon;
  tzOffsetMinutes = offsetStr.toInt() / 60;
  return true;
}

bool initTime(int tzOffsetMinutes) {
  configTime((long)tzOffsetMinutes * 60, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  for (int i{0}; i < 10; ++i) {
    if (getLocalTime(&timeinfo)) return true;
    delay(500);
  }
  return false;
}

bool fetchHourlyForecast(float lat, float lon, float temps[24]) {
  String url{"https://api.open-meteo.com/v1/forecast?latitude="};
  url += String(lat, 4);
  url += "&longitude=";
  url += String(lon, 4);
  url += "&hourly=temperature_2m&temperature_unit=fahrenheit&forecast_days=1";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) return false;

  const int code{http.GET()};
  if (code != 200) { http.end(); return false; }

  String body{http.getString()};
  http.end();

  // Find the temperature_2m array in the JSON response
  int idx{body.indexOf("\"temperature_2m\":[")};
  if (idx < 0) return false;
  idx += 18; // skip past "temperature_2m":[

  for (int i = 0; i < 24; i++) {
    int comma{body.indexOf(',', idx)};
    int bracket{body.indexOf(']', idx)};
    int end = (comma >= 0 && comma < bracket) ? comma : bracket;
    if (end < 0) return false;

    String val{body.substring(idx, end)};
    val.trim();
    temps[i] = val.toFloat();
    idx = end + 1;
  }

  return true;
}

String formatLocalTime(int seconds) {
  if (seconds < 0) return String("??:??:??");
  seconds %= 86400;
  const int h{seconds / 3600};
  const int m{(seconds % 3600) / 60};
  const int s{seconds % 60};
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

}  // namespace WebInfo
