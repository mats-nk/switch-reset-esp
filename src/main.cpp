// Switch Reset for ESP32 / ESP8266
// Web GUI + terminal for factory-resetting network switches over a serial console.
//
//   Browser  <--HTTP/WebSocket-->  ESP  <--UART (3.3 V TTL)-->  MAX3232  <--RS-232-->  Switch console
//
// Normal state: transparent serial terminal (browser <-> switch).
// Reset job:    ResetEngine (lib/switchreset) runs cooperatively inside loop(); while it waits it keeps
//               the web server / WebSocket serviced (see EspPlatform::sleepMs). No RTOS features are used,
//               so the same code runs on ESP32 and ESP8266.

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <stdio.h>
#include <string>
#include <vector>

#if defined(ESP8266)
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
typedef ESP8266WebServer WebServerT;
#define Con Serial    // UART0, swapped to GPIO13 (RX) / GPIO15 (TX) unless CONSOLE_NO_SWAP (ESP-01)
#define Dbg Serial1   // debug output on GPIO2 (TX only)
#ifndef CONSOLE_RX_PIN
#define CONSOLE_RX_PIN 13  // swapped UART0 (ESP-01: see CONSOLE_NO_SWAP -> GPIO3/GPIO1)
#endif
#ifndef CONSOLE_TX_PIN
#define CONSOLE_TX_PIN 15
#endif
#define OUT_MAX_BYTES 6000
#define HISTORY_MAX 2000
#else
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
typedef WebServer WebServerT;
static HardwareSerial Con(1);  // UART1 on any pins
#define Dbg Serial
#ifndef CONSOLE_RX_PIN
#define CONSOLE_RX_PIN 16
#endif
#ifndef CONSOLE_TX_PIN
#define CONSOLE_TX_PIN 17
#endif
#define OUT_MAX_BYTES 24000
#define HISTORY_MAX 6000
#endif

#include "engine.h"
#include "profiles.h"
#include "web_ui.h"

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

static const char* HOSTNAME = "switch-reset";
static const char* SETUP_AP = "SwitchReset-Setup";
static const char* SETUP_AP_PASSWORD = "switchreset";
static const char* DEFAULT_WEB_PASSWORD = "admin";

static WebServerT http(80);
static WebSocketsServer wsServer(81);

static String webPassword;
static bool authed[8];

static bool jobBusy = false;
static int pendingJob = -1;
static int pendingMode = 0;
static bool abortFlag = false;
static bool answerReady = false;
static std::string answerText;
static uint32_t currentBaud = 9600;

// ---- password storage ------------------------------------------------------------------
#if defined(ESP8266)
static String loadPassword() {
  EEPROM.begin(64);
  if (EEPROM.read(0) != 0xA5) return DEFAULT_WEB_PASSWORD;
  int n = EEPROM.read(1);
  if (n < 4 || n > 32) return DEFAULT_WEB_PASSWORD;
  String s;
  for (int i = 0; i < n; i++) s += (char)EEPROM.read(2 + i);
  return s;
}
static void savePassword(const String& pw) {
  EEPROM.write(0, 0xA5);
  EEPROM.write(1, (uint8_t)pw.length());
  for (size_t i = 0; i < pw.length(); i++) EEPROM.write(2 + i, (uint8_t)pw.c_str()[i]);
  EEPROM.commit();
}
#else
static Preferences prefs;
static String loadPassword() {
  prefs.begin("swreset", false);
  return prefs.getString("pw", DEFAULT_WEB_PASSWORD);
}
static void savePassword(const String& pw) { prefs.putString("pw", pw); }
#endif

// ---- outgoing message queue (engine -> WebSocket) ------------------------------------------
// Message = one type character + payload:
//   T terminal data   L<level>log line (i/o/w/e/d)   Q<0|1>label  question for the user (1 = secret)
//   S<busy>:<baud>    D<0|1> job finished (1 = ok)   A<0|1> authentication result
static std::vector<std::string> outQ;
static size_t outBytes = 0;
static std::string termHistory;

