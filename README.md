# Nexus WiFi Portal System

![Project Banner](https://via.placeholder.com/800x200.png?text=Nexus+WiFi+Portal+System)

Advanced captive portal system for ESP32 demonstrating WiFi security vulnerabilities. Educational use only.

## Features

- Realistic captive portal mimicking popular login pages
- Device fingerprinting (OS, browser, MAC address)
- Real-time monitoring via WebSocket
- Telegram notifications (optional)
- Google Sheets integration (optional)
- Admin security panel with lockout protection
- Connected device tracking

## ⚠️ Important Disclaimer

> **WARNING:** This software is intended for **EDUCATIONAL PURPOSES** and **SECURITY RESEARCH** only. Unauthorized use against networks/devices without explicit permission is **ILLEGAL**. The developer assumes **NO LIABILITY** for misuse of this tool. Always obtain written permission before testing on any network.

## Hardware Requirements

- ESP32 development board
- Micro USB cable
- Computer with Arduino IDE

## Setup Instructions

### 1. Install Dependencies
Install these libraries via Arduino Library Manager:
- WiFi
- DNSServer
- AsyncTCP
- ESPAsyncWebServer
- HTTPClient
- ArduinoJson

### 2. Configure Settings
Edit these values in the code:
```arduino
// Rogue Access Point
const char* rogueSSID = "FREE_PUBLIC_WIFI";  // Your custom AP name
const char* roguePassword = "";              // Keep empty for open network

// Real WiFi (for internet access)
const char* realSSID = "YOUR_HOME_WIFI";
const char* realPassword = "YOUR_WIFI_PASSWORD";

// Admin Panel
const char *adminPin = "SECURE_ADMIN_PIN";   // CHANGE THIS!
```

### 3. Optional Services Setup

**Telegram Notifications:**
1. Create bot with [@BotFather](https://t.me/BotFather)
2. Get your chat ID with [@userinfobot](https://t.me/userinfobot)
3. Enable by setting `ENABLE_TELEGRAM = true`

**Google Sheets Integration:**
1. Create Google Apps Script (follow online tutorials)
2. Set up form submission handling
3. Enable by setting `ENABLE_GOOGLE_SHEETS = true`

### 4. Upload to ESP32
1. Connect ESP32 via USB
2. Select correct board in Arduino IDE (ESP32 Dev Module)
3. Select appropriate COM port
4. Compile and upload

## Usage

1. Power on the ESP32
2. Look for WiFi network named `FREE_PUBLIC_WIFI` (or your custom name)
3. Connect any device to this network
4. Attempt to browse any website - will redirect to captive portal
5. Monitor captured credentials via:
   - Serial monitor (115200 baud)
   - Admin panel at `http://192.168.4.1/admin`
   - Telegram notifications (if enabled)
   - Google Sheets (if enabled)

## Admin Panel
Access at `http://192.168.4.1/admin` using your admin PIN
- View captured credentials
- Monitor connected devices
- View system logs
- System configuration

## ⚠️ Known Issues (v1.0)

- **Configuration Tab Instability:** 
  The web-based configuration interface may occasionally trigger the watchdog timer causing system resets. This is being addressed for v2.0. Until then, modify settings directly in the code.

- **Memory Limitations:**
  Storing large numbers of credentials may cause stability issues. Keep MAX_LOGINS at reasonable levels (≤100).

- **WiFi Reconnection:**
  The real WiFi connection may not automatically reconnect if disrupted. Manual restart required.

## License
Apache 2.0 - See [LICENSE](LICENSE) file for details

## Contribution
Contributions are welcome! Please open an issue first to discuss proposed changes.

---
**Remember:** Always use this tool ethically and legally. Never deploy without explicit permission.




## To use this project:
1. Replace all placeholder credentials with your actual information
2. Set up optional services (Telegram/Google Sheets) if desired
3. Upload to ESP32
4. Follow README instructions for usage

The configuration tab issues will be addressed in v2.0 - for now, users should modify settings directly in the code.