#include "mqtt.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/semphr.h>

#include <atomic>

#include "net.h"
#include "settings.h"
#include "state.h"
#include "version.h"
#include "weather.h"

namespace mqtt {
namespace {

const char* NVS_NS = "mqtt";
const char* BASE = "stove-monitor";
const char* T_STATUS = "stove-monitor/status";  // availability, retained
const char* T_STATE = "stove-monitor/state";
const char* T_SET = "stove-monitor/set/";        // + setting key
const char* T_SILENCE = "stove-monitor/silence";
const char* T_HA_STATUS = "homeassistant/status";  // HA's birth message
const uint32_t STATE_EVERY_MS = 10000;
const uint32_t STATE_MIN_GAP_MS = 1000;
const uint32_t BACKOFF_MIN_MS = 5000, BACKOFF_MAX_MS = 60000;
const uint16_t BUFFER = 2048;
const uint32_t CONNECT_AFTER_MS = 20000;     // let boot (and its weather fetch) settle
const size_t CONNECT_MIN_HEAP = 40000;       // internal RAM free before connecting
const uint32_t DISCOVERY_GAP_MS = 25;        // pace the discovery burst

SemaphoreHandle_t lock;
Status st;
char pass[65];  // guarded by lock; never logged or served
std::atomic<bool> reconfigure{false};

WiFiClient tcp;
PubSubClient client(tcp);
char devId[24];
char hostBuf[64];  // PubSubClient keeps a pointer to the host
bool sendDiscovery = false, sendState = false;
char rejected[96] = "";  // last setting Home Assistant sent that failed validation

// Every setting is exposed: 0/1 fields as switches, the rest as numbers.
struct Setting {
  const char* key;
  const char* name;
  bool toggle;
  const char* unit;
  bool absTemp;  // an absolute temperature, so HA may show it in °C
  bool config;   // listed under the device's Configuration section
  const char* icon;
};

const Setting SETTINGS[] = {
    {"stove_warn", "Stove warning", false, "°F", true, true, "mdi:thermometer-alert"},
    {"stove_crit", "Stove critical", false, "°F", true, true, "mdi:thermometer-alert"},
    {"flue_warn", "Flue warning", false, "°F", true, true, "mdi:thermometer-alert"},
    {"flue_crit", "Flue critical", false, "°F", true, true, "mdi:thermometer-alert"},
    {"flue_rate", "Chimney fire rise rate", false, "°F/min", false, true, "mdi:fire-alert"},
    {"flue_rate_min", "Chimney fire check above", false, "°F", true, true, "mdi:fire-alert"},
    {"reload_on", "Reload alert", true, nullptr, false, false, "mdi:bell-ring"},
    {"reload", "Reload temperature", false, "°F", true, false, "mdi:fire"},
    {"hyst", "Hysteresis", false, "°F", false, true, "mdi:arrow-expand-vertical"},
    {"ideal_on", "Ideal burn indicator", true, nullptr, false, true, "mdi:fire-circle"},
    {"ideal_lo", "Ideal stove from", false, "°F", true, true, "mdi:fire"},
    {"ideal_hi", "Ideal stove to", false, "°F", true, true, "mdi:fire"},
    {"ideal_exit", "Ideal chimney exit at least", false, "°F", true, true, "mdi:fire"},
    {"silence_min", "Silence warnings for", false, "min", false, true, "mdi:timer-outline"},
    {"silence_crit", "Silence critical for", false, "min", false, true, "mdi:timer-alert-outline"},
    {"quiet", "Quiet mode", true, nullptr, false, false, "mdi:bell-off"},
    {"units_c", "Screen shows °C", true, nullptr, false, true, "mdi:temperature-celsius"},
    {"brightness", "Screen brightness", false, nullptr, false, false, "mdi:brightness-6"},
    {"flip", "Flip screen", true, nullptr, false, true, "mdi:screen-rotation"},
};

void setConn(Conn c, const char* err = nullptr) {
  xSemaphoreTake(lock, portMAX_DELAY);
  st.conn = c;
  if (err) strlcpy(st.error, err, sizeof(st.error));
  xSemaphoreGive(lock);
}

const char* stateError(int rc) {
  switch (rc) {
    case MQTT_CONNECTION_TIMEOUT: return "broker stopped answering";
    case MQTT_CONNECTION_LOST: return "connection lost";
    case MQTT_CONNECT_FAILED: return "can't reach the broker";
    case MQTT_CONNECT_BAD_PROTOCOL: return "broker refused (protocol)";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "broker refused (client id)";
    case MQTT_CONNECT_UNAVAILABLE: return "broker unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "wrong username or password";
    case MQTT_CONNECT_UNAUTHORIZED: return "not authorised";
    default: return "disconnected";
  }
}

const char* levelSlug(state::Level l) {
  switch (l) {
    case state::Level::Reload: return "reload";
    case state::Level::Warning: return "warning";
    case state::Level::Fault: return "fault";
    case state::Level::Critical: return "critical";
    default: return "none";
  }
}

const char* onOff(bool b) { return b ? "ON" : "OFF"; }

bool publishJson(const char* topic, const JsonDocument& d, bool retain) {
  static char buf[BUFFER - 160];  // leave room for the topic and header
  size_t n = measureJson(d);
  if (n >= sizeof(buf)) {
    Serial.printf("mqtt: %s payload too big (%u)\n", topic, (unsigned)n);
    return false;
  }
  serializeJson(d, buf, sizeof(buf));
  return client.publish(topic, (const uint8_t*)buf, n, retain);
}

// ---- discovery

void entity(JsonDocument& d, const char* key, const char* name, bool hasState = true) {
  char uid[48];
  snprintf(uid, sizeof(uid), "%s_%s", devId, key);
  d["~"] = BASE;
  d["name"] = name;
  d["unique_id"] = uid;
  d["availability_topic"] = "~/status";
  if (hasState) d["state_topic"] = "~/state";
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"].add(devId);
  dev["name"] = "Stove Monitor";
  dev["manufacturer"] = "DIY";
  dev["model"] = "WT32-SC01 Plus";
  dev["sw_version"] = version::get();
  char url[32];
  snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
  dev["configuration_url"] = url;
}

void valueTemplate(JsonDocument& d, const char* path) {
  char vt[64];
  snprintf(vt, sizeof(vt), "{{ value_json.%s }}", path);
  d["value_template"] = vt;
}

void announce(const char* component, const char* key, const JsonDocument& d) {
  char topic[96];
  snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", component, devId, key);
  publishJson(topic, d, true);
  client.loop();
  vTaskDelay(pdMS_TO_TICKS(DISCOVERY_GAP_MS));
}

void sensor(const char* key, const char* name, const char* path, const char* unit,
            const char* devClass, const char* icon, bool measurement, bool diagnostic = false) {
  JsonDocument d;
  entity(d, key, name);
  valueTemplate(d, path);
  if (unit) d["unit_of_measurement"] = unit;
  if (devClass) d["device_class"] = devClass;
  if (icon) d["icon"] = icon;
  if (measurement) {
    d["state_class"] = "measurement";
    d["suggested_display_precision"] = 0;
  }
  if (diagnostic) d["entity_category"] = "diagnostic";
  announce("sensor", key, d);
}

void enumSensor(const char* key, const char* name, const char* path, const char* icon,
                std::initializer_list<const char*> options) {
  JsonDocument d;
  entity(d, key, name);
  valueTemplate(d, path);
  d["device_class"] = "enum";
  d["icon"] = icon;
  for (const char* o : options) d["options"].add(o);
  announce("sensor", key, d);
}

void binary(const char* key, const char* name, const char* path, const char* devClass,
            const char* icon) {
  JsonDocument d;
  entity(d, key, name);
  valueTemplate(d, path);
  if (devClass) d["device_class"] = devClass;
  if (icon) d["icon"] = icon;
  announce("binary_sensor", key, d);
}

void setting(const Setting& s) {
  JsonDocument d;
  entity(d, s.key, s.name);
  char path[40], cmd[48];
  snprintf(path, sizeof(path), "settings.%s", s.key);
  snprintf(cmd, sizeof(cmd), "~/set/%s", s.key);
  valueTemplate(d, path);
  d["command_topic"] = cmd;
  d["icon"] = s.icon;
  if (s.config) d["entity_category"] = "config";
  if (s.toggle) {
    d["state_on"] = "1";
    d["state_off"] = "0";
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    announce("switch", s.key, d);
    return;
  }
  long lo = 0, hi = 0;
  settings::range(s.key, &lo, &hi);
  d["min"] = lo;
  d["max"] = hi;
  d["step"] = 1;
  d["mode"] = strcmp(s.key, "brightness") ? "box" : "slider";
  if (s.unit) d["unit_of_measurement"] = s.unit;
  if (s.absTemp) d["device_class"] = "temperature";
  announce("number", s.key, d);
}

void announceAll() {
  sensor("stove_temp", "Stove temperature", "stove.temp", "°F", "temperature", nullptr, true);
  sensor("flue_temp", "Flue temperature", "flue.temp", "°F", "temperature", nullptr, true);
  sensor("stove_rate", "Stove rate", "stove.rate", "°F/min", nullptr, "mdi:chart-line", true);
  sensor("flue_rate", "Flue rate", "flue.rate", "°F/min", nullptr, "mdi:chart-line", true);
  sensor("outside_temp", "Outside temperature", "outside", "°F", "temperature", nullptr, true);
  enumSensor("alarm_level", "Alarm level", "alarm.level", "mdi:alarm-light",
             {"none", "reload", "warning", "fault", "critical"});
  enumSensor("stove_alarm", "Stove alarm", "alarm.stove", "mdi:thermometer-alert",
             {"none", "warning", "critical"});
  enumSensor("flue_alarm", "Flue alarm", "alarm.flue", "mdi:thermometer-alert",
             {"none", "warning", "critical"});
  sensor("silence_left", "Silence time left", "alarm.silence_left", "s", "duration",
         "mdi:timer-sand", false);
  sensor("rssi", "Wi-Fi signal", "rssi", "dBm", "signal_strength", nullptr, true, true);
  sensor("uptime", "Uptime", "uptime", "s", "duration", nullptr, false, true);
  sensor("reset_reason", "Last restart reason", "reset", nullptr, nullptr, "mdi:restart",
         false, true);
  sensor("rejected", "Last rejected setting", "rejected", nullptr, nullptr, "mdi:alert-circle",
         false, true);

  binary("chimney_fire", "Possible chimney fire", "alarm.chimney", "safety", nullptr);
  binary("stove_fault", "Stove probe fault", "alarm.stove_fault", "problem", nullptr);
  binary("flue_fault", "Flue probe fault", "alarm.flue_fault", "problem", nullptr);
  binary("reload_needed", "Reload needed", "alarm.reload", nullptr, "mdi:fire-alert");
  binary("ideal_burn", "Ideal burn", "alarm.ideal", nullptr, "mdi:fire");
  binary("silenced", "Alarm silenced", "alarm.silenced", nullptr, "mdi:volume-off");
  binary("sounding", "Buzzer sounding", "alarm.sounding", "sound", nullptr);

  for (const Setting& s : SETTINGS) setting(s);

  JsonDocument d;
  entity(d, "silence", "Silence alarm", false);
  d["command_topic"] = "~/silence";
  d["payload_press"] = "PRESS";
  d["icon"] = "mdi:volume-off";
  announce("button", "silence", d);
  Serial.println("mqtt: discovery sent");
}

// ---- state

// What changing should publish at once rather than on the 10s tick.
// Compared field by field: memcmp would also compare padding bytes, which
// copies don't preserve, and turn every check into a "change".
struct Sig {
  state::Level temp[thermo::COUNT];
  bool fault[thermo::COUNT];
  bool rateHigh, reload, ideal, silenced, audible;
  state::Level highest;
  settings::Settings cfg;
};

// Settings is all int32_t, so it has no padding and memcmp is exact.
static_assert(sizeof(settings::Settings) % sizeof(int32_t) == 0, "Settings must stay int32_t only");
bool sameSettings(const settings::Settings& a, const settings::Settings& b) {
  return memcmp(&a, &b, sizeof(a)) == 0;
}

bool operator==(const Sig& a, const Sig& b) {
  for (int i = 0; i < thermo::COUNT; i++) {
    if (a.temp[i] != b.temp[i] || a.fault[i] != b.fault[i]) return false;
  }
  return a.rateHigh == b.rateHigh && a.reload == b.reload && a.ideal == b.ideal &&
         a.silenced == b.silenced && a.audible == b.audible && a.highest == b.highest &&
         sameSettings(a.cfg, b.cfg);
}

Sig signature() {
  Sig g = {};
  state::Alarms a = state::get().alarms;
  for (int i = 0; i < thermo::COUNT; i++) {
    g.temp[i] = a.temp[i];
    g.fault[i] = a.fault[i];
  }
  g.rateHigh = a.rateHigh;
  g.reload = a.reload;
  g.ideal = a.ideal;
  g.silenced = a.silenced;
  g.audible = a.audible;
  g.highest = a.highest;
  g.cfg = settings::get();
  return g;
}

// Takes the member proxy itself: converting a missing member to JsonVariant
// gives an unbound variant, and setting that silently does nothing.
template <typename Slot>
void addNumber(Slot&& slot, float f, int decimals = 1) {
  if (isnan(f)) {
    slot = nullptr;
    return;
  }
  float p = decimals ? 10.0f : 1.0f;
  f = roundf(f * p) / p + 0.0f;  // + 0.0f turns -0 into 0
  slot = serialized(String(f, decimals));
}

void publishState() {
  state::Snapshot s = state::get();
  weather::Status w = weather::status();
  JsonDocument d;
  const char* names[thermo::COUNT] = {"stove", "flue"};
  for (int i = 0; i < thermo::COUNT; i++) {
    JsonObject c = d[names[i]].to<JsonObject>();
    addNumber(c["temp"], s.ch[i].valid ? s.ch[i].tempF : NAN);
    addNumber(c["rate"], s.ch[i].valid ? s.ch[i].rateFpm : NAN, 0);  // whole °F/min, like the screen
  }
  addNumber(d["outside"], w.valid ? w.tempF : NAN);
  const state::Alarms& a = s.alarms;
  JsonObject al = d["alarm"].to<JsonObject>();
  al["level"] = levelSlug(a.highest);
  al["stove"] = levelSlug(a.temp[thermo::STOVE]);
  al["flue"] = levelSlug(a.temp[thermo::FLUE]);
  al["chimney"] = onOff(a.rateHigh);
  al["stove_fault"] = onOff(a.fault[thermo::STOVE]);
  al["flue_fault"] = onOff(a.fault[thermo::FLUE]);
  al["reload"] = onOff(a.reload);
  al["ideal"] = onOff(a.ideal);
  al["silenced"] = onOff(a.silenced);
  al["silence_left"] = a.silenceLeftS;
  al["sounding"] = onOff(a.audible);
  JsonObject g = d["settings"].to<JsonObject>();
  settings::forEach(
      settings::get(),
      [](const char* k, long v, long, long, void* ctx) { (*static_cast<JsonObject*>(ctx))[k] = v; },
      &g);
  d["rssi"] = WiFi.RSSI();
  d["uptime"] = millis() / 1000;
  d["reset"] = state::resetReason();
  d["rejected"] = rejected[0] ? rejected : "none";
  if (publishJson(T_STATE, d, false)) {
    xSemaphoreTake(lock, portMAX_DELAY);
    st.stateSent++;
    xSemaphoreGive(lock);
  }
}

// ---- commands

void onMessage(char* topic, byte* payload, unsigned int len) {
  char val[24];
  len = min(len, (unsigned int)sizeof(val) - 1);
  memcpy(val, payload, len);
  val[len] = '\0';

  if (!strcmp(topic, T_HA_STATUS)) {
    if (!strcmp(val, "online")) sendDiscovery = sendState = true;  // HA restarted
    return;
  }
  if (!strcmp(topic, T_SILENCE)) {
    Serial.println("mqtt: silence from Home Assistant");
    state::requestSilence();
    return;
  }
  size_t pre = strlen(T_SET);
  if (strncmp(topic, T_SET, pre)) return;
  const char* key = topic + pre;

  long v;
  char err[64] = "";
  if (!strcmp(val, "ON")) {
    v = 1;
  } else if (!strcmp(val, "OFF")) {
    v = 0;
  } else {
    char* end = nullptr;
    float f = strtof(val, &end);
    if (end == val || *end || isnan(f)) {
      snprintf(err, sizeof(err), "not a number");
    }
    v = lroundf(f);
  }
  if (!err[0] && settings::setByName(key, v, err, sizeof(err))) {
    Serial.printf("mqtt: %s = %ld from Home Assistant\n", key, v);
    rejected[0] = '\0';
  } else {
    Serial.printf("mqtt: rejected %s = %s: %s\n", key, val, err);
    snprintf(rejected, sizeof(rejected), "%s = %s: %s", key, val, err);
  }
  sendState = true;  // also snaps HA back to the real value after a rejection
}

// ---- connection

bool tryConnect(uint16_t port, const char* user, const char* pw) {
  client.setServer(hostBuf, port);
  Serial.printf("mqtt: connecting to %s:%u as '%s'\n", hostBuf, port, user);  // never the password
  bool ok = client.connect(devId, user[0] ? user : nullptr, user[0] ? pw : nullptr, T_STATUS, 1,
                           true, "offline");
  if (!ok) return false;
  client.publish(T_STATUS, "online", true);
  char sub[48];
  snprintf(sub, sizeof(sub), "%s+", T_SET);
  client.subscribe(sub);
  client.subscribe(T_SILENCE);
  client.subscribe(T_HA_STATUS);
  sendDiscovery = sendState = true;
  return true;
}

void task(void*) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(devId, sizeof(devId), "stove_monitor_%02x%02x%02x", mac[3], mac[4], mac[5]);
  client.setBufferSize(BUFFER);
  client.setKeepAlive(15);    // broker marks us offline ~20s after a silent drop
  client.setSocketTimeout(5);
  client.setCallback(onMessage);

