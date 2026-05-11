#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

// ============================================================
// USER SETTINGS
// ============================================================
const char* WIFI_SSID = "BuzzerHub";
const char* WIFI_PASS = "buzzertest";

const char* MQTT_HOST = "192.168.4.1";
const uint16_t MQTT_PORT = 1883;
const char* OTA_PASSWORD = "buzzertest";


const int BUZZER_ID = 1;
  //RED, RED, 1
  //ORANGE, ORG, 2
  //YELLOW, YLW, 3
  //GREEN, GRN, 4
  //BLUE, BLU, 5
  //PURPLE", PUR, 6
  //PINK, PNK, 7
  //BROWN,  BRN,  8
const char* OTA_HOSTNAME;
const char* BUZZER_NAME;

// ============================================================
// PIN DEFINITIONS - ESP8266-12F / Wemos D1 Mini
// ============================================================
#define BUTTON_PIN 4     // GPIO4 / D2
#define LED_PIN    12    // GPIO12 / D6 - External LED
#define BATTERY_PIN A0

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// Most ESP8266 onboard LEDs are active LOW
#define INTERNAL_LED_ON  LOW
#define INTERNAL_LED_OFF HIGH

// ============================================================
// BATTERY MONITORING
// ============================================================
const float ADC_REF_VOLTAGE = 1.0f;
const float DIVIDER_TOP_K = 2200.0f;   // 2.2MΩ
const float DIVIDER_BOTTOM_K = 470.0f; // 470kΩ
const float DIVIDER_RATIO = DIVIDER_BOTTOM_K / (DIVIDER_TOP_K + DIVIDER_BOTTOM_K);

const float BATTERY_VOLTAGE_FULL = 4.5f;
const float BATTERY_VOLTAGE_EMPTY = 1.8f;
const int LOW_BATTERY_PERCENT = 25;

// ============================================================
// TIMING
// ============================================================
const unsigned long MQTT_RETRY_MS = 1500;
const unsigned long HEARTBEAT_MS = 30000;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long PRESS_LOCKOUT_MS = 750;

// ============================================================
// GLOBALS
// ============================================================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool buzzLatched = false;
bool buttonLastReading = HIGH;
bool buttonStableState = HIGH;

unsigned long lastDebounceTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastPressTime = 0;

unsigned long currentRound = 1;

int cachedBatteryPercent = 100;
bool cachedBatteryLow = false;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void connectWiFi();
void setupOTA();
bool connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);

void handleMQTT();
void handleButton();
void handleHeartbeat();

void clearForNextRound();
void handlePollRequest();

void publishPress();
void publishStatus(const char* eventName);
void publishReadyOrLowbat();
String buildPayload(const char* eventName, int batteryPercent, bool armed);

void updateBatteryCache();
float readBatteryVoltage();
int readBatteryPercent();

bool mqttReady();
void flashInternalLedTwice();

// ============================================================
// SETUP
// ============================================================
void setup() {
  switch (BUZZER_ID) {
    case 1:
      OTA_HOSTNAME = "buzzer-RED";
      BUZZER_NAME = "RED";
      break;

    case 2:
      OTA_HOSTNAME = "buzzer-ORANGE";
      BUZZER_NAME = "ORANGE";
      break;

    case 3:
      OTA_HOSTNAME = "buzzer-YELLOW";
      BUZZER_NAME = "YELLOW";
      break;

    case 4:
      OTA_HOSTNAME = "buzzer-GREEN";
      BUZZER_NAME = "GREEN";
      break;

    case 5:
      OTA_HOSTNAME = "buzzer-BLUE";
      BUZZER_NAME = "BLUE";
      break;

    case 6:
      OTA_HOSTNAME = "buzzer-PURPLE";
      BUZZER_NAME = "PURPLE";
      break;
      
    case 7:
      OTA_HOSTNAME = "buzzer-PINK";
      BUZZER_NAME = "PINK";
      break;

    case 8:
      OTA_HOSTNAME = "buzzer-BROWN";
      BUZZER_NAME = "BROWN";
      break;

    default:
      OTA_HOSTNAME = "buzzer-GREY";
      BUZZER_NAME = "GREY";
      break;
  }

  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("================================");
  Serial.println("        BUZZER NODE BOOT        ");
  Serial.println("================================");
  Serial.print("Buzzer ID: ");
  Serial.println(BUZZER_ID);
  Serial.print("Buzzer Name: ");
  Serial.println(BUZZER_NAME);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_BUILTIN, INTERNAL_LED_OFF);

  Serial.println("Pins initialized");
  Serial.print("Button pin: GPIO");
  Serial.println(BUTTON_PIN);
  Serial.print("External LED pin: GPIO");
  Serial.println(LED_PIN);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  Serial.println("WiFi configured as station");

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(256);

  Serial.print("MQTT host: ");
  Serial.println(MQTT_HOST);
  Serial.print("MQTT port: ");
  Serial.println(MQTT_PORT);

  updateBatteryCache();

  connectWiFi();
  setupOTA();
  connectMQTT();

  Serial.println("Setup complete");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  handleMQTT();
  handleButton();
  handleHeartbeat();

  yield();
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println();
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED) {
    ArduinoOTA.handle();
    delay(250);
    yield();

    Serial.print(".");
    attempts++;

    if (attempts % 20 == 0) {
      Serial.println();
      Serial.println("Still trying to connect to WiFi...");
    }
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  flashInternalLedTwice();
}

