#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include "Config.h"

// ==========================================
// AĞ DURUMLARI
// ==========================================
enum NetState { NET_AP, NET_CONNECTING, NET_SHOW_IP, NET_STA, NET_RECONNECTING };
extern NetState currentNetState;
extern uint32_t netStateTimer;
extern String targetSSID;
extern String targetPass;
extern Preferences prefs;
extern bool newCredentialsReceived;

// ==========================================
// NESNELER VE DONANIM
// ==========================================
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern TinyGPSPlus gps;

extern WiFiServer rtcmServer;
extern WiFiClient tcpClients[3]; 

// FreeRTOS Task, Mutex and Queue Definitions
extern TaskHandle_t NetworkTaskHandle;
extern SemaphoreHandle_t dataMutex; 
extern SemaphoreHandle_t tcpMutex;  
extern QueueHandle_t termQueue;     

extern portMUX_TYPE ppsMux; 

// ==========================================
// VERİ YAPILARI
// ==========================================
struct SatData {
  int id;
  char sys[4]; 
  int elev;   
  int azim;   
  int snr;    
  int sig; 
  uint32_t lastSeen; 
};

extern SatData activeSats[MAX_SATS];
extern int activeSatCount;

struct GpsSnapshot {
  double lat, lon, alt, hdop;
  bool validLoc, validAlt, validHdop, validTime;
  uint8_t hour, min, sec;
  uint8_t fixQual; 
};
extern GpsSnapshot safeGps;

extern uint32_t rtcmPaketSayaci;
extern volatile uint32_t sonPpsZamaniMicros; 
extern uint8_t globalFixQuality; 

// Counters for Core CPU Load Tracking
extern uint32_t core1BusyTime; 

extern char nmeaBuff[MAX_NMEA];
extern int nmeaIdx;