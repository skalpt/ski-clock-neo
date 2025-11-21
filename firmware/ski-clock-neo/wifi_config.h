#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

#include <AutoConnect.h>
#include <AutoConnectCredential.h>
#include "debug.h"

// Configuration constants
const char* AP_SSID = "SkiClock-Setup";
const char* AP_PASSWORD = "configure";

// AutoConnect objects
#if defined(ESP32)
  WebServer server;
  AutoConnect portal(server);
#else
  ESP8266WebServer server;
  AutoConnect portal(server);
#endif

AutoConnectConfig config;

// Custom CSS for portal styling
const char customCSS[] PROGMEM = R"(
body {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  font-family: Arial, sans-serif;
}
.menu > a {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}
.menu > a:hover {
  background: linear-gradient(135deg, #764ba2 0%, #667eea 100%);
}
)";

// Initialize AutoConnect with robust configuration
void setupWiFi() {
  DEBUG_PRINTLN("Initializing WiFi with AutoConnect...");
  
  // Configure AutoConnect behavior
  config.apid = AP_SSID;
  config.psk = AP_PASSWORD;
  config.title = "⛷️ Ski Clock Setup";
  config.homeUri = "/";  // Default AutoConnect home page
  config.bootUri = AC_ONBOOTURI_HOME;  // Redirect to homeUri on captive portal boot
  
  // KEY FEATURES for your requirements:
  
  // Keep Access Point running alongside WiFi connection
  // retainPortal keeps the SoftAP (and web interface) running after WiFi connects
  // NOTE: The auto-popup captive portal only works BEFORE WiFi connects
  //       After connection, users must manually connect to the same network and visit its IP
  config.retainPortal = true;
  
  // Automatically will try to reconnect with past established access points (BSSIDs) when the current configured SSID in ESP8266/ESP32 could not be connected
  config.autoReconnect = true;
  
  // Set reconnect interval for background retry (5 seconds)
  config.reconnectInterval = 5;
  
  // Configure portal timeout (0 = never timeout)
  config.portalTimeout = 0;  // Portal always available

  // Set host name for mDNS (bonjour)
  config.hostName = "ski-clock-neo";

  // Configure items in the AutoConnect menu
  config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET | AC_MENUITEM_DEVINFO | AC_MENUITEM_DELETESSID;
  
  // Minimum RSSI to connect (signal strength threshold)
  config.minRSSI = -80;

  // Disable auto-reset when the user clicks "Disconnect" in the portal menu
  config.autoReset = false;
  
  // Apply configuration
  portal.config(config);
  
  // Start AutoConnect
  // This will:
  // - Try to connect to previously saved networks (in order of signal strength)
  // - Show captive portal if no networks available or connection fails
  // - Keep portal running in background even after connection
  if (portal.begin()) {
    DEBUG_PRINTLN("WiFi connected successfully!");
    DEBUG_PRINT("IP address: ");
    DEBUG_PRINTLN(WiFi.localIP());
    DEBUG_PRINT("SSID: ");
    DEBUG_PRINTLN(WiFi.SSID());
  } else {
    DEBUG_PRINTLN("WiFi connection failed - portal active");
  }
  
  DEBUG_PRINTLN("AutoConnect portal is running");
  DEBUG_PRINT("Portal SSID: ");
  DEBUG_PRINTLN(AP_SSID);
  DEBUG_PRINT("Portal Password: ");
  DEBUG_PRINTLN(AP_PASSWORD);
  DEBUG_PRINTLN("Portal remains accessible even when connected to WiFi");
}

// Update WiFi - call this in loop()
// Handles:
// - Portal web interface requests
// - Background reconnection attempts
// - Credential management
void updateWiFi() {
  portal.handleClient();
  
  // AutoConnect automatically handles:
  // - Reconnection when WiFi drops
  // - Portal availability for network switching
  // - Multiple credential management
}

// Check if WiFi is currently connected
bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// Get WiFi status as string
String getWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Connected: " + WiFi.localIP().toString() + " (" + WiFi.SSID() + ")";
  } else {
    return "Disconnected - Portal Active";
  }
}

// Get number of stored credentials
int getStoredNetworkCount() {
  AutoConnectCredential credential;
  return credential.entries();
}

// Force disconnect and show portal (for manual network switching)
void openConfigPortal() {
  WiFi.disconnect();
  DEBUG_PRINTLN("Portal opened for manual configuration");
}

#endif
