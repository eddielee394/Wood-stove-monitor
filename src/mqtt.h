// Home Assistant over MQTT, in its own low-priority task on core 0. Publishes
// discovery configs (homeassistant/...), live state and availability, and
// takes commands: every setting (through settings::apply, like the
// touchscreen) and Silence. Off until a broker is set on the web page; the
// broker password lives only in NVS and is never logged or served back.
// Safety never depends on it - alarms run the same with no broker.
#pragma once

#include <stdint.h>

namespace mqtt {

enum class Conn : uint8_t { Off, WaitingForWiFi, Connecting, Connected, Failed };

struct Status {
  Conn conn;
  char host[64];
  uint16_t port;
  char user[65];
  bool hasPass;
  char error[48];      // last connection error, "" once connected
  uint32_t retryInS;   // seconds until the next attempt when Failed
  uint32_t stateSent;  // state messages published since boot
};

void start();
Status status();
const char* connName(Conn c);

// Save the broker and reconnect. An empty pass keeps the stored one.
// Returns false (and leaves everything unchanged) for a bad host or port.
bool configure(const char* host, uint16_t port, const char* user, const char* pass);
void clear();  // forget the broker; MQTT off

}  // namespace mqtt
