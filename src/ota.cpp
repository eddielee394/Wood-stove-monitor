#include "ota.h"

#include <Preferences.h>
#include <Update.h>

#include <atomic>

namespace ota {
namespace {

const char* NVS_NS = "ota";
std::atomic<int> pct{-1};
size_t total = 0, written = 0;
String lastError;

String loadPassword() {
  Preferences p;
  p.begin(NVS_NS, true);
  String pw = p.getString("pass", "");
  p.end();
  return pw;
}

void fail() {
  lastError = Update.errorString();
  Serial.printf("ota: failed: %s\n", lastError.c_str());
  pct = -1;
}

}  // namespace

bool enabled() { return hasPassword(); }

bool checkPassword(const String& pw) {
  String stored = loadPassword();
  return !stored.isEmpty() && pw == stored;
}

int progress() { return pct; }

bool startUpdate(size_t size) {
  lastError = "";
  total = size;
  written = 0;
  // `size` is only for progress (it includes HTTP framing); let Update
  // accept up to the whole OTA slot and validate the image at the end.
  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    fail();
    return false;
  }
  pct = 0;
  Serial.println("ota: update starting");
  return true;
}

bool write(const uint8_t* data, size_t len) {
  if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    fail();
    return false;
  }
  written += len;
  if (total) pct = min(99, (int)(written * 100ULL / total));
  return true;
}

bool finish() {
  if (!Update.end(true)) {
    fail();
    return false;
  }
  pct = 100;
  Serial.printf("ota: update complete (%u bytes), rebooting\n", (unsigned)written);
  return true;
}

void abort() {
  Update.abort();
  pct = -1;
}

const char* error() { return lastError.c_str(); }

void setPassword(const char* pw) {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("pass", pw);
  p.end();
}

bool hasPassword() { return !loadPassword().isEmpty(); }

}  // namespace ota
