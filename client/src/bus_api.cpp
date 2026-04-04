#include "bus_api.h"
#include "time_mgr.h"
#include "../include/config.h"
#include "../include/debug.h"
#include "../include/secrets.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

StopData stopData[STOP_COUNT];

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void initBusApi() {
  memset(stopData, 0, sizeof(stopData));
  DBG_INFO("Bus API initialised — %d stops configured", STOP_COUNT);
}

bool fetchAllStops() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_WARN("fetchAllStops: WiFi not connected");
    return false;
  }

  String nasUrl = getNasUrl();
  String apiUrl = nasUrl + "/api/state";

  WiFiClient client;  // Plain HTTP for LAN
  HTTPClient http;
  http.begin(client, apiUrl);
  http.setTimeout(10000);

  // Add auth header if API key is configured
  if (strlen(SECRET_NAS_API_KEY) > 0) {
    http.addHeader("Authorization", String("Bearer ") + SECRET_NAS_API_KEY);
  }

  DBG_INFO("Fetching from NAS: %s", apiUrl.c_str());
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    DBG_WARN("NAS fetch failed: HTTP %d", code);
    http.end();
    return false;
  }

  int contentLen = http.getSize();
  char lenBuf[16];
  if (contentLen < 0) {
    strcpy(lenBuf, "chunked");
  } else {
    snprintf(lenBuf, sizeof(lenBuf), "%d bytes", contentLen);
  }
  DBG_INFO("NAS response: %s, heap: %u", lenBuf, ESP.getFreeHeap());

  // Parse JSON directly from stream — NAS response is ~2KB for 4 stops
  StaticJsonDocument<4096> doc;

  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    DBG_ERROR("NAS JSON parse error: %s", err.c_str());
    return false;
  }

  // Parse stops array
  JsonArray stops = doc["stops"];
  if (stops.isNull()) {
    DBG_WARN("NAS response: no stops array");
    return false;
  }

  // Clear existing data
  memset(stopData, 0, sizeof(stopData));

  // Populate stopData directly in server-provided order so the NAS remains
  // the single source of truth for the displayed stop list.
  uint8_t idx = 0;
  for (JsonObject stop : stops) {
    if (idx >= STOP_COUNT) {
      DBG_WARN("NAS returned more than %d stops; extra stops ignored", STOP_COUNT);
      break;
    }

    const char* stopId = stop["id"];
    const char* name = stop["name"];
    if (!stopId || !name || !setStopConfig(idx, stopId, name)) {
      DBG_WARN("NAS stop[%d]: invalid id/name payload", idx);
      continue;
    }

    StopData& sd = stopData[idx];
    sd.valid = stop["valid"] | true;  // default true if missing
    sd.lastFetchMs = millis();

    // Parse departures
    JsonArray deps = stop["departures"];
    sd.count = min((size_t)DEPARTURES_PER_STOP, deps.size());

    for (uint8_t j = 0; j < sd.count; j++) {
      JsonObject dep = deps[j];
      Departure& d = sd.departures[j];

      const char* route = dep["route"];
      if (route) strncpy(d.route, route, sizeof(d.route) - 1);

      const char* clock = dep["clock"];
      if (clock) strncpy(d.clockTime, clock, sizeof(d.clockTime) - 1);

      const char* dest = dep["dest"];
      if (dest) strncpy(d.destination, dest, sizeof(d.destination) - 1);

      d.epochUTC = dep["epoch"] | 0;
      d.minutesUntil = dep["minutes"] | 0;
      d.delaySecs = dep["delay"] | 0;
      d.isRealtime = dep["rt"] | false;

      d.valid = true;
    }

    // Handle alerts
    const char* alert = stop["alert"];
    if (alert && strlen(alert) > 0) {
      sd.hasAlerts = true;
      strncpy(sd.alertText, alert, sizeof(sd.alertText) - 1);
    }

    idx++;
  }

  DBG_INFO("NAS fetch complete: %d stops updated", stops.size());
  return true;
}

void recalcMinutes() {
  time_t nowUTC = getUTCNow();
  for (uint8_t i = 0; i < STOP_COUNT; i++) {
    for (uint8_t j = 0; j < stopData[i].count; j++) {
      Departure& d = stopData[i].departures[j];
      if (d.valid) {
        d.minutesUntil = (int)((d.epochUTC - nowUTC) / 60);
      }
    }
  }
}