static void pushOut(char type, const std::string& payload) {
  if (type == 'T') {
    if (outBytes > OUT_MAX_BYTES) return;  // drop terminal data when the browser cannot keep up
    if (!outQ.empty() && outQ.back()[0] == 'T' && outQ.back().size() < 512) {
      outQ.back() += payload;
      outBytes += payload.size();
      return;
    }
  } else if (outQ.size() > 300) {
    return;
  }
  std::string m(1, type);
  m += payload;
  outBytes += m.size();
  outQ.push_back(m);
}

static void pushStatus() {
  char b[32];
  snprintf(b, sizeof b, "%d:%lu", jobBusy ? 1 : 0, (unsigned long)currentBaud);
  pushOut('S', b);
}

static void flushOut() {
  if (outQ.empty()) return;
  std::vector<std::string> local;
  local.swap(outQ);
  outBytes = 0;
  for (size_t k = 0; k < local.size(); k++) {
    const std::string& m = local[k];
    if (m[0] == 'T') {
      termHistory.append(m, 1, std::string::npos);
      if (termHistory.size() > HISTORY_MAX) termHistory.erase(0, termHistory.size() - HISTORY_MAX * 2 / 3);
    }
    for (int i = 0; i < 8; i++)
      if (authed[i] && wsServer.clientIsConnected(i)) wsServer.sendTXT(i, m.c_str(), m.size());
  }
}

// ---- console UART ---------------------------------------------------------------------
static bool consoleStarted = false;

static void openConsole(uint32_t baud) {
#if defined(ESP8266)
  if (!consoleStarted) {
    Con.setRxBufferSize(1024);
    Con.begin(baud, SERIAL_8N1);
#if !defined(CONSOLE_NO_SWAP)
    Con.swap();  // UART0 -> GPIO13 (RX) / GPIO15 (TX)
#endif
    consoleStarted = true;
  } else {
    Con.updateBaudRate(baud);
  }
#else
  Con.end();
  Con.setRxBufferSize(2048);
  Con.begin(baud, SERIAL_8N1, CONSOLE_RX_PIN, CONSOLE_TX_PIN);
  consoleStarted = true;
#endif
  currentBaud = baud;
}

// Idle mode: show everything the switch prints in the browser terminal.
static void pumpIdle() {
  if (jobBusy) return;
  std::string raw;
  int n = 0;
  while (Con.available() && n < 256) {
    int s = tu::sanitizeForTerminal(Con.read());
    n++;
    if (s >= 0) raw.push_back((char)s);
  }
  if (!raw.empty()) pushOut('T', raw);
}

// Keeps the network side alive; called whenever the engine waits.
static void service() {
  http.handleClient();
  wsServer.loop();
  ArduinoOTA.handle();
#if defined(ESP8266)
  MDNS.update();
#endif
  flushOut();
}

// ---- Platform for the engine ----------------------------------------------------------
class EspPlatform : public Platform {
 public:
  int readByte() override { return Con.available() ? Con.read() : -1; }
  void writeStr(const std::string& s) override {
    Con.write((const uint8_t*)s.data(), s.size());
    Con.flush();
  }
  void flushInput() override {
    while (Con.available()) Con.read();
  }
  void openPort(uint32_t baud) override { openConsole(baud); }
  uint32_t nowMs() override { return millis(); }
  void sleepMs(uint32_t ms) override {
    delay(ms);
    service();
  }
  void terminalOut(const std::string& raw) override { pushOut('T', raw); }
  void log(char level, const std::string& msg) override {
    std::string p(1, level);
    p += msg;
    pushOut('L', p);
  }
  bool ask(const std::string& label, bool secret, std::string& answer) override {
    answerReady = false;
    pushOut('Q', std::string(secret ? "1" : "0") + label);
    uint32_t t0 = millis();
    while (!answerReady) {
      if (abortFlag || millis() - t0 > 300000UL) return false;
      sleepMs(50);
    }
    answer = answerText;
    answerReady = false;
    return true;
  }
  bool aborted() override { return abortFlag; }
  void baudChanged(uint32_t) override { pushStatus(); }
};

