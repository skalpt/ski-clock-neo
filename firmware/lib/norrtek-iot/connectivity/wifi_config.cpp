#include "wifi_config.h"

static char wifiProductName[32] = "generic";

#if defined(ESP32)
  WebServer server;
#elif defined(ESP8266)
  ESP8266WebServer server;
#endif

AutoConnect portal(server);
AutoConnectConfig portalConfig;

void setWifiProduct(const char* productName) {
  strncpy(wifiProductName, productName, sizeof(wifiProductName) - 1);
  wifiProductName[sizeof(wifiProductName) - 1] = '\0';
}

#if defined(ESP32)
void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINT("WiFi connected! IP: ");
  DEBUG_PRINTLN(WiFi.localIP().toString());
  setConnectivityState(true, mqttIsConnected);
  logEvent("wifi_connect", "{\"ssid\":\"" + WiFi.SSID() + "\"}");
}

void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINTLN("WiFi disconnected!");
  setConnectivityState(false, false);
  logEvent("wifi_disconnect");
  
  if (mqttIsConnected) {
    disconnectMQTT();
  }
}
#elif defined(ESP8266)
WiFiEventHandler wifiConnectedHandler;
WiFiEventHandler wifiDisconnectedHandler;

void onWiFiConnected(const WiFiEventStationModeGotIP& event) {
  DEBUG_PRINT("WiFi connected! IP: ");
  DEBUG_PRINTLN(WiFi.localIP().toString());
  setConnectivityState(true, mqttIsConnected);
  logEvent("wifi_connect", ("{\"ssid\":\"" + WiFi.SSID() + "\"}").c_str());
}

void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event) {
  DEBUG_PRINTLN("WiFi disconnected!");
  setConnectivityState(false, false);
  logEvent("wifi_disconnect");
  
  if (mqttIsConnected) {
    disconnectMQTT();
  }
}
#endif

void initWiFi() {
  DEBUG_PRINTLN("Initializing WiFi with AutoConnect...");
  
  String apName = "NorrtekIoT-" + String(wifiProductName) + "-" + getDeviceID().substring(0, 4);
  portalConfig.apid = apName;
  portalConfig.psk = "";
  portalConfig.autoReconnect = true;
  portalConfig.reconnectInterval = 1;
  
  portal.config(portalConfig);
  
  #if defined(ESP32)
    WiFi.onEvent(onWiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  #elif defined(ESP8266)
    wifiConnectedHandler = WiFi.onStationModeGotIP(onWiFiConnected);
    wifiDisconnectedHandler = WiFi.onStationModeDisconnected(onWiFiDisconnected);
  #endif
  
  if (portal.begin()) {
    DEBUG_PRINTLN("AutoConnect portal started");
    DEBUG_PRINT("WiFi connected to: ");
    DEBUG_PRINTLN(WiFi.SSID());
    DEBUG_PRINT("IP address: ");
    DEBUG_PRINTLN(WiFi.localIP().toString());
    setConnectivityState(true, false);
  } else {
    DEBUG_PRINTLN("AutoConnect portal failed to start");
    setConnectivityState(false, false);
  }
}

void updateWiFi() {
  portal.handleClient();
}
