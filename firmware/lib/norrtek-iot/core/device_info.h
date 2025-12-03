#ifndef NORRTEK_DEVICE_INFO_H
#define NORRTEK_DEVICE_INFO_H

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

String getDeviceID();

String getBoardType();

String getPlatform();

long parseVersion(String version);

#endif
