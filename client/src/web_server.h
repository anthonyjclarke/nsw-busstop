#pragma once

// Initialise AsyncWebServer routes. Call after WiFi is connected.
void initWebServer();

// No-op — AsyncWebServer is event-driven. Kept for future use.
void handleWebServer();

// Returns true once for each queued stop-data refresh requested by the WebUI.
bool consumeStopRefreshRequest();
