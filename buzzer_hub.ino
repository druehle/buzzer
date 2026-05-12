#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
#include <PicoMQTT.h>

#ifdef ESP8266
  #define TFT_CS 0
  #define TFT_DC 15
#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32C6)
  #define TFT_CS 7
  #define TFT_DC 8
#elif defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3)
  #define TFT_CS 15
  #define TFT_DC 33
#else
  #define TFT_CS 9
  #define TFT_DC 10
#endif

#define TFT_RST -1

Adafruit_HX8357 tft(TFT_CS, TFT_DC, TFT_RST);

// ============================================================
// WiFi AP settings
// ============================================================
const char* AP_SSID = "BuzzerHub";
const char* AP_PASS = "buzzertest";
const int   AP_CHANNEL = 1;
const int   AP_MAX_CLIENTS = 8;

// ============================================================
// OTA settings
// ============================================================
const char* OTA_HOSTNAME = "buzzer-hub";
const char* OTA_PASSWORD = "buzzertest";

// ============================================================
// MQTT broker
// ============================================================
PicoMQTT::Server mqtt;

// ============================================================
// Screen layout
// ============================================================
const int SCREEN_W = 480;
const int SCREEN_H = 320;

const int BOTTOM_H = 46;
const int BOTTOM_Y = SCREEN_H - BOTTOM_H;
const int TOP_H    = BOTTOM_Y - 2;

const int RESET_PROMPT_X = 54;
const int RESET_PROMPT_Y = 92;
const int RESET_PROMPT_W = 372;
const int RESET_PROMPT_H = 118;
const int RESET_PROMPT_R = 12;

// ============================================================
// Colors
// ============================================================
uint16_t BLUE_DARK;
uint16_t BLUE_MID;
uint16_t BLUE_BRIGHT;
uint16_t BLUE_LIGHT;
uint16_t CYAN_GLOW;
uint16_t BLACKISH;
uint16_t WHITE_SOFT;
uint16_t GRAY_DARK;
uint16_t GRAY_MED;
uint16_t STATUS_READY;
uint16_t STATUS_BUZZED;
uint16_t STATUS_LOWBAT;

// ============================================================
// Button state model
// ============================================================
enum ButtonState {
  STATE_OFF,
  STATE_READY,
  STATE_BUZZED,
  STATE_LOWBAT
};

struct ButtonInfo {
  const char* colorName;
  const char* shortName;
  uint16_t color;
  ButtonState state;
  bool inBuzzList;
  int battery;
  int id;
};

ButtonInfo buttons[8] = {
  { "RED",    "RED",  0, STATE_OFF, false, -1, 1 },
  { "ORANGE", "ORG",  0, STATE_OFF, false, -1, 2 },
  { "YELLOW", "YLW",  0, STATE_OFF, false, -1, 3 },
  { "GREEN",  "GRN",  0, STATE_OFF, false, -1, 4 },
  { "BLUE",   "BLU",  0, STATE_OFF, false, -1, 5 },
  { "PURPLE", "PUR",  0, STATE_OFF, false, -1, 6 },
  { "PINK",   "PNK",  0, STATE_OFF, false, -1, 7 },
  { "BROWN",  "BRN",  0, STATE_OFF, false, -1, 8 }
};

int buzzOrder[8];
int buzzCount = 0;
int roundNumber = 1;

unsigned long buzzTimes[8];
unsigned long firstBuzzTime = 0;

#define BTN_RESET  14
#define BTN_NEXT   32

// ============================================================
// App state
// ============================================================
String lastTopic = "";
String lastPayload = "";
unsigned long messageCount = 0;
unsigned long lastStatusPublish = 0;

unsigned long lastNextButtonTime = 0;
const unsigned long nextDebounceDelay = 1000;

const unsigned long resetHoldRequiredMs = 5000;
bool resetHoldActive = false;
bool resetHoldCompleted = false;
unsigned long resetHoldStartMs = 0;
unsigned long lastResetPromptUpdateMs = 0;
int lastResetPromptSeconds = -1;

