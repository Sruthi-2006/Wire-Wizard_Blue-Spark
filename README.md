# EnviroGuard — ESP32 Environmental Monitoring Dashboard

A full-stack starter project for ESP32 + DHT11 + LDR + active buzzer.

## Features
- React + Vite dashboard with responsive layout and sensor cards.
- Node.js + Express API to receive readings from the ESP32.
- Dashboard refreshes from the backend every 2 seconds.
- Configurable thresholds in the dashboard (stored in backend memory).
- Alert event history and simulated demo mode.
- ESP32 firmware reads DHT11 and LDR, drives an active-high buzzer intermittently, and posts readings over Wi-Fi.

## Project layout
- `frontend/` React dashboard
- `backend/` Express API
- `esp32/EnviroGuard.ino` ESP32 firmware

## Requirements
- Node.js 18+ (20+ recommended)
- VS Code
- Arduino IDE with ESP32 board support
- Arduino library: **DHT sensor library by Adafruit**
- Arduino library dependency: **Adafruit Unified Sensor**
- Arduino library: **ArduinoJson** (install through Arduino IDE Library Manager)

## 1. Run the backend
Open a terminal in the `backend` folder:

```bash
npm install
npm run dev
```

The API listens on port `3001`. Check `http://localhost:3001/api/health`.

## 2. Run the React frontend
Open a second terminal in `frontend`:

```bash
npm install
npm run dev -- --host 0.0.0.0
```

Open the URL Vite prints (usually `http://localhost:5173`).

## 3. Connect ESP32 over Wi-Fi
1. Connect the ESP32 and laptop to the same Wi-Fi network.
2. Find your laptop's IPv4 address using Windows Command Prompt: `ipconfig`.
3. In `esp32/EnviroGuard.ino`, set:
   - `WIFI_SSID` to your Wi-Fi name
   - `WIFI_PASSWORD` to your Wi-Fi password
   - `SERVER_URL` to `http://YOUR_LAPTOP_IPV4:3001/api/esp32/data`
4. Upload the sketch to ESP32.
5. Ensure Windows Firewall allows Node.js on private networks.
6. The backend should show incoming readings; the dashboard connection indicator changes to **ESP32 connected**.

Do not use `localhost` in `SERVER_URL`: on the ESP32, localhost means the ESP32 itself, not your laptop.
Your laptop IP can change after reconnecting to Wi-Fi; update `SERVER_URL` if needed.

## Hardware wiring
- DHT11 VCC -> ESP32 3V3; DATA -> GPIO 4; GND -> GND.
- LDR module VCC -> 3V3; GND -> GND; AO -> GPIO 34.
- Active buzzer signal/+ -> GPIO 25; GND/- -> GND.
- If using a bare DHT11, use a 4.7k–10k pull-up resistor between DATA and 3V3.
- Keep all grounds common. Ensure the LDR AO output never exceeds 3.3 V.
- Do not drive a buzzer that exceeds ESP32 GPIO current limits directly; use a transistor driver if needed.

## Threshold defaults
- Temperature alert when `temperature > 35 °C`
- Humidity alert when `humidity > 80 % RH`
- Light alert when `lightAdc < 300` (assumes lower ADC means darker; calibrate to your LDR)
- Buzzer pulses 500 ms ON / 1500 ms OFF while any alert is active.

The dashboard's threshold controls are sent to the backend. To make them control the physical ESP32 too, the firmware polls `/api/config` and applies the returned thresholds. This is implemented in the included firmware.

## API endpoints
- `GET /api/health` — backend health
- `GET /api/readings` — latest reading and connection freshness
- `POST /api/esp32/data` — ESP32 posts sensor readings and alert state
- `GET /api/config` — thresholds for ESP32
- `PUT /api/config` — dashboard updates thresholds
- `GET /api/events` — recent threshold state changes
- `POST /api/demo` — turn demo mode on/off

## Troubleshooting
- If dashboard says demo mode, check the ESP32 and backend share the same Wi-Fi network and `SERVER_URL` uses the laptop's IPv4 address.
- If browser loads but no data arrives, allow port 3001 through Windows Firewall on Private networks.
- Check Arduino Serial Monitor at 115200 baud.
- DHT11 normally needs around 2 seconds between reads.
- LDR threshold direction depends on the specific LDR module wiring; compare ADC values in bright and dark conditions.