static void runJob(int idx, int mode) {
  bool ok;
  {
    EspPlatform platform;
    ResetEngine engine(platform);
    ok = engine.run(PROFILES[idx], mode);
  }
  pushOut('D', ok ? "1" : "0");
  jobBusy = false;
  pushStatus();
}

// Called from the WebSocket handler: only marks the job, loop() runs it.
static bool startReset(int idx, int mode, std::string& why) {
  if (idx < 0 || idx >= PROFILE_COUNT) {
    why = "Invalid switch type.";
    return false;
  }
  if (mode < 0 || mode >= PROFILES[idx].modeCount) {
    why = "Invalid reset mode.";
    return false;
  }
  if (jobBusy) {
    why = "A reset is already running.";
    return false;
  }
  jobBusy = true;
  abortFlag = false;
  answerReady = false;
  pendingJob = idx;
  pendingMode = mode;
  pushStatus();
  return true;
}

// ---- WebSocket ------------------------------------------------------------------------
static void wsReply(uint8_t num, const std::string& m) { wsServer.sendTXT(num, m.c_str(), m.size()); }

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (num >= 8) return;
  switch (type) {
    case WStype_DISCONNECTED: authed[num] = false; return;
    case WStype_CONNECTED: authed[num] = false; return;
    case WStype_TEXT: break;
    default: return;
  }
  if (length == 0) return;
  char cmd = (char)payload[0];
  std::string arg((const char*)payload + 1, length - 1);

  if (cmd == 'P') {  // password
    if (String(arg.c_str()) == webPassword) {
      authed[num] = true;
      wsReply(num, "A1");
      char b[32];
      snprintf(b, sizeof b, "S%d:%lu", jobBusy ? 1 : 0, (unsigned long)currentBaud);
      wsReply(num, b);
      if (!termHistory.empty()) wsReply(num, "T" + termHistory);
    } else {
      delay(400);  // slow down guessing
      wsReply(num, "A0");
    }
    return;
  }
  if (!authed[num]) return;

  switch (cmd) {
    case 'r':  // raw bytes to the switch (terminal)
      if (!jobBusy) Con.write((const uint8_t*)arg.data(), arg.size());
      break;
    case 'b': {  // terminal baud rate
      long baud = atol(arg.c_str());
      if (!jobBusy && baud >= 1200 && baud <= 115200) {
        openConsole((uint32_t)baud);
        pushStatus();
      }
      break;
    }
    case 's': {  // start reset: s<profile index>,<mode index>
      std::string why;
      int mode = 0;
      size_t comma = arg.find(',');
      if (comma != std::string::npos) mode = atoi(arg.c_str() + comma + 1);
      if (!startReset(atoi(arg.c_str()), mode, why)) pushOut('L', "e" + why);
      break;
    }
    case 'a':  // answer to a question (username/password)
      answerText = arg;
      answerReady = true;
      break;
    case 'x':  // abort
      abortFlag = true;
      break;
    default: break;
  }
}

// ---- HTTP -----------------------------------------------------------------------------
static String jsonEsc(const char* s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') o += '\\';
    o += *s;
  }
  return o;
}