// ============================================================
// Forward declarations
// ============================================================
void startAccessPoint();
void setupOTA();
void startBroker();

void drawBootScreen();
void drawBootBackground();
void drawBurst(int cx, int cy);
void drawGlow(int cx, int cy);
void drawSidePattern();
void drawBootTitle();
void updateBootFeature(const char* feature);

void drawMainScreen();
void drawBuzzHeader();
void drawBuzzGrid();
void drawBuzzSlot(int slotIndex);
void drawBottomStatusRow();
void drawBottomButtonBox(int index);

void handleMqttMessage(const char* topic, const char* payload);
void publishStatus();

int getButtonIndexByName(const String& name);
int getButtonIndexById(int id);
const char* stateToString(ButtonState state);
uint16_t stateToColor(ButtonState state);
void setButtonState(int buttonIndex, ButtonState state);
void addBuzz(int buttonIndex);
void resetRoundAndPoll();
void handlePhysicalButtons();
void drawResetHoldPrompt(unsigned long heldMs);
void clearResetHoldPrompt();
bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);

String jsonGetString(const String& json, const String& key);
int jsonGetInt(const String& json, const String& key);
bool jsonGetBool(const String& json, const String& key);
bool jsonGetBoolDefault(const String& json, const String& key, bool defaultValue);

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(250);

  tft.begin();
  tft.setRotation(3);

  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);

  BLUE_DARK    = tft.color565(0, 18, 90);
  BLUE_MID     = tft.color565(0, 55, 180);
  BLUE_BRIGHT  = tft.color565(0, 115, 255);
  BLUE_LIGHT   = tft.color565(90, 210, 255);
  CYAN_GLOW    = tft.color565(180, 245, 255);
  BLACKISH     = tft.color565(0, 0, 0);
  WHITE_SOFT   = tft.color565(230, 245, 255);
  GRAY_DARK    = tft.color565(28, 28, 34);
  GRAY_MED     = tft.color565(95, 95, 105);

  STATUS_READY  = tft.color565(46, 204, 113);
  STATUS_BUZZED = tft.color565(241, 196, 15);
  STATUS_LOWBAT = tft.color565(231, 76, 60);

  buttons[0].color = tft.color565(231, 76, 60);
  buttons[1].color = tft.color565(230, 126, 34);
  buttons[2].color = tft.color565(241, 196, 15);
  buttons[3].color = tft.color565(46, 204, 113);
  buttons[4].color = tft.color565(52, 152, 219);
  buttons[5].color = tft.color565(155, 89, 182);
  buttons[6].color = tft.color565(255, 95, 162);
  buttons[7].color = tft.color565(142, 90, 60);

  for (int i = 0; i < 8; i++) {
    buzzOrder[i] = -1;
    buzzTimes[i] = 0;
  }

  drawBootScreen();

  updateBootFeature("Initializing display");
  delay(250);

  updateBootFeature("Starting WiFi AP");
  startAccessPoint();

  updateBootFeature("Starting OTA");
  setupOTA();

  updateBootFeature("Starting MQTT broker");
  startBroker();

  updateBootFeature("Loading main screen");
  delay(300);

  drawMainScreen();
}

// ============================================================
// Loop
// ============================================================
void loop() {
  ArduinoOTA.handle();

  mqtt.loop();

  handlePhysicalButtons();

  if (millis() - lastStatusPublish >= 5000) {
    lastStatusPublish = millis();
    publishStatus();
  }
}

// ============================================================
// WiFi AP
// ============================================================
void startAccessPoint() {
  WiFi.mode(WIFI_AP);

  bool ok = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CLIENTS);

  if (!ok) {
    Serial.println("Failed to start AP");
    updateBootFeature("WiFi AP failed");

    while (true) {
      ArduinoOTA.handle();
      delay(1000);
    }
  }

  Serial.println();
  Serial.println("=== Access Point Started ===");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// ============================================================
