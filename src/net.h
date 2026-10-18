// Network task: Wi-Fi station management with reconnect backoff, network
// scanning for the setup screen, and credentials stored in NVS. Runs on core
// 0 at low priority and never touches the sensor or alarm paths - safety
// works the same with Wi-Fi down.
#pragma once

#include <stdint.h>

#include <vector>

#include <Arduino.h>

namespace net {

enum class Link : uint8_t { NotSetUp, Connecting, Connected, Disconnected };

struct Status {
  Link link;
  char ssid[33];
  char ip[16];
  int8_t rssi;           // dBm, 0 when not connected
  uint32_t retryInS;     // seconds until the next attempt when Disconnected
};

struct Network {
  String ssid;
  int8_t rssi;
  bool secured;
};

void start();
Status status();

// Setup screen API (called from the UI task).
void requestScan();
bool scanning();
std::vector<Network> scanResults();  // strongest first, duplicates merged
void connect(const char* ssid, const char* password);  // saves on success path
void forget();                                         // clears credentials

// Memory-heavy network work (the weather TLS fetch, the MQTT connect and
// discovery burst) takes turns under this lock: run together at boot they
// exhausted internal RAM and took Wi-Fi down. Blocks until free.
void heavyBegin();
void heavyEnd();

}  // namespace net