  uint32_t backoff = BACKOFF_MIN_MS, nextTry = 0, lastState = 0;
  Sig last = {};
  for (;;) {
    uint32_t now = millis();
    if (reconfigure.exchange(false)) {
      if (client.connected()) client.disconnect();
      backoff = BACKOFF_MIN_MS;
      nextTry = now;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    bool configured = st.host[0] != '\0';
    Conn conn = st.conn;
    xSemaphoreGive(lock);

    if (!configured) {
      if (client.connected()) client.disconnect();
      setConn(Conn::Off, "");
    } else if (net::status().link != net::Link::Connected) {
      if (client.connected()) client.disconnect();
      if (conn != Conn::WaitingForWiFi) setConn(Conn::WaitingForWiFi);
      nextTry = now;
    } else if (!client.connected()) {
      if (conn == Conn::Connected) {
        Serial.printf("mqtt: %s\n", stateError(client.state()));
        setConn(Conn::Failed, stateError(client.state()));
        nextTry = now + 1000;  // quick first retry after a drop
      } else if ((int32_t)(now - nextTry) >= 0 && now >= CONNECT_AFTER_MS &&
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= CONNECT_MIN_HEAP) {
        char user[65], pw[65];
        uint16_t port;
        xSemaphoreTake(lock, portMAX_DELAY);
        strlcpy(hostBuf, st.host, sizeof(hostBuf));
        strlcpy(user, st.user, sizeof(user));
        strlcpy(pw, pass, sizeof(pw));
        port = st.port;
        xSemaphoreGive(lock);
        setConn(Conn::Connecting);
        net::heavyBegin();
        bool ok = tryConnect(port, user, pw);
        memset(pw, 0, sizeof(pw));
        if (ok) {
          announceAll();  // the burst, while holding the heavy lock
          sendDiscovery = false;
        }
        net::heavyEnd();
        if (ok) {
          Serial.println("mqtt: connected");
          setConn(Conn::Connected, "");
          backoff = BACKOFF_MIN_MS;
        } else {
          const char* e = stateError(client.state());
          Serial.printf("mqtt: %s, retry in %lus\n", e, (unsigned long)backoff / 1000);
          setConn(Conn::Failed, e);
          nextTry = millis() + backoff;
          backoff = min(backoff * 2, BACKOFF_MAX_MS);
        }
      }
    } else {
      client.loop();
      if (sendDiscovery) {  // HA restarted
        sendDiscovery = false;
        net::heavyBegin();
        announceAll();
        net::heavyEnd();
      }
      Sig now_sig = signature();
      uint32_t since = millis() - lastState;
      // Never more than one state message a second, whatever changes.
      if (since >= STATE_MIN_GAP_MS &&
          (sendState || !(now_sig == last) || since >= STATE_EVERY_MS)) {
        sendState = false;
        last = now_sig;
        lastState = millis();
        publishState();
      }
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    st.retryInS = st.conn == Conn::Failed && (int32_t)(nextTry - now) > 0
                      ? (nextTry - now) / 1000 + 1 : 0;
    xSemaphoreGive(lock);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

void start() {
  lock = xSemaphoreCreateMutex();
  st = {};
  Preferences p;
  p.begin(NVS_NS, true);
  strlcpy(st.host, p.getString("host", "").c_str(), sizeof(st.host));
  st.port = p.getUShort("port", 1883);
  strlcpy(st.user, p.getString("user", "").c_str(), sizeof(st.user));
  strlcpy(pass, p.getString("pass", "").c_str(), sizeof(pass));
  p.end();
  st.hasPass = pass[0] != '\0';
  st.conn = Conn::Off;
  xTaskCreatePinnedToCore(task, "mqtt", 6144, nullptr, 1, nullptr, 0);
}

Status status() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Status s = st;
  xSemaphoreGive(lock);
  return s;
}

const char* connName(Conn c) {
  switch (c) {
    case Conn::Off: return "off";
    case Conn::WaitingForWiFi: return "waiting for Wi-Fi";
    case Conn::Connecting: return "connecting";
    case Conn::Connected: return "connected";
    case Conn::Failed: return "failed";
  }
  return "?";
}

bool configure(const char* host, uint16_t port, const char* user, const char* pw) {
  size_t hl = strlen(host);
  if (hl == 0 || hl >= sizeof(st.host) || strchr(host, ' ') || port == 0) return false;
  if (strlen(user) >= sizeof(st.user) || strlen(pw) >= sizeof(pass)) return false;
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("host", host);
  p.putUShort("port", port);
  p.putString("user", user);
  if (pw[0]) p.putString("pass", pw);
  p.end();
  xSemaphoreTake(lock, portMAX_DELAY);
  strlcpy(st.host, host, sizeof(st.host));
  st.port = port;
  strlcpy(st.user, user, sizeof(st.user));
  if (pw[0]) strlcpy(pass, pw, sizeof(pass));
  st.hasPass = pass[0] != '\0';
  st.error[0] = '\0';
  xSemaphoreGive(lock);
  reconfigure = true;
  Serial.printf("mqtt: broker set to %s:%u\n", host, port);
  return true;
}

void clear() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
  xSemaphoreTake(lock, portMAX_DELAY);
  st.host[0] = st.user[0] = st.error[0] = '\0';
  st.port = 1883;
  memset(pass, 0, sizeof(pass));
  st.hasPass = false;
  xSemaphoreGive(lock);
  reconfigure = true;
  Serial.println("mqtt: broker cleared, MQTT off");
}

}  // namespace mqtt