// OTA
// ============================================================
void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println();
    Serial.println("=== OTA UPDATE STARTING ===");

    tft.fillScreen(HX8357_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(HX8357_CYAN);
    tft.setCursor(40, 120);
    tft.print("OTA UPDATE");
    tft.setTextSize(2);
    tft.setCursor(40, 170);
    tft.print("Do not power off");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println();
    Serial.println("=== OTA UPDATE COMPLETE ===");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static int lastPct = -1;

    int pct = (progress * 100) / total;

    if (pct != lastPct) {
      lastPct = pct;

      Serial.print("OTA Progress: ");
      Serial.print(pct);
      Serial.println("%");

      tft.fillRect(40, 215, 400, 28, HX8357_BLACK);
      tft.drawRect(40, 215, 400, 28, HX8357_WHITE);
      tft.fillRect(42, 217, map(pct, 0, 100, 0, 396), 24, HX8357_CYAN);

      tft.fillRect(185, 255, 120, 28, HX8357_BLACK);
      tft.setTextSize(2);
      tft.setTextColor(HX8357_WHITE);
      tft.setCursor(195, 260);
      tft.print(pct);
      tft.print("%");
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print("OTA Error [");
    Serial.print(error);
    Serial.print("]: ");

    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }

    tft.fillScreen(HX8357_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(HX8357_RED);
    tft.setCursor(40, 120);
    tft.print("OTA ERROR");
  });

  ArduinoOTA.begin();

  Serial.println("=== OTA Ready ===");
  Serial.print("Hostname: ");
  Serial.println(OTA_HOSTNAME);
  Serial.print("Password: ");
  Serial.println(OTA_PASSWORD);
  Serial.print("OTA IP: ");
  Serial.println(WiFi.softAPIP());
}

// ============================================================
// MQTT broker
// ============================================================
void startBroker() {
  mqtt.subscribe("#", [](const char* topic, const char* payload) {
    handleMqttMessage(topic, payload);
  });

  mqtt.begin();

  Serial.println("=== MQTT Broker Started ===");
  Serial.println("Broker listening on port 1883");
}

