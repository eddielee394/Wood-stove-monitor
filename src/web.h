// Local web page and JSON API, served from the net task once Wi-Fi is up.
//   GET  /              status page (web_page.h)
//   GET  /api/status    readings, alarms, settings, Wi-Fi
//   POST /api/settings  form-encoded key=value pairs, applied atomically
//   POST /api/silence   same as tapping Silence on the screen
//   POST /api/location  lat=..&lon=.. for the outside temperature (empty clears)
//   POST /api/mqtt      host, port, user, pass for Home Assistant (empty host = off)
#pragma once

namespace web {

void begin();   // call from the net task after the first connection
void handle();  // call often from the net task

}  // namespace web