void flashInternalLedTwice() {
  Serial.println("Flashing internal LED twice");

  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_BUILTIN, INTERNAL_LED_ON);
    delay(150);
    digitalWrite(LED_BUILTIN, INTERNAL_LED_OFF);
    delay(150);
  }
}

// ============================================================
// OTA
// ============================================================
void setupOTA() {
  Serial.println("Setting up OTA");

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting");
    digitalWrite(LED_PIN, LOW);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println();
    Serial.println("OTA update complete");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    yield();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error [%u]: ", error);

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
  });

  ArduinoOTA.begin();

  Serial.print("OTA ready. Hostname: ");
  Serial.println(OTA_HOSTNAME);
}

// ============================================================
// MQTT
// ============================================================
void handleMQTT() {
  if (!mqtt.connected()) {
    unsigned long now = millis();

    if (now - lastMqttReconnectAttempt >= MQTT_RETRY_MS) {
      lastMqttReconnectAttempt = now;

      Serial.println("MQTT disconnected. Attempting reconnect...");
      connectMQTT();
    }

    return;
  }

  mqtt.loop();
}

bool connectMQTT() {
  if (mqtt.connected()) {
    return true;
  }

  String clientId = String("buzzer-") + BUZZER_ID + "-" + ESP.getChipId();

  Serial.println();
  Serial.println("Connecting to MQTT...");
  Serial.print("Client ID: ");
  Serial.println(clientId);

  if (!mqtt.connect(clientId.c_str())) {
    Serial.print("MQTT connection failed. State: ");
    Serial.println(mqtt.state());
    return false;
  }

  Serial.println("MQTT connected");

  mqtt.subscribe("hub/poll");
  mqtt.subscribe("hub/round");
  mqtt.subscribe("buzzer/all/reset");
  mqtt.subscribe("buzzer/all/next_round");

  String directResetTopic = String("buzzer/") + BUZZER_ID + "/reset";
  mqtt.subscribe(directResetTopic.c_str());

  Serial.println("Subscribed to:");
  Serial.println("  hub/poll");
  Serial.println("  hub/round");
  Serial.println("  buzzer/all/reset");
  Serial.println("  buzzer/all/next_round");
  Serial.print("  ");
  Serial.println(directResetTopic);

  publishStatus("online");
  publishReadyOrLowbat();

  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = topic;

  Serial.println();
  Serial.println("========== MQTT MESSAGE ==========");
  Serial.print("Topic: ");
  Serial.println(topicStr);

  Serial.print("Payload: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("==================================");

  if (topicStr == "hub/poll") {
    Serial.println("Hub poll received");
    handlePollRequest();
    return;
  }

  if (
    topicStr == "hub/round" ||
    topicStr == "buzzer/all/reset" ||
    topicStr == "buzzer/all/next_round"
  ) {
    Serial.println("Round reset / next round received");
    currentRound++;
    clearForNextRound();
    return;
  }

  String directResetTopic = String("buzzer/") + BUZZER_ID + "/reset";

  if (topicStr == directResetTopic) {
    Serial.println("Direct reset received");
    currentRound++;
    clearForNextRound();
    return;
  }

  Serial.println("MQTT topic ignored");
}

// ============================================================
// BUTTON HANDLING
// ============================================================
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (reading != buttonLastReading) {
    lastDebounceTime = now;

    Serial.print("Button reading changed to: ");
    Serial.println(reading == LOW ? "LOW / PRESSED" : "HIGH / RELEASED");
  }

  if ((now - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonStableState) {
      buttonStableState = reading;

      Serial.print("Button stable state changed to: ");
      Serial.println(buttonStableState == LOW ? "PRESSED" : "RELEASED");

      if (buttonStableState == LOW) {
        if (!buzzLatched && mqttReady() && (now - lastPressTime >= PRESS_LOCKOUT_MS)) {
          lastPressTime = now;

          Serial.println("Valid button press detected");
          Serial.println("Latching buzzer LED ON");

          buzzLatched = true;
          digitalWrite(LED_PIN, HIGH);

          publishPress();
          publishStatus("buzzed");
        } else {
          Serial.println("Button press ignored");

          if (buzzLatched) {
            Serial.println("Reason: already latched");
          }

          if (!mqttReady()) {
            Serial.println("Reason: MQTT/WiFi not ready");
          }

          if (now - lastPressTime < PRESS_LOCKOUT_MS) {
            Serial.println("Reason: press lockout active");
          }
        }
      }
    }
  }

  buttonLastReading = reading;
}

