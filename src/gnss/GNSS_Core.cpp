#include <gnss/GNSS_Core.h>

void addSat(const char* sys, int id, int elev, int azim, int snr, int sig) {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();
    for (int i = 0; i < activeSatCount; i++) {
      if (strcmp(activeSats[i].sys, sys) == 0 && activeSats[i].id == id && activeSats[i].sig == sig) {
        activeSats[i].elev = elev; 
        activeSats[i].azim = azim; 
        activeSats[i].snr = snr;
        activeSats[i].lastSeen = now;
        xSemaphoreGive(dataMutex);
        return;
      }
    }
    if (activeSatCount < MAX_SATS) {
      activeSats[activeSatCount].id = id;
      strlcpy(activeSats[activeSatCount].sys, sys, sizeof(activeSats[0].sys));
      activeSats[activeSatCount].elev = elev;
      activeSats[activeSatCount].azim = azim;
      activeSats[activeSatCount].snr = snr;
      activeSats[activeSatCount].sig = sig; 
      activeSats[activeSatCount].lastSeen = now;
      activeSatCount++;
    }
    xSemaphoreGive(dataMutex);
  }
}

void cleanOldSatellites() {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    uint32_t now = millis();
    int newCount = 0;
    for (int i = 0; i < activeSatCount; i++) {
      if (now - activeSats[i].lastSeen <= 5000) {
        activeSats[newCount++] = activeSats[i];
      }
    }
    activeSatCount = newCount;
    xSemaphoreGive(dataMutex);
  }
}

bool isChecksumValid(const char* sentence) {
  int len = strlen(sentence);
  if (len < 8) return false; 
  
  int starIndex = -1;
  for (int i = 0; i < len; i++) {
    if (sentence[i] == '*') {
      starIndex = i;
      break;
    }
  }
  
  if (starIndex == -1 || starIndex > len - 3) return false;

  char c1 = sentence[starIndex + 1];
  char c2 = sentence[starIndex + 2];
  if (!isxdigit(c1) || !isxdigit(c2)) return false; 
  
  uint8_t calculatedCS = 0;
  for (int i = 1; i < starIndex; i++) {
    calculatedCS ^= sentence[i];
  }
  
  char hexStr[3] = {c1, c2, '\0'};
  uint8_t providedCS = (uint8_t)strtol(hexStr, NULL, 16);
  
  return (calculatedCS == providedCS); 
}

void uyduTipleriniAyristir(const char* nmea) {
  if (strstr(nmea, "GSV") != NULL) {
    const char* sys = "UN";
    if (strncmp(nmea, "$GP", 3) == 0) sys = "GP";
    else if (strncmp(nmea, "$GL", 3) == 0) sys = "GL";
    else if (strncmp(nmea, "$GA", 3) == 0) sys = "GA";
    else if (strncmp(nmea, "$GB", 3) == 0 || strncmp(nmea, "$BD", 3) == 0) sys = "GB";
    else if (strncmp(nmea, "$GI", 3) == 0) sys = "GI";
    else if (strncmp(nmea, "$GQ", 3) == 0) sys = "GQ";
    else if (strncmp(nmea, "$SB", 3) == 0) sys = "SB"; 

    int commas[25]; 
    int cCount = 0;
    int len = strlen(nmea);
    int starIdx = -1;
    
    for (int i = 0; i < len; i++) {
      if (nmea[i] == ',') {
        if (cCount < 25) commas[cCount++] = i;
      } else if (nmea[i] == '*') {
        starIdx = i;
      }
    }

    if (starIdx < 0) return;

    bool hasSignalId = false;
    int sig_id = 1; 
    
    if (cCount >= 4) {
      int lastFieldLen = starIdx - commas[cCount - 1] - 1;
      if ((cCount - 3) % 4 == 1 && lastFieldLen > 0 && lastFieldLen <= 2) { 
        char sigStr[4] = {0};
        strncpy(sigStr, nmea + commas[cCount - 1] + 1, lastFieldLen);
        
        bool isHex = true;
        for(int k = 0; k < lastFieldLen; k++) {
            if(!isxdigit(sigStr[k])) isHex = false;
        }
        
        if (isHex) {
          hasSignalId = true;
          sig_id = strtol(sigStr, NULL, 16);
        }
      }
    }

    for (int i = 4; i < cCount; i += 4) {
      if (i + 2 < cCount) {
        int id = atoi(nmea + commas[i-1] + 1);
        int elev = atoi(nmea + commas[i] + 1);
        int azim = atoi(nmea + commas[i+1] + 1);
        int snr = 0;
        
        if (i + 3 < cCount) {
          snr = atoi(nmea + commas[i+2] + 1);
        } else if (!hasSignalId) {
          char snrStr[8] = {0};
          int snrLen = starIdx - commas[i+2] - 1;
          if(snrLen > 0 && snrLen < 8) {
              strncpy(snrStr, nmea + commas[i+2] + 1, snrLen);
              snr = atoi(snrStr);
          }
        }
        
        if (id > 0) {
          const char* finalSys = sys;

          if (strcmp(sys, "GP") == 0 || strcmp(sys, "UN") == 0 || strcmp(sys, "SB") == 0) {
              if (id == 121 || id == 123 || id == 126 || id == 136 || 
                  id == 131 || id == 133 || id == 135 ||              
                  id == 127 || id == 128 || id == 157 ||              
                  id == 129 || id == 137 ||                           
                  id == 134 || id == 149 ||                           
                  id == 130 || id == 143) {                           
                  finalSys = "SB"; 
              } 
              else if (id >= 33 && id <= 64) {                          
                  finalSys = "SB"; 
                  id += 87; 
              } 
              else if (id == 183 || id == 193 || (id >= 193 && id <= 200)) {
                  finalSys = "GQ"; 
              }
          }
          
          addSat(finalSys, id, elev, azim, snr, sig_id); 
        }
      }
    }
  }
}

bool sendGnssCommand(const char* cmd, unsigned long timeoutMs) {
  while(Serial2.available()) Serial2.read(); 
  
  Serial2.print(cmd);
  Serial2.print("\r\n");
  Serial.print("[GNSS-TX] "); Serial.println(cmd);

  char expectedAck[32] = {0};
  
  if (strncmp(cmd, "$PAIR", 5) == 0) {
    char cmdId[4] = {0};
    strncpy(cmdId, cmd + 5, 3);
    snprintf(expectedAck, sizeof(expectedAck), "$PAIR001,%s,0", cmdId);
  } 
  else if (strncmp(cmd, "$PQTM", 5) == 0) {
    int endIdx = 0;
    while(cmd[endIdx] != ',' && cmd[endIdx] != '*' && cmd[endIdx] != '\0' && endIdx < 31) {
      expectedAck[endIdx] = cmd[endIdx];
      endIdx++;
    }
    expectedAck[endIdx] = '\0';
    strcat(expectedAck, ",OK"); 
  } 
  else {
    strcpy(expectedAck, "OK");
  }

  unsigned long start = millis();
  String line = "";
  
  while (millis() - start < timeoutMs) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n') {
        if (line.indexOf(expectedAck) != -1) {
          Serial.print("[GNSS-RX] ACKNOWLEDGED: "); Serial.println(line);
          return true;
        }
        line = ""; 
      } else if (c != '\r') {
        line += c;
      }
    }
  }
  
  Serial.print("[GNSS-ERR] TIMEOUT! Module did not acknowledge: ");
  Serial.println(expectedAck);
  return false;
}