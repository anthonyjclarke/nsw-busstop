#pragma once

// Initialise AsyncWebServer routes. Call after WiFi is connected.
// AsyncWebServer is fully event-driven — no per-loop handler needed.
void initWebServer();
