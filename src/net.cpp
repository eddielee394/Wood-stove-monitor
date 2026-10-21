#include "net.h"

#include <Preferences.h>
#include <WiFi.h>
#include <freertos/semphr.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>

#include <algorithm>
#include <atomic>

#include "web.h"

namespace net {
namespace {

const char* NVS_NS = "wifi";
const char* HOSTNAME = "stove-monitor";
const uint32_t CONNECT_TIMEOUT_MS = 20000;
const uint32_t BACKOFF_MIN_MS = 5000, BACKOFF_MAX_MS = 60000;
// Wi-Fi can stay "connected" while no traffic gets through (seen after
// internal RAM ran out, 2026-10-21). Ping the router; if it stops answering,
// reconnect, and after a couple of tries let main.cpp restart the board.
const uint32_t PING_EVERY_MS = 30000;
const uint32_t STUCK_RECONNECT_MS = 180000;
const int STUCK_RECONNECTS = 2;

SemaphoreHandle_t lock;
SemaphoreHandle_t heavy;
Status st;
std::vector<Network> results;
bool scanRequested = false, scanBusy = false;

// Pending request from the UI; consumed by the task.
bool connectRequested = false, forgetRequested = false;
String reqSsid, reqPass;

String ssid, pass;  // task-owned copy of the saved credentials

esp_ping_handle_t pinger = nullptr;
uint32_t pingTarget = 0;
std::atomic<bool> gatewayAnswers{false};  // only watch a router that answers pings
std::atomic<uint32_t> lastReply{0};

// Time since the router last answered (or since `since`, if later). A reply
// can land after `now` was read; treat that as 0, not a wrapped huge gap.
uint32_t silentFor(uint32_t now, uint32_t since = 0) {
  int32_t d = (int32_t)(now - std::max<uint32_t>(lastReply, since));
  return d > 0 ? d : 0;
}

void onPingReply(esp_ping_handle_t, void*) {
  lastReply = millis();
  gatewayAnswers = true;
}

// One ping every 30s to the router, for as long as the board runs.
void startPinger() {
  IPAddress gw = WiFi.gatewayIP();
  uint32_t target = gw;
  if (!target || target == pingTarget) return;
  if (pinger) {
    esp_ping_stop(pinger);
    esp_ping_delete_session(pinger);
    pinger = nullptr;
  }
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = PING_EVERY_MS;
  cfg.timeout_ms = 2000;
  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = onPingReply;
  if (esp_ping_new_session(&cfg, &cbs, &pinger) == ESP_OK) {
    esp_ping_start(pinger);
    pingTarget = target;
  } else {
    pinger = nullptr;
  }
}

void setLink(Link l) {
  xSemaphoreTake(lock, portMAX_DELAY);
  st.link = l;
  xSemaphoreGive(lock);
}

void loadCredentials() {
  Preferences p;
  p.begin(NVS_NS, true);
  ssid = p.getString("ssid", "");
  pass = p.getString("pass", "");
  p.end();
}

void saveCredentials() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
}

void publish() {
  xSemaphoreTake(lock, portMAX_DELAY);
  strlcpy(st.ssid, ssid.c_str(), sizeof(st.ssid));
  if (st.link == Link::Connected) {
    strlcpy(st.ip, WiFi.localIP().toString().c_str(), sizeof(st.ip));
    st.rssi = WiFi.RSSI();
  } else {
    st.ip[0] = '\0';
    st.rssi = 0;
  }
  xSemaphoreGive(lock);
}

void beginConnect() {
  Serial.printf("net: connecting to '%s'\n", ssid.c_str());  // never the password
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  setLink(Link::Connecting);
}

void collectScan(int n) {
  std::vector<Network> found;
  for (int i = 0; i < n; i++) {
    String name = WiFi.SSID(i);
    if (name.isEmpty()) continue;  // hidden networks
    auto it = std::find_if(found.begin(), found.end(),
                           [&](const Network& x) { return x.ssid == name; });
    int8_t rssi = WiFi.RSSI(i);
    if (it != found.end()) {
      it->rssi = std::max(it->rssi, rssi);
    } else {
      found.push_back({name, rssi, WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
  }
  WiFi.scanDelete();
  std::sort(found.begin(), found.end(),
            [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  xSemaphoreTake(lock, portMAX_DELAY);
  results = found;
  scanBusy = false;
  xSemaphoreGive(lock);
}

void task(void*) {
  WiFi.persistent(false);  // we keep credentials ourselves
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);    // the old device's flaky Wi-Fi; favour reliability
  WiFi.setAutoReconnect(false);

  loadCredentials();
  uint32_t backoff = BACKOFF_MIN_MS, attemptAt = 0, nextTry = 0, connectedAt = 0;
  uint32_t seenReply = 0;
  int stuckReconnects = 0;
  bool wasConnected = false;
  if (ssid.length()) {
    beginConnect();
    attemptAt = millis();
  } else {
    setLink(Link::NotSetUp);
  }

  for (;;) {
    uint32_t now = millis();

    // Requests from the setup screen
    xSemaphoreTake(lock, portMAX_DELAY);
    bool doForget = forgetRequested, doConnect = connectRequested, doScan = scanRequested;
    forgetRequested = connectRequested = scanRequested = false;
    String newSsid = reqSsid, newPass = reqPass;
    reqPass = "";  // don't keep a second copy around
    xSemaphoreGive(lock);

    if (doForget) {
      ssid = pass = "";
      saveCredentials();
      WiFi.disconnect(false, true);
      setLink(Link::NotSetUp);
      wasConnected = false;
    }
    if (doConnect) {
      ssid = newSsid;
      pass = newPass;
      saveCredentials();
      backoff = BACKOFF_MIN_MS;
      beginConnect();
      attemptAt = now;
      wasConnected = false;
    }
    if (doScan) {
      // A scan can't run alongside a connection attempt; pause it.
      if (status().link == Link::Connecting) WiFi.disconnect(false, false);
      WiFi.scanNetworks(true);
    }
    int sc = WiFi.scanComplete();
    if (sc >= 0) collectScan(sc);
    else if (sc == WIFI_SCAN_FAILED && scanning()) collectScan(0);

    // Connection state machine with backoff
    Link link = status().link;
    bool up = WiFi.status() == WL_CONNECTED;
    if (ssid.length() && !scanning()) {
      if (up && link != Link::Connected) {
        setLink(Link::Connected);
        backoff = BACKOFF_MIN_MS;
        wasConnected = true;
        Serial.printf("net: connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        web::begin();  // idempotent; keeps serving across reconnects
        configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC clock, for TLS dates
        startPinger();
        connectedAt = now;
      } else if (up && link == Link::Connected && gatewayAnswers &&
                 silentFor(now, connectedAt) > STUCK_RECONNECT_MS &&
                 stuckReconnects < STUCK_RECONNECTS) {
        Serial.println("net: router stopped answering, reconnecting Wi-Fi");
        stuckReconnects++;
        WiFi.disconnect(false, false);
        setLink(Link::Disconnected);
        nextTry = now + 1000;
      } else if (!up && link == Link::Connected) {
        Serial.println("net: connection lost");
        setLink(Link::Disconnected);
        nextTry = now + (wasConnected ? 1000 : backoff);  // quick first retry after a drop
      } else if (link == Link::Connecting && now - attemptAt > CONNECT_TIMEOUT_MS) {
        Serial.printf("net: connect timed out, retry in %lus\n", (unsigned long)backoff / 1000);
        WiFi.disconnect(false, false);
        setLink(Link::Disconnected);
        nextTry = now + backoff;
        backoff = std::min(backoff * 2, BACKOFF_MAX_MS);
      } else if (link == Link::Disconnected && (int32_t)(now - nextTry) >= 0) {
        beginConnect();
        attemptAt = now;
      }
    }

    if (lastReply != seenReply) {
      seenReply = lastReply;
      stuckReconnects = 0;
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    st.retryInS = st.link == Link::Disconnected && (int32_t)(nextTry - now) > 0
                      ? (nextTry - now) / 1000 + 1 : 0;
    xSemaphoreGive(lock);
    publish();
    web::handle();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

uint32_t stuckMs() {
  if (!gatewayAnswers || status().link != Link::Connected) return 0;
  return silentFor(millis());
}

void heavyBegin() { xSemaphoreTake(heavy, portMAX_DELAY); }
void heavyEnd() { xSemaphoreGive(heavy); }

void start() {
  lock = xSemaphoreCreateMutex();
  heavy = xSemaphoreCreateMutex();
  st = {};
  xTaskCreatePinnedToCore(task, "net", 6144, nullptr, 2, nullptr, 0);
}

Status status() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Status s = st;
  xSemaphoreGive(lock);
  return s;
}

void requestScan() {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!scanBusy) {
    scanBusy = scanRequested = true;
    results.clear();
  }
  xSemaphoreGive(lock);
}

bool scanning() {
  xSemaphoreTake(lock, portMAX_DELAY);
  bool b = scanBusy;
  xSemaphoreGive(lock);
  return b;
}

std::vector<Network> scanResults() {
  xSemaphoreTake(lock, portMAX_DELAY);
  std::vector<Network> r = results;
  xSemaphoreGive(lock);
  return r;
}

void connect(const char* s, const char* p) {
  xSemaphoreTake(lock, portMAX_DELAY);
  reqSsid = s;
  reqPass = p;
  connectRequested = true;
  xSemaphoreGive(lock);
}

void forget() {
  xSemaphoreTake(lock, portMAX_DELAY);
  forgetRequested = true;
  xSemaphoreGive(lock);
}

}  // namespace net