// ============================================================
// MQTT handling
// ============================================================
void handleMqttMessage(const char* topic, const char* payload) {
  messageCount++;

  lastTopic = topic ? topic : "";
  lastPayload = payload ? payload : "";

  Serial.println();
  Serial.println("=== MQTT MESSAGE RECEIVED ===");
  Serial.print("Topic: ");
  Serial.println(lastTopic);
  Serial.print("Payload: ");
  Serial.println(lastPayload);

  String t = lastTopic;
  String p = lastPayload;
  t.trim();
  p.trim();

  if (t.equalsIgnoreCase("buzzer/all/reset") ||
      (t.equalsIgnoreCase("hub/reset") && (p == "1" || p.equalsIgnoreCase("reset")))) {

    Serial.println("Matched round reset");
    resetRoundAndPoll();
    mqtt.publish("hub/ack", "reset");
    return;
  }

  if (!t.startsWith("buzzer/")) {
    Serial.println("Topic not handled");
    return;
  }

  int slash1 = t.indexOf('/');
  int slash2 = t.indexOf('/', slash1 + 1);

  if (slash2 <= 0) {
    Serial.println("Topic format invalid, expected buzzer/<id>/<action>");
    return;
  }

  String idStr = t.substring(slash1 + 1, slash2);
  String action = t.substring(slash2 + 1);
  idStr.trim();
  action.trim();
  action.toLowerCase();

  int topicId = idStr.toInt();

  String colorName = jsonGetString(p, "name");
  String eventName = jsonGetString(p, "event");
  int payloadId = jsonGetInt(p, "id");
  int battery = jsonGetInt(p, "battery");
  bool armed = jsonGetBoolDefault(p, "armed", true);

  int idx = -1;

  if (colorName.length() > 0) {
    idx = getButtonIndexByName(colorName);
  }

  if (idx < 0 && payloadId > 0) {
    idx = getButtonIndexById(payloadId);
  }

  if (idx < 0 && topicId > 0) {
    idx = getButtonIndexById(topicId);
  }

  if (idx < 0) {
    Serial.println("No matching button found");
    return;
  }

  if (battery >= 0) {
    buttons[idx].battery = battery;
  }

  bool changed = false;

  bool isPress =
    (action == "press") ||
    eventName.equalsIgnoreCase("press");

  bool isBuzzed =
    eventName.equalsIgnoreCase("buzzed") ||
    eventName.equalsIgnoreCase("buzz");

  bool isReady =
    eventName.equalsIgnoreCase("ready") ||
    eventName.equalsIgnoreCase("online") ||
    eventName.equalsIgnoreCase("status");

  bool isLowbat =
    eventName.equalsIgnoreCase("lowbat");

  // LWT / offline events.
  // Your buzzer button LWT should publish something like:
  // Topic:   buzzer/1/status
  // Payload: {"battery":0,"event":"off"}
  bool isOff =
    eventName.equalsIgnoreCase("off") ||
    eventName.equalsIgnoreCase("offline") ||
    eventName.equalsIgnoreCase("disconnect") ||
    eventName.equalsIgnoreCase("disconnected") ||
    eventName.equalsIgnoreCase("lwt");

  // IMPORTANT:
  // Trust the event field first.
  // Example:
  // {"battery":0,"event":"ready"} will display READY.
  // {"event":"lowbat"} will display LOWBAT.
  // {"event":"press"}, "buzz", or "buzzed" will display BUZZED and enter the scoreboard.
  // {"battery":0,"event":"off"} is the LWT payload and will display OFF.
  if (isOff) {
    Serial.println("OFF/LWT received. Setting button OFF.");

    buttons[idx].battery = -1;
    setButtonState(idx, STATE_OFF);

    // Keep the button in the buzz order if it already buzzed.
    // LWT should only update the bottom status row to OFF.

    changed = true;
  } else if (isPress || isBuzzed) {
    setButtonState(idx, STATE_BUZZED);
    addBuzz(idx);
    changed = true;
  } else if (isLowbat) {
    setButtonState(idx, STATE_LOWBAT);
    changed = true;
  } else if (isReady) {
    // Do not let a later READY heartbeat erase a buzzed button mid-round.
    if (buttons[idx].state != STATE_BUZZED) {
      setButtonState(idx, STATE_READY);
      changed = true;
    } else {
      Serial.println("READY received, but button is already BUZZED this round. Ignoring READY.");
    }
  } else {
    Serial.println("Known buzzer, but event was not handled.");
  }

  if (changed) {
    drawBottomButtonBox(idx);

    for (int i = 0; i < 8; i++) {
      drawBuzzSlot(i);
    }

    mqtt.publish("hub/ack", ("received:" + t).c_str());
  }
}

// ============================================================
// Publish broker status
// ============================================================
void publishStatus() {
  String status = String("{\"messages\":") + messageCount +
                  ",\"clients\":" + WiFi.softAPgetStationNum() +
                  ",\"buzzCount\":" + buzzCount + "}";

  mqtt.publish("hub/status", status.c_str());
}

// ============================================================
// Boot UI
// ============================================================
void drawBootScreen() {
  drawBootBackground();
  drawBootTitle();
  updateBootFeature("Booting...");
}

void drawBootBackground() {
  tft.fillScreen(BLUE_DARK);

  int cx = 240;
  int cy = 120;

  drawBurst(cx, cy);
  drawGlow(cx, cy);
  drawSidePattern();
}

void drawBurst(int cx, int cy) {
  for (int x = 0; x < SCREEN_W; x += 12) {
    uint16_t c = (abs(x - cx) < 100) ? BLUE_LIGHT : BLUE_BRIGHT;
    tft.drawLine(cx, cy, x, 0, c);
    tft.drawLine(cx, cy, x, SCREEN_H - 1, c);
  }

  for (int y = 0; y < SCREEN_H; y += 12) {
    uint16_t c = (abs(y - cy) < 80) ? BLUE_LIGHT : BLUE_MID;
    tft.drawLine(cx, cy, 0, y, c);
    tft.drawLine(cx, cy, SCREEN_W - 1, y, c);
  }
}