// ============================================================
// HUB EVENT HANDLERS
// ============================================================
void clearForNextRound() {
  Serial.println();
  Serial.println("Clearing for next round");

  buzzLatched = false;
  digitalWrite(LED_PIN, LOW);

  delay(25);

  updateBatteryCache();
  publishReadyOrLowbat();

  Serial.print("Current round: ");
  Serial.println(currentRound);
}

void handlePollRequest() {
  Serial.println("Handling hub poll request");

  if (buzzLatched) {
    Serial.println("Currently buzzed/latched");
    publishStatus("buzzed");
  } else {
    Serial.println("Currently ready or low battery");
    publishReadyOrLowbat();
  }
}

// ============================================================
// HEARTBEAT
// ============================================================
void handleHeartbeat() {
  if (!mqtt.connected()) return;

  unsigned long now = millis();

  if (now - lastHeartbeatTime >= HEARTBEAT_MS) {
    lastHeartbeatTime = now;

    Serial.println();
    Serial.println("Heartbeat");

    updateBatteryCache();

    if (buzzLatched) {
      publishStatus("buzzed");
    } else {
      publishReadyOrLowbat();
    }
  }
}

// ============================================================
// PUBLISH HELPERS
// ============================================================
void publishPress() {
  String topic = String("buzzer/") + BUZZER_ID + "/press";
  String payload = buildPayload("press", cachedBatteryPercent, false);

  Serial.println();
  Serial.println("Publishing PRESS");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(payload);

  mqtt.publish(topic.c_str(), payload.c_str());
}

void publishReadyOrLowbat() {
  if (cachedBatteryLow) {
    Serial.println("Battery low. Publishing LOWBAT");
    publishStatus("lowbat");
  } else {
    Serial.println("Battery okay. Publishing READY");
    publishStatus("ready");
  }
}

void publishStatus(const char* eventName) {
  String topic = String("buzzer/") + BUZZER_ID + "/status";

  bool armed = !buzzLatched && cachedBatteryPercent >= LOW_BATTERY_PERCENT;
  String payload = buildPayload(eventName, cachedBatteryPercent, armed);

  Serial.println();
  Serial.print("Publishing status: ");
  Serial.println(eventName);
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(payload);

  bool ok = mqtt.publish(topic.c_str(), payload.c_str());

  Serial.print("Publish result: ");
  Serial.println(ok ? "OK" : "FAILED");
}

String buildPayload(const char* eventName, int batteryPercent, bool armed) {
  String payload = "{";

  payload += "\"battery\":";
  payload += batteryPercent;

  payload += ",\"event\":\"";
  payload += eventName;
  payload += "\"";

  payload += "}";

  return payload;
}

// ============================================================
// BATTERY
// ============================================================
void updateBatteryCache() {
  Serial.println();
  Serial.println("Updating battery cache");

  float voltage = readBatteryVoltage();

  cachedBatteryPercent = readBatteryPercent();
  cachedBatteryLow = cachedBatteryPercent < LOW_BATTERY_PERCENT;

  Serial.print("Battery voltage: ");
  Serial.print(voltage, 3);
  Serial.println(" V");

  Serial.print("Battery percent: ");
  Serial.print(cachedBatteryPercent);
  Serial.println("%");

  Serial.print("Low battery: ");
  Serial.println(cachedBatteryLow ? "YES" : "NO");
}

float readBatteryVoltage() {
  const int samples = 6;
  long total = 0;

  for (int i = 0; i < samples; i++) {
    total += analogRead(BATTERY_PIN);
    delay(2);
    yield();
  }

  float raw = total / (float)samples;
  float adcVoltage = (raw / 1023.0f) * ADC_REF_VOLTAGE;
  float batteryVoltage = adcVoltage / DIVIDER_RATIO;

  Serial.print("ADC raw average: ");
  Serial.println(raw);

  Serial.print("ADC voltage: ");
  Serial.print(adcVoltage, 3);
  Serial.println(" V");

  return batteryVoltage;
}

int readBatteryPercent() {
  float v = readBatteryVoltage();

  float pct = ((v - BATTERY_VOLTAGE_EMPTY) /
              (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY)) * 100.0f;

  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;

  return (int)(pct + 0.5f);
}

// ============================================================
// STATE
// ============================================================
bool mqttReady() {
  bool ready = WiFi.status() == WL_CONNECTED && mqtt.connected();

  return ready;
}