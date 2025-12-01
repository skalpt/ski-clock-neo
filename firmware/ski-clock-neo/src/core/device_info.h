#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

// Get unique device ID
String getDeviceID();

// Get board type as human-readable string
String getBoardType();

// Get platform identifier for firmware downloads
String getPlatform();

// Parse version string to comparable integer
long parseVersion(String version);

#endif
