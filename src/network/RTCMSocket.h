#pragma once
#include <Arduino.h>

void setupRTCMSocket();
void handleNewRTCMClients();
void broadcastRTCM(uint8_t* buf, size_t len);
int getActiveTCPClientsAndCleanup();