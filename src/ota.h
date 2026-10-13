// Firmware updates over Wi-Fi via HTTP: POST the firmware .bin to /update
// (see web.cpp) with HTTP Basic auth, user "ota". TCP-only on purpose - UDP
// (ArduinoOTA/espota) is blocked on Eddie's Mac. Disabled until a password
// is stored (console: otapass <password>); the password lives only in NVS
// and in the developer's local file, never in git.
#pragma once

#include <Arduino.h>

namespace ota {

bool enabled();
bool checkPassword(const String& pw);
int progress();  // -1 when no update is running, else 0-100

// Upload stream, driven by the web server.
bool startUpdate(size_t size);
bool write(const uint8_t* data, size_t len);
bool finish();   // true when the new image is valid; caller reboots
void abort();
const char* error();

// From the console. Never echoed or logged.
void setPassword(const char* pw);
bool hasPassword();

}  // namespace ota
