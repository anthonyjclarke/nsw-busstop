#include "display.h"
#include "bus_api.h"
#include "time_mgr.h"
#include "../include/config.h"
#include "../include/debug.h"

// Using TFT_eSPI built-in fonts:
//   Font 4 (26px) — header time
//   Font 2 (16px) — all other text
// Upgrade path: generate NotoSansBold VLW fonts via Processing and swap in.

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------
#define COL_BG        TFT_BLACK
#define COL_HEADER_FG TFT_WHITE
#define COL_DATE_FG   0xC618       // light grey (RGB565)
#define COL_STOP_NAME TFT_CYAN
#define COL_ROUTE     TFT_WHITE
#define COL_TIME_FG   TFT_WHITE
#define COL_MINS_NEAR TFT_GREEN    // < 10 min
#define COL_MINS_FAR  TFT_YELLOW   // >= 10 min
#define COL_ALERT     TFT_ORANGE
#define COL_DIVIDER   0x2104       // dark grey (RGB565)
#define COL_STATUS_BG 0x2104

// ---------------------------------------------------------------------------
// Layout
// Landscape: 320×240
// Panels:  [0] top-left   [1] top-right
//          [2] bot-left   [3] bot-right
// ---------------------------------------------------------------------------
static const int PANEL_X[4] = { 0,      PANEL_W, 0,      PANEL_W };
static const int PANEL_Y[4] = { HEADER_H, HEADER_H,
                                 HEADER_H + PANEL_H, HEADER_H + PANEL_H };

#define ROW_NAME_H  16   // px — compact stop name band
#define ROW_DEP_H   22   // px — tightened row pitch for 3 departures
#define PAD_X        4   // px — left padding inside panel
#define ALERT_DOT_R  3   // px — subtle service-alert indicator
#define BADGE_X     30   // px — compact realtime/scheduled badge anchor
#define MINS_X      68   // px — minutes label anchor after badge
#define BADGE_FONT   1   // smaller built-in font for compact status labels
#define BADGE_Y_OFF  4   // vertically centre the smaller badge in the row

TFT_eSPI tft = TFT_eSPI();

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------

static void fitPanelText(const char* text, char* buf, size_t bufLen, int maxWidth, int font) {
  if (bufLen == 0) return;
  if (text == nullptr) {
    buf[0] = '\0';
    return;
  }

  strncpy(buf, text, bufLen - 1);
  buf[bufLen - 1] = '\0';

  if (tft.textWidth(buf, font) <= maxWidth) {
    return;
  }

  size_t len = strlen(buf);
  while (len > 0) {
    if (len > 3) {
      buf[len - 1] = '\0';
      len--;
      strcpy(&buf[len - 3], "...");
    } else {
      buf[0] = '\0';
    }

    if (tft.textWidth(buf, font) <= maxWidth || buf[0] == '\0') {
      return;
    }
  }
}

static void drawDividers() {
  tft.drawFastHLine(0,      HEADER_H - 1,        320, COL_DIVIDER);  // below header
  tft.drawFastHLine(0,      HEADER_H + PANEL_H,  320, COL_DIVIDER);  // mid horizontal
  tft.drawFastVLine(PANEL_W, HEADER_H,            240 - HEADER_H, COL_DIVIDER);  // mid vertical
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void initDisplay() {
  tft.init();
  tft.setRotation(1);  // landscape
  tft.fillScreen(COL_BG);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, BRIGHTNESS_DEFAULT);

  drawDividers();
  DBG_INFO("Display initialised — %dx%d", tft.width(), tft.height());
}

void setBrightness(uint8_t brightness) {
  ledcWrite(0, brightness);
}

void drawHeader(const char* timeStr, const char* dateStr) {
  tft.fillRect(0, 0, 320, HEADER_H - 1, COL_BG);

  // Time — Font 4 (26px), left-aligned, 1px top margin
  tft.setTextColor(COL_HEADER_FG, COL_BG);
  tft.drawString(timeStr, PAD_X, 1, 4);

  // Date — Font 2 (16px), right-aligned, vertically centred in header
  tft.setTextColor(COL_DATE_FG, COL_BG);
  tft.drawRightString(dateStr, 320 - PAD_X, 6, 2);
}

