#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <Preferences.h>
  WebServer server(80);
  Preferences preferences;
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <EEPROM.h>
  ESP8266WebServer server(80);
#else
  #error "This code requires ESP32 or ESP8266"
#endif

#include <DNSServer.h>

// Configuration constants
const char* AP_SSID = "SkiClock-Setup";
const char* AP_PASSWORD = "configure";
const uint8_t DNS_PORT = 53;
const unsigned long WIFI_TIMEOUT_MS = 10000;  // 10 seconds
const unsigned long PORTAL_TIMEOUT_MS = 300000;  // 5 minutes

// Global objects
DNSServer dnsServer;
bool wifiConfigMode = false;
unsigned long portalStartTime = 0;

// HTML for the configuration portal
const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ski Clock - WiFi Setup</title>
<style>
body {
  font-family: Arial, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  margin: 0;
  padding: 20px;
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
}
.container {
  background: white;
  border-radius: 10px;
  padding: 30px;
  box-shadow: 0 10px 40px rgba(0,0,0,0.2);
  max-width: 400px;
  width: 100%;
}
h1 {
  color: #667eea;
  text-align: center;
  margin-top: 0;
}
.subtitle {
  text-align: center;
  color: #666;
  margin-bottom: 30px;
}
label {
  display: block;
  margin-bottom: 5px;
  color: #333;
  font-weight: bold;
}
input[type="text"], input[type="password"] {
  width: 100%;
  padding: 10px;
  margin-bottom: 20px;
  border: 2px solid #ddd;
  border-radius: 5px;
  box-sizing: border-box;
  font-size: 16px;
}
input[type="text"]:focus, input[type="password"]:focus {
  outline: none;
  border-color: #667eea;
}
button {
  width: 100%;
  padding: 12px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  border: none;
  border-radius: 5px;
  font-size: 16px;
  font-weight: bold;
  cursor: pointer;
  transition: transform 0.2s;
}
button:hover {
  transform: translateY(-2px);
}
button:active {
  transform: translateY(0);
}
.info {
  background: #f0f7ff;
  border-left: 4px solid #667eea;
  padding: 10px;
  margin-bottom: 20px;
  border-radius: 3px;
  font-size: 14px;
}
.network-list {
  margin-bottom: 20px;
  max-height: 200px;
  overflow-y: auto;
  border: 2px solid #ddd;
  border-radius: 5px;
}
.network-item {
  padding: 10px;
  cursor: pointer;
  border-bottom: 1px solid #eee;
  transition: background 0.2s;
}
.network-item:hover {
  background: #f5f5f5;
}
.network-item:last-child {
  border-bottom: none;
}
.signal-strength {
  float: right;
  color: #667eea;
}
</style>
</head>
<body>
<div class="container">
  <h1>⛷️ Ski Clock Setup</h1>
  <p class="subtitle">Configure WiFi Connection</p>
  
  <div class="info">
    Select a network below or enter credentials manually.
  </div>
  
  <div class="network-list" id="networks">
    <div style="padding: 20px; text-align: center; color: #999;">
      Scanning for networks...
    </div>
  </div>
  
  <form action="/save" method="POST">
    <label for="ssid">Network Name (SSID)</label>
    <input type="text" id="ssid" name="ssid" required>
    
    <label for="password">Password</label>
    <input type="password" id="password" name="password">
    
    <button type="submit">Connect</button>
  </form>
</div>

<script>
// Fetch and display available networks
fetch('/scan')
  .then(r => r.json())
  .then(data => {
    const container = document.getElementById('networks');
    if (data.networks && data.networks.length > 0) {
      container.innerHTML = data.networks.map(n => 
        `<div class="network-item" onclick="selectNetwork('${n.ssid}')">
          ${n.ssid}
          <span class="signal-strength">${n.signal}%</span>
        </div>`
      ).join('');
    } else {
      container.innerHTML = '<div style="padding: 20px; text-align: center; color: #999;">No networks found</div>';
    }
  })
  .catch(() => {
    document.getElementById('networks').innerHTML = 
      '<div style="padding: 20px; text-align: center; color: #999;">Scan failed</div>';
  });

function selectNetwork(ssid) {
  document.getElementById('ssid').value = ssid;
  document.getElementById('password').focus();
}
</script>
</body>
</html>
)rawliteral";

const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ski Clock - Connected</title>
<style>
body {
  font-family: Arial, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  margin: 0;
  padding: 20px;
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
}
.container {
  background: white;
  border-radius: 10px;
  padding: 40px;
  box-shadow: 0 10px 40px rgba(0,0,0,0.2);
  max-width: 400px;
  width: 100%;
  text-align: center;
}
.success-icon {
  font-size: 64px;
  margin-bottom: 20px;
}
h1 {
  color: #667eea;
  margin-bottom: 10px;
}
p {
  color: #666;
  line-height: 1.6;
}
</style>
</head>
<body>
<div class="container">
  <div class="success-icon">✅</div>
  <h1>Connected!</h1>
  <p>WiFi credentials saved successfully.</p>
  <p>The device will now restart and connect to your network.</p>