// Streamed in chunks (one chunk per switch type) to keep heap usage low on the ESP8266.
static void handleProfiles() {
  http.setContentLength(CONTENT_LENGTH_UNKNOWN);
  http.send(200, "application/json", "");
  http.sendContent("[");
  for (int i = 0; i < PROFILE_COUNT; i++) {
    const Profile& p = PROFILES[i];
    String j;
    if (i) j += ',';
    j += "{\"name\":\"" + jsonEsc(p.name) + "\",\"baud\":" + String((unsigned long)p.baud) + ",\"hint\":\"" +
         jsonEsc(p.hint) + "\",\"modes\":[";
    for (int m = 0; m < p.modeCount; m++) {
      const Mode& md = p.modes[m];
      if (m) j += ',';
      j += "{\"name\":\"" + jsonEsc(md.name) + "\",\"desc\":\"" + jsonEsc(md.description) + "\",\"steps\":[";
      for (int k = 0; k < md.stepCount; k++) {
        if (k) j += ',';
        j += "{\"d\":\"" + jsonEsc(md.steps[k].description) + "\",\"c\":\"" + jsonEsc(md.steps[k].command) + "\"}";
      }
      j += "]}";
    }
    j += "]}";
    http.sendContent(j);
  }
  http.sendContent("]");
  http.sendContent("");
}

static void handleInfo() {
  String j = "{\"defaultPassword\":";
  j += (webPassword == DEFAULT_WEB_PASSWORD) ? "true" : "false";
  j += ",\"rxPin\":" + String(CONSOLE_RX_PIN) + ",\"txPin\":" + String(CONSOLE_TX_PIN) + "}";
  http.send(200, "application/json", j);
}

// ---- setup / loop ---------------------------------------------------------------------
void setup() {
#if defined(ESP8266)
  Dbg.begin(115200);  // TX-only debug UART on GPIO2
#else
  Dbg.begin(115200);
#endif
  webPassword = loadPassword();

  // Hold the BOOT/FLASH button for 3 s at power-up: forget WiFi settings and reset the web password.
  bool factory = false;
#if BOOT_BUTTON_PIN >= 0  // ESP-01 has no usable button pin (GPIO0 low at boot = flash mode)
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    uint32_t t0 = millis();
    while (digitalRead(BOOT_BUTTON_PIN) == LOW && millis() - t0 < 3000) delay(20);
    factory = millis() - t0 >= 3000;
  }
#endif

  openConsole(9600);

  WiFi.mode(WIFI_STA);
#if defined(ESP8266)
  WiFi.hostname(HOSTNAME);
#else
  WiFi.setHostname(HOSTNAME);
#endif
  WiFiManager wm;
  if (factory) {
    wm.resetSettings();
    webPassword = DEFAULT_WEB_PASSWORD;
    savePassword(webPassword);
    Dbg.println("Factory reset: WiFi settings cleared, web password reset to default.");
  }
  WiFiManagerParameter pwParam("pw", "Web UI password (min. 4 characters)", webPassword.c_str(), 32);
  wm.addParameter(&pwParam);
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(20);
  if (!wm.autoConnect(SETUP_AP, SETUP_AP_PASSWORD)) {
    Dbg.println("WiFi setup timed out - restarting.");
    delay(1000);
    ESP.restart();
  }
  String np = pwParam.getValue();
  if (np.length() >= 4 && np.length() <= 32 && np != webPassword) {
    webPassword = np;
    savePassword(webPassword);
  }

  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);

  http.on("/", HTTP_GET, []() { http.send_P(200, "text/html", INDEX_HTML); });
  http.on("/api/profiles", HTTP_GET, handleProfiles);
  http.on("/api/info", HTTP_GET, handleInfo);
  http.onNotFound([]() { http.send(404, "text/plain", "Not found"); });
  http.begin();

  wsServer.begin();
  wsServer.onEvent(onWsEvent);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(webPassword.c_str());
  ArduinoOTA.begin();

  Dbg.printf("Ready: http://%s.local/  or  http://%s/\n", HOSTNAME, WiFi.localIP().toString().c_str());
}

void loop() {
  service();
  pumpIdle();
  if (pendingJob >= 0) {
    int idx = pendingJob;
    pendingJob = -1;
    runJob(idx, pendingMode);
  }
  delay(2);
}
