# 📡 Buzzer System (MQTT-Based Quiz/Game System)

A lightweight, wireless buzzer system built with ESP8266 / ESP32 devices and an MQTT-based hub, designed for quiz games, classrooms, or competitive “first-to-buzz” scenarios.

- ⚡ Real-time buzz detection  
- 🔋 Battery-powered wireless buttons  
- 💡 LED + display feedback  
- 🔧 Easily expandable  

---

## 🧠 Overview

The system consists of three main components:

### 🎛️ Buzzer Buttons (ESP-12F / ESP8266)
Each player has a wireless buzzer device that:
- Connects to WiFi
- Sends a message when pressed
- Lights an LED when activated
- Reports battery level when requested
- Locks until reset

---

### 🧭 Buzzer Hub (ESP32 or Raspberry Pi)
The hub controls the system:
- Hosts/connects to MQTT broker
- Listens for button presses
- Determines buzz order
- Sends reset / next-round commands
- Polls devices for battery/status

---

### 🖥️ Display / UI
Provides:
- Buzz order (who pressed first)
- Player status (READY / BUZZED / LOWBAT)
- Round counter
- Reset + next round controls

---

🚀 Features
✅ Fully local (no internet required)
✅ Fast MQTT-based response
✅ Battery efficient
✅ Scalable player count
✅ Works with ESP8266 / ESP32

Mostly vibe coded using ChatGPT