void drawGlow(int cx, int cy) {
  tft.fillCircle(cx, cy, 54, BLUE_MID);
  tft.fillCircle(cx, cy, 40, BLUE_BRIGHT);
  tft.fillCircle(cx, cy, 26, BLUE_LIGHT);
  tft.fillCircle(cx, cy, 12, CYAN_GLOW);

  tft.drawFastHLine(35, cy + 2, 410, BLUE_LIGHT);
  tft.drawFastHLine(35, cy + 3, 410, BLUE_BRIGHT);
}

void drawSidePattern() {
  int r = 12;

  for (int y = 36; y < 260; y += 28) {
    int offset = ((y / 28) % 2) ? 10 : 0;

    for (int x = 20 + offset; x < 90; x += 24) {
      tft.drawCircle(x, y, r, BLUE_BRIGHT);
    }

    for (int x = 460 - offset; x > 390; x -= 24) {
      tft.drawCircle(x, y, r, BLUE_BRIGHT);
    }
  }
}

void drawBootTitle() {
  int plateX = 70;
  int plateY = 88;
  int plateW = 340;
  int plateH = 98;

  tft.fillRoundRect(plateX, plateY, plateW, plateH, 12, BLACKISH);
  tft.drawRoundRect(plateX, plateY, plateW, plateH, 12, BLUE_LIGHT);
  tft.drawRoundRect(plateX + 2, plateY + 2, plateW - 4, plateH - 4, 10, BLUE_BRIGHT);

  tft.setTextSize(4);
  tft.setTextColor(BLUE_DARK);
  tft.setCursor(108, 104);
  tft.print("BUZZER");

  tft.setTextColor(CYAN_GLOW);
  tft.setCursor(104, 100);
  tft.print("BUZZER");

  tft.setTextColor(BLUE_DARK);
  tft.setCursor(108, 146);
  tft.print("SYSTEM");

  tft.setTextColor(WHITE_SOFT);
  tft.setCursor(104, 142);
  tft.print("SYSTEM");
}

void updateBootFeature(const char* feature) {
  tft.fillRect(50, 210, 380, 60, BLUE_DARK);
  tft.drawRoundRect(60, 214, 360, 42, 8, BLUE_BRIGHT);

  tft.setTextSize(2);
  tft.setTextColor(BLUE_LIGHT);
  tft.setCursor(150, 190);
  tft.print("Starting:");

  tft.setTextColor(WHITE_SOFT);

  int len = strlen(feature);
  int textX = 240 - (len * 6);
  if (textX < 75) textX = 75;

  tft.setCursor(textX, 226);
  tft.print(feature);
}

// ============================================================
// Main UI
// ============================================================
void drawMainScreen() {
  tft.fillScreen(HX8357_BLACK);
  drawBuzzGrid();
  drawBottomStatusRow();
}

void drawBuzzHeader() {
  tft.fillRect(0, 0, SCREEN_W, 24, HX8357_BLACK);

  tft.setTextColor(HX8357_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 8);

  tft.print("Round #");
  tft.print(roundNumber);
}

void drawBuzzGrid() {
  drawBuzzHeader();

  for (int i = 0; i < 8; i++) {
    drawBuzzSlot(i);
  }

  tft.drawFastHLine(0, TOP_H, SCREEN_W, GRAY_MED);
}

