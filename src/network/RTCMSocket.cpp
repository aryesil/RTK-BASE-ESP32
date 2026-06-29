#include <network/RTCMSocket.h>
#include <Globals.h>

void setupRTCMSocket() {
  rtcmServer.begin();
}

void handleNewRTCMClients() {
  if (rtcmServer.hasClient()) {
    WiFiClient newClient = rtcmServer.available();
    if (newClient) {
      newClient.setNoDelay(true); 
      
      bool added = false;
      if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
        for (int i = 0; i < 3; i++) {
          if (!tcpClients[i].connected()) {
            tcpClients[i].stop();
            tcpClients[i] = newClient;
            added = true;
            break;
          }
        }
        xSemaphoreGive(tcpMutex);
      }
      if (!added) newClient.stop(); 
    }
  }
}

void broadcastRTCM(uint8_t* buf, size_t len) {
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < 3; i++) {
      if (tcpClients[i].connected()) {
        tcpClients[i].write(buf, len);
      }
    }
    xSemaphoreGive(tcpMutex);
  }
}

int getActiveTCPClientsAndCleanup() {
  int activeTcp = 0;
  if (xSemaphoreTake(tcpMutex, portMAX_DELAY)) {
    for (int i = 0; i < 3; i++) {
      if (tcpClients[i].connected()) {
        activeTcp++;
      } else {
        tcpClients[i].stop();
      }
    }
    xSemaphoreGive(tcpMutex);
  }
  return activeTcp;
}