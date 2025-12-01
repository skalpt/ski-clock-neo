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

#include <AutoConnectCore.h>       // For AutoConnect portal functionality
#include <AutoConnectCredential.h> // For credential management
#include "core/device_info.h"      // For getDeviceID()
#include "core/led_indicator.h"    // For LED status patterns when WiFi connection state changes
#include "mqtt_client.h"           // For MQTT connection management when WiFi connection state changes
#include "core/event_log.h"        // For logging WiFi events
#include "core/debug.h"            // For debug logging

// Configuration constants
const char* AP_PASSWORD = "configure";

// Device-specific SSID (generated dynamically in setupWiFi)
char AP_SSID[32];  // Buffer for dynamic SSID (e.g., "SkiClockNeo-abcdef123456")

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

// Initialize AutoConnect with configuration
void initWiFi() {
  // Register WiFi event handlers
  DEBUG_PRINTLN("Registering WiFi connection event handlers...");
  #if defined(ESP32)
    WiFi.onEvent(onWiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  #elif defined(ESP8266)
    static WiFiEventHandler gotIpHandler = WiFi.onStationModeGotIP(onWiFiConnected);
    static WiFiEventHandler disconnectedHandler = WiFi.onStationModeDisconnected(onWiFiDisconnected);
  #endif
  
  DEBUG_PRINTLN("Initializing WiFi with AutoConnect...");

  // Generate device-specific AP SSID
  String deviceID = getDeviceID();
  snprintf(AP_SSID, sizeof(AP_SSID), "SkiClockNeo-%s", deviceID.c_str());
  
  DEBUG_PRINT("Generated AP SSID: ");
  DEBUG_PRINTLN(AP_SSID);

  // Configure AutoConnect behavior
  config.apid = AP_SSID;
  config.psk = AP_PASSWORD;
  config.title = "⛷️ Ski Clock Neo Setup";

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

  // Add redirect from root "/" to AutoConnect portal "/_ac"
  // This makes it easier for users browsing to the device IP directly
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Location", "/_ac", true);
    server.send(302, "text/plain", "Redirecting to portal...");
  });

  DEBUG_PRINTLN("AutoConnect portal is running");
  DEBUG_PRINT("Portal SSID: ");
  DEBUG_PRINTLN(AP_SSID);
  DEBUG_PRINT("Portal Password: ");
  DEBUG_PRINTLN(AP_PASSWORD);
  DEBUG_PRINTLN("Portal remains accessible even when connected to WiFi");
  DEBUG_PRINTLN("Access portal at device IP address (redirects / to /_ac)");
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

// WiFi event handlers - update connectivity state for centralized LED management
#if defined(ESP32)
  void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    DEBUG_PRINTLN("WiFi connected, connecting to MQTT...");
    
    // Log wifi_connect event with connection details
    String wifiData = "{\"ssid\":\"";
    wifiData += WiFi.SSID();
    wifiData += "\",\"rssi\":";
    wifiData += WiFi.RSSI();
    wifiData += ",\"ip\":\"";
    wifiData += WiFi.localIP().toString();
    wifiData += "\"}";
    logEvent("wifi_connect", wifiData.c_str());
    
    setConnectivityState(true, false);  // WiFi=connected, MQTT=disconnected
    connectMQTT();
  }

  void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    DEBUG_PRINTLN("WiFi disconnected, stopping MQTT...");
    logEvent("wifi_disconnect");
    disconnectMQTT();
    setConnectivityState(false, false);  // WiFi=disconnected, MQTT=disconnected
  }
#elif defined(ESP8266)
  void onWiFiConnected(const WiFiEventStationModeGotIP& event) {
    DEBUG_PRINTLN("WiFi connected, connecting to MQTT...");
    
    // Log wifi_connect event with connection details
    String wifiData = "{\"ssid\":\"";
    wifiData += WiFi.SSID();
    wifiData += "\",\"rssi\":";
    wifiData += WiFi.RSSI();
    wifiData += ",\"ip\":\"";
    wifiData += WiFi.localIP().toString();
    wifiData += "\"}";
    logEvent("wifi_connect", wifiData.c_str());
    
    setConnectivityState(true, false);  // WiFi=connected, MQTT=disconnected
    connectMQTT();
  }

  void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event) {
    DEBUG_PRINTLN("WiFi disconnected, stopping MQTT...");
    logEvent("wifi_disconnect");
    disconnectMQTT();
    setConnectivityState(false, false);  // WiFi=disconnected, MQTT=disconnected
  }
#endif

#endif