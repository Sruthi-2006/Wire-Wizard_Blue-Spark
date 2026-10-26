#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// Hardware: DHT11 DATA -> GPIO4; LDR AO -> GPIO34; active buzzer + -> GPIO25.
// Sensor VCC -> 3V3, GND -> GND. Use a common ground.
#define DHTPIN 4
#define DHTTYPE DHT11
#define LDR_PIN 34
#define BUZZER_PIN 25

DHT dht(DHTPIN, DHTTYPE);

// Replace with your Wi-Fi credentials.
const char* WIFI_SSID = "wifi name";
const char* WIFI_PASSWORD = "wifi password";

// IMPORTANT: use your laptop IPv4 address AND port 3001 in both URLs.
// Example only: 10.234.198.216. Check your own address with ipconfig.
const char* SERVER_URL = "http://xxxxxxxxxx/api/esp32/data";
const char* CONFIG_URL = "http://xxxxxxxxxx/api/config";

// Keep these defaults in sync with backend/server.js and the dashboard.
float TEMP_LIMIT = 35.0;
float HUMIDITY_LIMIT = 80.0;
int LIGHT_LIMIT = 2500;

// Current setup assumes lower ADC means darker. Confirm by observing ADC in light/dark.
const bool LOW_LIGHT_IS_LOW_ADC = true;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long CONFIG_INTERVAL = 10000;
const unsigned long BEEP_ON_TIME = 500;
const unsigned long BEEP_OFF_TIME = 1500;
const unsigned long WIFI_RETRY_INTERVAL = 10000;

unsigned long lastSensorRead = 0;
unsigned long lastConfigRead = 0;
unsigned long lastBeepChange = 0;
unsigned long lastWiFiAttempt = 0;

bool alertActive = false;
bool buzzerState = false;
bool haveValidDhtReading = false;

float temperature = NAN;
float humidity = NAN;
int lightValue = 0;

void setBuzzer(bool on) {
  buzzerState = on;
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW); // active-high buzzer
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not connected; will retry.");
  }
}

void fetchThresholds() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(CONFIG_URL)) {
    Serial.println("Could not start threshold HTTP request.");
    return;
  }
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err) {
      TEMP_LIMIT = doc["temperature"] | TEMP_LIMIT;
      HUMIDITY_LIMIT = doc["humidity"] | HUMIDITY_LIMIT;
      LIGHT_LIMIT = doc["lightAdc"] | LIGHT_LIMIT;
      Serial.printf("Thresholds: temp %.1f C, humidity %.1f %%, light ADC %d\n",
                    TEMP_LIMIT, HUMIDITY_LIMIT, LIGHT_LIMIT);
    } else {
      Serial.println("Threshold JSON parse failed.");
    }
  } else {
    Serial.printf("Threshold GET failed, HTTP %d\n", code);
  }
  http.end();
}

void sendReadings() {
  if (WiFi.status() != WL_CONNECTED || !haveValidDhtReading) return;

  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(SERVER_URL)) {
    Serial.println("Could not start sensor HTTP request.");
    return;
  }
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["lightAdc"] = lightValue;
  doc["alert"] = alertActive;
  doc["buzzerOn"] = buzzerState;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  Serial.printf("POST sensor data -> HTTP %d\n", code);
  if (code > 0) {
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.println("Backend not reachable. Check laptop IP, port 3001, Wi-Fi and firewall.");
  }
  http.end();
}

void evaluateAlert() {
  bool highTemp = temperature > TEMP_LIMIT;
  bool highHumidity = humidity > HUMIDITY_LIMIT;
  bool lowLight = LOW_LIGHT_IS_LOW_ADC
                    ? (lightValue < LIGHT_LIMIT)
                    : (lightValue > LIGHT_LIMIT);

  bool wasAlert = alertActive;
  alertActive = highTemp || highHumidity || lowLight;

  Serial.println("--------------------------------");
  Serial.printf("Temperature: %.1f C (limit %.1f) -> %s\n",
                temperature, TEMP_LIMIT, highTemp ? "ALERT" : "OK");
  Serial.printf("Humidity: %.1f %% (limit %.1f) -> %s\n",
                humidity, HUMIDITY_LIMIT, highHumidity ? "ALERT" : "OK");
  Serial.printf("Light ADC: %d (limit %d) -> %s\n",
                lightValue, LIGHT_LIMIT, lowLight ? "LOW LIGHT ALERT" : "OK");
  Serial.printf("OVERALL: %s\n", alertActive ? "ALERT" : "NORMAL");

  if (alertActive && !wasAlert) {
    lastBeepChange = millis();
    setBuzzer(true); // sound immediately when any condition becomes abnormal
  } else if (!alertActive) {
    setBuzzer(false);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);
  pinMode(LDR_PIN, INPUT);
  analogReadResolution(12);
  dht.begin();

  Serial.println("\nEnviroGuard starting...");
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) fetchThresholds();
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = now;
    Serial.println("Reconnecting Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (now - lastConfigRead >= CONFIG_INTERVAL) {
    lastConfigRead = now;
    fetchThresholds();
  }

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int l = analogRead(LDR_PIN);

    if (isnan(t) || isnan(h)) {
      haveValidDhtReading = false;
      alertActive = false;
      setBuzzer(false);
      Serial.println("DHT11 read failed; check VCC, GND and DATA on GPIO4.");
    } else {
      temperature = t;
      humidity = h;
      lightValue = l;
      haveValidDhtReading = true;
      evaluateAlert();
      sendReadings();
    }
  }

  // Non-blocking intermittent buzzer: 500 ms ON, 1500 ms OFF.
  if (alertActive) {
    unsigned long duration = buzzerState ? BEEP_ON_TIME : BEEP_OFF_TIME;
    if (millis() - lastBeepChange >= duration) {
      lastBeepChange = millis();
      setBuzzer(!buzzerState);
    }
  } else {
    setBuzzer(false);
  }
  delay(5);
}