</div>
</body>
</html>
)rawliteral";

// Save WiFi credentials to persistent storage
void saveWiFiCredentials(const String& ssid, const String& password) {
#if defined(ESP32)
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();
#elif defined(ESP8266)
  EEPROM.begin(512);
  // Store SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(i, i < ssid.length() ? ssid[i] : 0);
  }
  // Store password
  for (int i = 0; i < 64; i++) {
    EEPROM.write(32 + i, i < password.length() ? password[i] : 0);
  }
  EEPROM.commit();
  EEPROM.end();
#endif
}

// Load WiFi credentials from persistent storage
bool loadWiFiCredentials(String& ssid, String& password) {
#if defined(ESP32)
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();
  return ssid.length() > 0;
#elif defined(ESP8266)
  EEPROM.begin(512);
  char ssidBuf[33] = {0};
  char passBuf[65] = {0};
  
  // Check if EEPROM is initialized (not 0xFF)
  uint8_t firstByte = EEPROM.read(0);
  if (firstByte == 0xFF || firstByte == 0x00) {
    EEPROM.end();
    return false;  // Uninitialized EEPROM
  }
  
  for (int i = 0; i < 32; i++) {
    ssidBuf[i] = EEPROM.read(i);
  }
  for (int i = 0; i < 64; i++) {
    passBuf[i] = EEPROM.read(32 + i);
  }
  EEPROM.end();
  
  ssid = String(ssidBuf);
  password = String(passBuf);
  
  // Verify SSID contains printable characters
  if (ssid.length() == 0) return false;
  for (unsigned int i = 0; i < ssid.length(); i++) {
    if (ssid[i] < 32 || ssid[i] > 126) {
      return false;  // Non-printable character, likely corrupted
    }
  }
  
  return true;
#endif
}

// Handle root page request
void handleRoot() {
  server.send(200, "text/html", PORTAL_HTML);
}

// Handle network scan request
void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"signal\":" + String(constrain(2 * (WiFi.RSSI(i) + 100), 0, 100)) + "}";
  }
  
  json += "]}";
  server.send(200, "application/json", json);
}

// Handle save credentials request
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    saveWiFiCredentials(ssid, password);
    
    server.send(200, "text/html", SUCCESS_HTML);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing SSID or password");
  }
}

// Handle captive portal redirect
void handleCaptivePortal() {
  server.sendHeader("Location", "http://192.168.4.1", true);
  server.send(302, "text/plain", "");
}

// Start configuration portal
void startConfigPortal() {
  Serial.println("Starting WiFi configuration portal...");
  
  // Use AP+STA mode to enable network scanning while in portal mode
#if defined(ESP32)
  WiFi.mode(WIFI_MODE_APSTA);
#else
  WiFi.mode(WIFI_AP_STA);
#endif
  
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  IPAddress apIP(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, subnet);
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/save", handleSave);
  server.onNotFound(handleCaptivePortal);
  
  server.begin();
  
  wifiConfigMode = true;
  portalStartTime = millis();
  
  Serial.println("Configuration portal started");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Perform initial scan to populate network list
  Serial.println("Scanning for networks...");
  WiFi.scanNetworks(true);  // Async scan
}

// Try to connect to WiFi with stored credentials
bool connectToWiFi() {
  String ssid, password;
  
  if (!loadWiFiCredentials(ssid, password)) {
    Serial.println("No WiFi credentials stored");
    return false;
  }
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("WiFi connection failed");
    return false;
  }
}

// Initialize WiFi - call this in setup()
void setupWiFi() {
  WiFi.persistent(false);
  
  if (!connectToWiFi()) {
    startConfigPortal();
  }
}

// Update WiFi - call this in loop()
void updateWiFi() {
  if (wifiConfigMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Optional: timeout the portal after a period
    if (millis() - portalStartTime > PORTAL_TIMEOUT_MS) {
      Serial.println("Configuration portal timeout");
      ESP.restart();
    }
  }
}

// Check if WiFi is connected
bool isWiFiConnected() {
  return !wifiConfigMode && WiFi.status() == WL_CONNECTED;
}

// Get WiFi status as string
String getWiFiStatus() {
  if (wifiConfigMode) {
    return "Config Mode";
  } else if (WiFi.status() == WL_CONNECTED) {
    return "Connected: " + WiFi.localIP().toString();
  } else {
    return "Disconnected";
  }
}

#endif