void drawBuzzSlot(int slotIndex) {
  const int margin = 8;
  const int headerH = 26;
  const int gap = 5;

  const int slotW = (SCREEN_W - (margin * 3)) / 2;
  const int slotH = (TOP_H - headerH - (gap * 3) - 2) / 4;

  const int leftX = margin;
  const int rightX = margin * 2 + slotW;

  int col = (slotIndex < 4) ? 0 : 1;
  int row = (slotIndex < 4) ? slotIndex : slotIndex - 4;

  int x = (col == 0) ? leftX : rightX;
  int y = headerH + (row * (slotH + gap));

  tft.fillRoundRect(x, y, slotW, slotH, 8, GRAY_DARK);
  tft.drawRoundRect(x, y, slotW, slotH, 8, GRAY_MED);

  tft.setTextSize(3);
  tft.setTextColor(HX8357_YELLOW);
  tft.setCursor(x + 10, y + 15);
  tft.print(slotIndex + 1);

  if (buzzOrder[slotIndex] >= 0) {
    int btn = buzzOrder[slotIndex];

    tft.setTextSize(3);
    tft.setTextColor(buttons[btn].color);
    tft.setCursor(x + 42, y + 15);
    tft.print(buttons[btn].colorName);

    char timeText[16];

    if (slotIndex == 0) {
      snprintf(timeText, sizeof(timeText), "0.00");
    } else {
      float elapsed = (buzzTimes[slotIndex] - firstBuzzTime) / 1000.0;
      snprintf(timeText, sizeof(timeText), "+%.2f", elapsed);
    }

    tft.setTextSize(2);
    tft.setTextColor(HX8357_WHITE);

    int textLen = strlen(timeText);
    int timeX = x + slotW - (textLen * 12) - 10;
    if (timeX < x + 140) timeX = x + 140;

    tft.setCursor(timeX, y + 20);
    tft.print(timeText);
  }
}

void drawBottomStatusRow() {
  for (int i = 0; i < 8; i++) {
    drawBottomButtonBox(i);
  }
}

void drawBottomButtonBox(int index) {
  const int boxW = SCREEN_W / 8;
  const int x = index * boxW;
  const int y = BOTTOM_Y;
  const int h = SCREEN_H - BOTTOM_Y;

  tft.fillRect(x, y, boxW, h, HX8357_BLACK);
  tft.drawRect(x + 1, y + 1, boxW - 2, h - 2, buttons[index].color);

  tft.setTextSize(2);
  tft.setTextColor(buttons[index].color);

  int nameLen = strlen(buttons[index].shortName);
  int nameX = x + (boxW / 2) - (nameLen * 6);
  int nameY = y + 6;

  tft.setCursor(nameX, nameY);
  tft.print(buttons[index].shortName);

  const char* statusText = stateToString(buttons[index].state);
  tft.setTextSize(1);
  tft.setTextColor(stateToColor(buttons[index].state));

  int statusLen = strlen(statusText);
  int statusX = x + (boxW / 2) - (statusLen * 3);
  int statusY = y + 29;

  if (statusX < x + 2) statusX = x + 2;

  tft.setCursor(statusX, statusY);
  tft.print(statusText);
}

// ============================================================
// Helpers
// ============================================================
int getButtonIndexByName(const String& name) {
  for (int i = 0; i < 8; i++) {
    if (name.equalsIgnoreCase(buttons[i].colorName)) {
      return i;
    }
  }

  return -1;
}

int getButtonIndexById(int id) {
  for (int i = 0; i < 8; i++) {
    if (buttons[i].id == id) {
      return i;
    }
  }

  return -1;
}

const char* stateToString(ButtonState state) {
  switch (state) {
    case STATE_OFF:    return "OFF";
    case STATE_READY:  return "READY";
    case STATE_BUZZED: return "BUZZED";
    case STATE_LOWBAT: return "LOWBAT";
    default:           return "?";
  }
}

uint16_t stateToColor(ButtonState state) {
  switch (state) {
    case STATE_OFF:    return GRAY_MED;
    case STATE_READY:  return STATUS_READY;
    case STATE_BUZZED: return STATUS_BUZZED;
    case STATE_LOWBAT: return STATUS_LOWBAT;
    default:           return HX8357_WHITE;
  }
}

void setButtonState(int buttonIndex, ButtonState state) {
  if (buttonIndex < 0 || buttonIndex > 7) return;

  buttons[buttonIndex].state = state;

  Serial.print("Button ");
  Serial.print(buttons[buttonIndex].colorName);
  Serial.print(" new state = ");
  Serial.println(stateToString(state));
}

