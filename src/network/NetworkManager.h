#pragma once
#include <Arduino.h>

void setupNetwork();
void handleNetworkState(uint32_t now);

// Bu satırı ekle:
void networkTaskCode(void * parameter);