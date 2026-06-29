#pragma once
#include "Globals.h"

void addSat(const char* sys, int id, int elev, int azim, int snr, int sig);
void cleanOldSatellites();
bool isChecksumValid(const char* sentence);
void uyduTipleriniAyristir(const char* nmea);
bool sendGnssCommand(const char* cmd, unsigned long timeoutMs = 1000);