void addBuzz(int buttonIndex) {
  if (buttonIndex < 0 || buttonIndex > 7) return;
  if (buttons[buttonIndex].inBuzzList) return;
  if (buzzCount >= 8) return;

  unsigned long now = millis();

  if (buzzCount == 0) {
    firstBuzzTime = now;
  }

  buzzOrder[buzzCount] = buttonIndex;
  buzzTimes[buzzCount] = now;
  buttons[buttonIndex].inBuzzList = true;

  Serial.print("Added to buzz order slot ");
  Serial.print(buzzCount + 1);
  Serial.print(": ");
  Serial.println(buttons[buttonIndex].colorName);

  buzzCount++;
}

void resetRoundAndPoll() {
  Serial.println("Resetting round and polling buzzers");

  buzzCount = 0;
  firstBuzzTime = 0;

  for (int i = 0; i < 8; i++) {
    buzzTimes[i] = 0;
    buzzOrder[i] = -1;
    buttons[i].inBuzzList = false;
    buttons[i].state = STATE_OFF;
  }

  drawBuzzHeader();

  for (int i = 0; i < 8; i++) {
    drawBuzzSlot(i);
    drawBottomButtonBox(i);
  }

  String resetMsg = String("{\"reset\":true,\"round\":") + roundNumber + "}";

  mqtt.publish("buzzer/all/reset", resetMsg.c_str());
  mqtt.publish("buzzer/all/next_round", resetMsg.c_str());
  mqtt.publish("hub/poll", "{\"poll\":true}");

  Serial.println("Published buzzer/all/reset");
  Serial.println("Published buzzer/all/next_round");
  Serial.println("Published hub/poll");
}

// ============================================================
// Physical buttons
// ============================================================
void handlePhysicalButtons() {
  bool resetPressed = (digitalRead(BTN_RESET) == LOW);
  bool nextPressed  = (digitalRead(BTN_NEXT) == LOW);
  unsigned long now = millis();

  if (resetPressed) {
    if (!resetHoldActive) {
      resetHoldActive = true;
      resetHoldCompleted = false;
      resetHoldStartMs = now;
      lastResetPromptUpdateMs = 0;
      lastResetPromptSeconds = -1;

      Serial.println("RESET hold started");
      drawResetHoldPrompt(0);
    }

    unsigned long heldMs = now - resetHoldStartMs;

    if (now - lastResetPromptUpdateMs >= 200) {
      lastResetPromptUpdateMs = now;
      drawResetHoldPrompt(heldMs);
    }

    if (!resetHoldCompleted && heldMs >= resetHoldRequiredMs) {
      resetHoldCompleted = true;
      resetHoldActive = false;

      Serial.println("RESET hold confirmed");

      clearResetHoldPrompt();

      roundNumber = 1;
      resetRoundAndPoll();

      mqtt.publish("hub/round", "0");
      mqtt.publish("hub/ack", "physical_reset_hold_confirmed");

      return;
    }

    return;
  }

  if (resetHoldActive && !resetPressed) {
    Serial.println("RESET hold cancelled");

    resetHoldActive = false;
    resetHoldCompleted = false;
    resetHoldStartMs = 0;
    lastResetPromptUpdateMs = 0;
    lastResetPromptSeconds = -1;

    clearResetHoldPrompt();
  }

  if (nextPressed && (now - lastNextButtonTime >= nextDebounceDelay)) {
    Serial.println("NEXT button pressed");

    roundNumber++;
    resetRoundAndPoll();

    String msg = String("{\"round\":") + roundNumber + "}";
    mqtt.publish("hub/round", msg.c_str());
    mqtt.publish("hub/ack", "next_round");

    lastNextButtonTime = now;
  }
}