void drawStopPanel(uint8_t idx) {
  if (idx >= STOP_COUNT) return;

  int px = PANEL_X[idx];
  int py = PANEL_Y[idx];

  tft.fillRect(px, py, PANEL_W - 1, PANEL_H - 1, COL_BG);

  const StopData& sd = stopData[idx];

  // Stop name — Font 2, cyan
  tft.setTextColor(COL_STOP_NAME, COL_BG);
  char stopLabel[STOP_NAME_MAX];
  int titleMaxWidth = PANEL_W - (PAD_X * 2) - (sd.hasAlerts ? 12 : 0);
  fitPanelText(stopNames[idx], stopLabel, sizeof(stopLabel), titleMaxWidth, 2);
  tft.drawString(stopLabel, px + PAD_X, py + 2, 2);

  if (sd.hasAlerts) {
    tft.fillCircle(px + PANEL_W - 10, py + 8, ALERT_DOT_R, COL_ALERT);
  }

  if (!sd.valid || sd.count == 0) {
    tft.setTextColor(TFT_DARKGREY, COL_BG);
    tft.drawString("No data", px + PAD_X, py + ROW_NAME_H + 4, 2);
    return;
  }

  for (uint8_t i = 0; i < sd.count && i < DEPARTURES_PER_STOP; i++) {
    const Departure& dep = sd.departures[i];
    int rowY = py + ROW_NAME_H + (i * ROW_DEP_H) + 4;
    int clockRightX = px + PANEL_W - PAD_X - 2;

    // Route number
    tft.setTextColor(COL_ROUTE, COL_BG);
    tft.drawString(dep.route, px + PAD_X, rowY, 2);

    // Compact text badge matches the WebUI better than a dot/tilde marker.
    if (dep.isRealtime) {
      tft.setTextColor(COL_MINS_NEAR, COL_BG);
      tft.drawString("LIVE", px + BADGE_X, rowY + BADGE_Y_OFF, BADGE_FONT);
    } else {
      tft.setTextColor(TFT_DARKGREY, COL_BG);
      tft.drawString("SCH", px + BADGE_X, rowY + BADGE_Y_OFF, BADGE_FONT);
    }

    // Minutes until — colour-coded; day abbreviation for non-today departures
    char minsStr[8];
    if (!isLocalToday(dep.epochUTC)) {
      formatLocalDayAbbr(dep.epochUTC, minsStr, sizeof(minsStr));
      tft.setTextColor(TFT_DARKGREY, COL_BG);
    } else if (dep.minutesUntil <= 0) {
      strncpy(minsStr, "Now", sizeof(minsStr));
      tft.setTextColor(TFT_ORANGE, COL_BG);
    } else if (dep.minutesUntil >= 60) {
      snprintf(minsStr, sizeof(minsStr), "%dh%02dm", dep.minutesUntil / 60, dep.minutesUntil % 60);
      tft.setTextColor(COL_MINS_FAR, COL_BG);
    } else {
      snprintf(minsStr, sizeof(minsStr), "%dm", dep.minutesUntil);
      tft.setTextColor((dep.minutesUntil < 10) ? COL_MINS_NEAR : COL_MINS_FAR, COL_BG);
    }
    tft.drawString(minsStr, px + MINS_X, rowY, 2);

    // Clock time
    tft.setTextColor(COL_DATE_FG, COL_BG);
    tft.drawRightString(dep.clockTime, clockRightX, rowY, 2);
  }
}

void drawAllStops() {
  for (uint8_t i = 0; i < STOP_COUNT; i++) {
    drawStopPanel(i);
  }
  drawDividers();  // redraw after fillRect ops may clip divider lines
}

void drawStatusBar(const char* msg, uint16_t colour) {
  tft.fillRect(0, 220, 320, 20, COL_STATUS_BG);
  tft.setTextColor(colour, COL_STATUS_BG);
  tft.drawString(msg, PAD_X, 222, 2);
}

void drawLastUpdated(const char* timeStr, bool serverOffline) {
  // Sits at y=224–239 — the 16px gap below the last departure row in the
  // lower panels. Panel fillRect clears this area on every drawAllStops(),
  // so no background fill is needed here.
  char buf[12];
  snprintf(buf, sizeof(buf), "upd %s", timeStr);

  tft.setTextColor(COL_DATE_FG, COL_BG);
  int tw = tft.drawString("Server Status", PAD_X, 224, 2);
  tft.fillCircle(PAD_X + tw + 6, 231, 4, serverOffline ? TFT_RED : TFT_GREEN);

  tft.setTextColor(COL_DATE_FG, COL_BG);
  tft.drawRightString(buf, 320 - PAD_X, 224, 2);
}
