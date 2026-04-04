#pragma once
#include <Arduino.h>
#include "../include/config.h"

struct Departure {
  char   route[8];        // e.g. "501"
  char   clockTime[6];    // e.g. "10:48" — local Sydney time, derived from epochUTC
  char   destination[32]; // e.g. "Gladesville - Jordan St" — from transportation.destination.name
  time_t epochUTC;        // estimated departure as UTC epoch — recalculate minutes from this
  int    minutesUntil;    // computed from epochUTC; refreshed by recalcMinutes()
  int    delaySecs;       // estimated - planned, in seconds; 0 if no estimate available
  bool   isRealtime;      // true if isRealtimeControlled — live GPS vs scheduled
  bool   valid;
};

struct StopData {
  Departure departures[DEPARTURES_PER_STOP];
  uint8_t   count;
  bool      valid;          // at least one departure returned
  bool      hasAlerts;      // true if infos[] was non-empty
  char      alertText[64];  // first alert subtitle, truncated
  uint32_t  lastFetchMs;    // millis() of last successful fetch
};

// Global stop data array — indexed by stop config order
extern StopData stopData[STOP_COUNT];

void initBusApi();
bool fetchAllStops();      // single GET to NAS /api/state
void recalcMinutes();      // recalculate minutesUntil from stored epoch for all stops
