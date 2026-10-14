#include "weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>

#include <atomic>

#include "net.h"
#include "weather_ca.h"

namespace weather {
namespace {

const char* NVS_NS = "weather";
const uint32_t REFRESH_MS = 15 * 60 * 1000;
const uint32_t RETRY_MIN_MS = 60 * 1000;   // after a failure, doubling...
const uint32_t STALE_S = 60 * 60;          // ...and the reading expires after an hour
const uint32_t HTTP_TIMEOUT_MS = 10000;

SemaphoreHandle_t lock;
Status st;
uint32_t okMs = 0;  // millis() of the last good fetch
std::atomic<bool> refresh{false};

Sky skyFromWmo(int code) {
  if (code < 0) return Sky::Unknown;
  if (code <= 1) return Sky::Clear;  // clear, mainly clear
  if (code <= 3) return Sky::Cloudy;  // partly cloudy, overcast
  if (code == 45 || code == 48) return Sky::Fog;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if (code >= 95) return Sky::Storm;
  if (code >= 51) return Sky::Rain;  // drizzle, rain, freezing rain, showers
  return Sky::Unknown;
}

// "2026-10-13T07:00" -> "07:00"
void timeOfDay(const char* iso, char* out) {
  const char* t = iso ? strchr(iso, 'T') : nullptr;
  if (t && strlen(t + 1) >= 5) strlcpy(out, t + 1, 6);
  else out[0] = '\0';
}

void setError(const char* msg) {
  xSemaphoreTake(lock, portMAX_DELAY);
  strlcpy(st.error, msg, sizeof(st.error));
  xSemaphoreGive(lock);
  Serial.printf("weather: %s\n", msg);
}

bool fetch(float lat, float lon) {
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,is_day&daily=sunrise,sunset"
           "&timezone=auto&forecast_days=1&temperature_unit=fahrenheit",
           lat, lon);
  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1);
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("stove-monitor");
  if (!http.begin(client, url)) {
    setError("bad request");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char err[48];
    if (code < 0) snprintf(err, sizeof(err), "%s", HTTPClient::errorToString(code).c_str());
    else snprintf(err, sizeof(err), "HTTP %d", code);
    http.end();
    setError(err);
    return false;
  }
  String body = http.getString();  // handles chunked replies; ~600 bytes
  http.end();
  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, body);
  if (je) {
    setError("bad response");
    return false;
  }
  float t = doc["current"]["temperature_2m"] | NAN;
  if (isnan(t)) {
    setError("no temperature in response");
    return false;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  st.tempF = t;
  st.sky = skyFromWmo(doc["current"]["weather_code"] | -1);
  st.isDay = (doc["current"]["is_day"] | 1) != 0;
  timeOfDay(doc["daily"]["sunrise"][0], st.sunrise);
  timeOfDay(doc["daily"]["sunset"][0], st.sunset);
  st.error[0] = '\0';
  okMs = millis();
  xSemaphoreGive(lock);
  Serial.printf("weather: %.1fF %s\n", t, skyName(st.sky));
  return true;
}

void task(void*) {
  uint32_t nextAt = 0, retry = RETRY_MIN_MS;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t now = millis();
    if (refresh.exchange(false)) {
      nextAt = now;
      retry = RETRY_MIN_MS;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool located = st.located;
    float lat = st.lat, lon = st.lon;
    xSemaphoreGive(lock);
    if (!located || net::status().link != net::Link::Connected) continue;
    if ((int32_t)(now - nextAt) < 0) continue;
    if (fetch(lat, lon)) {
      nextAt = millis() + REFRESH_MS;
      retry = RETRY_MIN_MS;
    } else {
      nextAt = millis() + retry;
      retry = min(retry * 2, REFRESH_MS);
    }
  }
}

}  // namespace

void start() {
  lock = xSemaphoreCreateMutex();
  st = {};
  st.tempF = NAN;
  Preferences p;
  p.begin(NVS_NS, true);
  st.lat = p.getFloat("lat", NAN);
  st.lon = p.getFloat("lon", NAN);
  p.end();
  st.located = !isnan(st.lat) && !isnan(st.lon);
  // TLS needs a deep stack; priority 1 keeps it below the net task.
  xTaskCreatePinnedToCore(task, "weather", 10240, nullptr, 1, nullptr, 0);
}

Status status() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Status s = st;
  uint32_t age = okMs ? (millis() - okMs) / 1000 : 0;
  xSemaphoreGive(lock);
  s.ageS = age;
  s.valid = okMs && age < STALE_S && s.located;
  if (!s.valid) s.tempF = NAN;
  return s;
}

const char* skyName(Sky s) {
  switch (s) {
    case Sky::Clear: return "Clear";
    case Sky::Cloudy: return "Cloudy";
    case Sky::Fog: return "Fog";
    case Sky::Rain: return "Rain";
    case Sky::Snow: return "Snow";
    case Sky::Storm: return "Thunderstorm";
    default: return "";
  }
}

bool setLocation(float lat, float lon) {
  if (isnan(lat) || isnan(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
  Preferences p;
  p.begin(NVS_NS, false);
  p.putFloat("lat", lat);
  p.putFloat("lon", lon);
  p.end();
  xSemaphoreTake(lock, portMAX_DELAY);
  st.lat = lat;
  st.lon = lon;
  st.located = true;
  okMs = 0;  // old reading was for somewhere else
  st.error[0] = '\0';
  xSemaphoreGive(lock);
  refresh = true;
  return true;
}

void clearLocation() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
  xSemaphoreTake(lock, portMAX_DELAY);
  st.lat = st.lon = NAN;
  st.located = false;
  okMs = 0;
  st.error[0] = '\0';
  xSemaphoreGive(lock);
}

}  // namespace weather
