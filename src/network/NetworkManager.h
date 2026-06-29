#pragma once
#include <Arduino.h>

void setupNetwork();
void handleNetworkState(uint32_t now);

void networkTaskCode(void * parameter);