void drawResetHoldPrompt(unsigned long heldMs) {
  int secondsRemaining = 5 - (heldMs / 1000);
  if (secondsRemaining < 0) secondsRemaining = 0;

  if (secondsRemaining == lastResetPromptSeconds) return;
  lastResetPromptSeconds = secondsRemaining;

  tft.fillRoundRect(
    RESET_PROMPT_X,
    RESET_PROMPT_Y,
    RESET_PROMPT_W,
    RESET_PROMPT_H,
    RESET_PROMPT_R,
    HX8357_BLACK
  );

  tft.drawRoundRect(
    RESET_PROMPT_X,
    RESET_PROMPT_Y,
    RESET_PROMPT_W,
    RESET_PROMPT_H,
    RESET_PROMPT_R,
    STATUS_LOWBAT
  );

  tft.drawRoundRect(
    RESET_PROMPT_X + 2,
    RESET_PROMPT_Y + 2,
    RESET_PROMPT_W - 4,
    RESET_PROMPT_H - 4,
    10,
    HX8357_WHITE
  );

  tft.setTextSize(2);
  tft.setTextColor(STATUS_LOWBAT);
  tft.setCursor(RESET_PROMPT_X + 36, RESET_PROMPT_Y + 18);
  tft.print("HOLD RESET TO CONFIRM");

  tft.setTextSize(2);
  tft.setTextColor(HX8357_WHITE);
  tft.setCursor(RESET_PROMPT_X + 54, RESET_PROMPT_Y + 52);
  tft.print("Release cancels reset");

  tft.setTextSize(3);
  tft.setTextColor(HX8357_YELLOW);
  tft.setCursor(RESET_PROMPT_X + 142, RESET_PROMPT_Y + 82);
  tft.print(secondsRemaining);
  tft.print(" sec");
}

void clearResetHoldPrompt() {
  tft.fillRoundRect(
    RESET_PROMPT_X,
    RESET_PROMPT_Y,
    RESET_PROMPT_W,
    RESET_PROMPT_H,
    RESET_PROMPT_R,
    HX8357_BLACK
  );

  drawBuzzHeader();

  const int margin = 8;
  const int headerH = 26;
  const int gap = 5;
  const int slotW = (SCREEN_W - (margin * 3)) / 2;
  const int slotH = (TOP_H - headerH - (gap * 3) - 2) / 4;

  const int leftX = margin;
  const int rightX = margin * 2 + slotW;

  for (int i = 0; i < 8; i++) {
    int col = (i < 4) ? 0 : 1;
    int row = (i < 4) ? i : i - 4;

    int slotX = (col == 0) ? leftX : rightX;
    int slotY = headerH + (row * (slotH + gap));

    if (rectsOverlap(
          slotX, slotY, slotW, slotH,
          RESET_PROMPT_X, RESET_PROMPT_Y, RESET_PROMPT_W, RESET_PROMPT_H
        )) {
      drawBuzzSlot(i);
    }
  }

  tft.drawFastHLine(0, TOP_H, SCREEN_W, GRAY_MED);
}

bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw &&
         ax + aw > bx &&
         ay < by + bh &&
         ay + ah > by;
}

// ============================================================
// Lightweight JSON helpers
// ============================================================
String jsonGetString(const String& json, const String& key) {
  String needle = "\"" + key + "\":\"";
  int start = json.indexOf(needle);
  if (start < 0) return "";

  start += needle.length();

  int end = json.indexOf("\"", start);
  if (end < 0) return "";

  return json.substring(start, end);
}

int jsonGetInt(const String& json, const String& key) {
  String needle = "\"" + key + "\":";
  int start = json.indexOf(needle);
  if (start < 0) return -1;

  start += needle.length();

  while (start < (int)json.length() && json[start] == ' ') {
    start++;
  }

  int end = start;

  if (end < (int)json.length() && json[end] == '-') {
    end++;
  }

  while (end < (int)json.length() && isDigit(json[end])) {
    end++;
  }

  if (end == start) return -1;

  return json.substring(start, end).toInt();
}

bool jsonGetBool(const String& json, const String& key) {
  return jsonGetBoolDefault(json, key, false);
}

bool jsonGetBoolDefault(const String& json, const String& key, bool defaultValue) {
  String needle = "\"" + key + "\":";
  int start = json.indexOf(needle);
  if (start < 0) return defaultValue;

  start += needle.length();

  while (start < (int)json.length() && json[start] == ' ') {
    start++;
  }

  if (json.startsWith("true", start)) return true;
  if (json.startsWith("false", start)) return false;

  return defaultValue;
}
