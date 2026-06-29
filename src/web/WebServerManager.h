#pragma once
#include <Arduino.h>

void setupWebServer();
void handleWebSocketQueue();
void handleTelemetry(uint32_t now);