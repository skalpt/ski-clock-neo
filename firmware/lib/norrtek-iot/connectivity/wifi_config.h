#ifndef NORRTEK_WIFI_CONFIG_H
#define NORRTEK_WIFI_CONFIG_H

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

#include <AutoConnectCore.h>
#include <AutoConnectCredential.h>
#include "../core/device_info.h"
#include "../core/led_indicator.h"
#include "mqtt_client.h"
#include "../core/event_log.h"
#include "../core/debug.h"

void setWifiProduct(const char* productName);

void initWiFi();
void updateWiFi();

#if defined(ESP32)
  void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info);
  void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
#elif defined(ESP8266)
  void onWiFiConnected(const WiFiEventStationModeGotIP& event);
  void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event);
#endif

#endif
