#include "../include/config.h"
#include "../include/debug.h"

#include <Preferences.h>
#include <string.h>

char stopIds[STOP_COUNT][STOP_ID_MAX];
char stopNames[STOP_COUNT][STOP_NAME_MAX];

static const char* NAS_NAMESPACE  = "busstop2";
static const char* NAS_URL_KEY    = "nasUrl";

void initStopConfig() {
  for (uint8_t i = 0; i < STOP_COUNT; i++) {
    setStopConfig(i, STOP_IDS_DEFAULT[i], STOP_NAMES_DEFAULT[i]);
  }
  DBG_INFO("Stop config: seeded from compiled defaults until NAS sync");
}

bool setStopConfig(uint8_t idx, const char* stopId, const char* stopName) {
  if (idx >= STOP_COUNT || stopId == nullptr || stopName == nullptr) {
    return false;
  }
  if (strlen(stopId) >= STOP_ID_MAX || strlen(stopName) >= STOP_NAME_MAX) {
    return false;
  }

  strncpy(stopIds[idx], stopId, STOP_ID_MAX - 1);
  stopIds[idx][STOP_ID_MAX - 1] = '\0';

  strncpy(stopNames[idx], stopName, STOP_NAME_MAX - 1);
  stopNames[idx][STOP_NAME_MAX - 1] = '\0';

  return true;
}

// ---------------------------------------------------------------------------
// NAS URL config
// ---------------------------------------------------------------------------

String getNasUrl() {
  Preferences prefs;
  if (!prefs.begin(NAS_NAMESPACE, true)) {
    // A read-only open fails on first boot if the namespace does not exist yet.
    if (!prefs.begin(NAS_NAMESPACE, false)) {
      DBG_WARN("NAS config: prefs.begin failed, using default");
      return NAS_DEFAULT_URL;
    }

    prefs.putString(NAS_URL_KEY, NAS_DEFAULT_URL);
    prefs.end();
    DBG_INFO("NAS config: initialised default URL in NVS");
    return NAS_DEFAULT_URL;
  }

  String url = prefs.getString(NAS_URL_KEY, NAS_DEFAULT_URL);
  prefs.end();
  return url;
}

bool setNasUrl(const String& url) {
  Preferences prefs;
  if (!prefs.begin(NAS_NAMESPACE, false)) {
    DBG_ERROR("NAS config: prefs.begin failed");
    return false;
  }

  prefs.putString(NAS_URL_KEY, url);
  prefs.end();
  DBG_INFO("NAS URL saved: %s", url.c_str());
  return true;
}
