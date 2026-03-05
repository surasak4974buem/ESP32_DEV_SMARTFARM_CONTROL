#include <Arduino.h>
#include "DevIsoInput.h"
#include "DevRelay.h"
#include "DevSwitch.h"
#include "DevTempHumidity.h"

// OLED display library
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi Manager
#include <WiFi.h>
#include <WiFiManager.h>

// HTTP Client for API requests
#include <HTTPClient.h>

// JSON parsing for API responses
#include <ArduinoJson.h>

// SPIFFS for file system
#include <SPIFFS.h>

// Async Web Server
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// DS18B20 Temperature Sensor
#include <OneWire.h>
#include <DallasTemperature.h>

// MQTT Client
#include <PubSubClient.h>

// Telegram Bot
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// OLED display configuration
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif
#ifndef OLED_RESET
#define OLED_RESET -1
#endif
#ifndef SCREEN_ADDRESS
#define SCREEN_ADDRESS 0x3C
#endif

// Instantiate OLED display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// DS18B20 Temperature Sensor Configuration
const uint8_t PIN_TEMP_SENSOR = 14; // GPIO14 for DS18B20 Data Pin
OneWire oneWire(PIN_TEMP_SENSOR);
DallasTemperature tempSensor(&oneWire);

// Temperature variables
float currentTemperature = 0.0;
bool sensorConnected = false;
unsigned long lastTempUpdate = 0;
const unsigned long TEMP_UPDATE_INTERVAL = 2000; // Update every 2 seconds
float simulatedTemp = 25.0; // Starting simulated temperature
float tempDelta = 0.5; // Simulated temperature change rate

// XY-MD03 Temperature & Humidity Sensor (Modbus RTU via Serial0/RS485)
DevTempHumidity xymd03(&Serial, 1); // Serial0 (GPIO1=TX, GPIO3=RX), Slave ID=1
float xymd03_temperature = 0.0;
float xymd03_humidity = 0.0;
bool xymd03_connected = false;
unsigned long lastXYMD03Update = 0;
const unsigned long XYMD03_UPDATE_INTERVAL = 3000; // Update every 3 seconds
// Simulation variables for XY-MD03
float simTempXY = 26.5;
float simHumXY = 65.0;
float simTempDeltaXY = 0.3;
float simHumDeltaXY = 1.0;

// Display page management
uint8_t currentPage = 0; // 0=Main page, 1=XY-MD03 page, 2=Weather page
const uint8_t MAX_PAGES = 3;

// OpenWeather API Configuration
const String OPENWEATHER_API_KEY = "5285b3436c86bdab46069027fd961d09";
String cityName = "Nakhon Si Thammarat"; // Default city (user can change)

// Weather data variables
float weather_temp = 0.0;
float weather_feels_like = 0.0;
float weather_humidity = 0.0;
float weather_pressure = 0.0;
float weather_wind_speed = 0.0;
String weather_description = "";
String weather_main = "";

// Air quality data variables
int aqi = 0; // Air Quality Index (1=Good, 2=Fair, 3=Moderate, 4=Poor, 5=Very Poor)
float pm2_5 = 0.0; // PM2.5 concentration
float pm10 = 0.0;  // PM10 concentration
float co = 0.0;    // Carbon Monoxide
float no2 = 0.0;   // Nitrogen Dioxide
float o3 = 0.0;    // Ozone

// Weather update timing
bool weatherDataAvailable = false;
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 600000; // Update every 10 minutes (600000 ms)

// Coordinates for air quality (from current weather API)
float weather_lat = 0.0;
float weather_lon = 0.0;

// ======================================
// AUTOMATION SETTINGS
// ======================================

// Temperature Control Settings
float TEMP_FAN_ON = 30.0;      // เปิดพัดลมเมื่ออุณหภูมิเกิน 30°C
float TEMP_FAN_OFF = 28.0;     // ปิดพัดลมเมื่ออุณหภูมิต่ำกว่า 28°C
float TEMP_HEATER_ON = 20.0;   // เปิดฮีตเตอร์เมื่ออุณหภูมิต่ำกว่า 20°C
float TEMP_HEATER_OFF = 22.0;  // ปิดฮีตเตอร์เมื่ออุณหภูมิสูงกว่า 22°C

// Humidity Control Settings
float HUM_MIN = 60.0;  // ความชื้นต่ำสุด (เปิดปั๊มน้ำ)
float HUM_MAX = 80.0;  // ความชื้นสูงสุด (เปิดพัดลม)

// Automation Enable/Disable
bool autoTempEnabled = false;  // เปิด/ปิด ควบคุมอุณหภูมิอัตโนมัติ
bool autoHumEnabled = false;   // เปิด/ปิด ควบคุมความชื้นอัตโนมัติ
bool scheduleEnabled = false;  // เปิด/ปิด ระบบตั้งเวลา

// Automation Timing
unsigned long lastAutoCheck = 0;
const unsigned long AUTO_CHECK_INTERVAL = 5000; // Check every 5 seconds

// Schedule System
// days bitmask: bit0=อา(Sun), bit1=จ(Mon), bit2=อ(Tue), bit3=พ(Wed), bit4=พฤ(Thu), bit5=ศ(Fri), bit6=ส(Sat)
// 0x7F = ทุกวัน (every day)
struct Schedule {
  uint8_t hour;
  uint8_t minute;
  uint8_t relayNum;  // 1=Fan, 2=Pump, 3=Heater
  bool turnOn;       // true=เปิด, false=ปิด
  bool enabled;
  uint8_t days;      // Bitmask วันทำงาน (0x7F=ทุกวัน)
};

#define MAX_SCHEDULES 20

// Default schedules (can be modified via Web)
Schedule schedules[MAX_SCHEDULES] = {
  {6, 0, 2, true, false, 0x7F},   // 06:00 เปิดปั๊มน้ำ ทุกวัน
  {6, 30, 2, false, false, 0x7F}, // 06:30 ปิดปั๊มน้ำ ทุกวัน
  {12, 0, 1, true, false, 0x7F},  // 12:00 เปิดพัดลม ทุกวัน
  {18, 0, 1, false, false, 0x7F}, // 18:00 ปิดพัดลม ทุกวัน
  {20, 0, 3, true, false, 0x7F},  // 20:00 เปิดฮีตเตอร์ ทุกวัน
  {7, 0, 3, false, false, 0x7F},  // 07:00 ปิดฮีตเตอร์ ทุกวัน
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F},
  {0, 0, 0, false, false, 0x7F}
};

int lastScheduleMinute = -1; // Track last executed minute to prevent double execution

// Pin definitions (from HardwareESP32Config.md)
const uint8_t PIN_SW1 = 34; // SW1 = Enter/Select (Active Low)
const uint8_t PIN_SW2 = 35; // SW2 = Down (Active Low)
const uint8_t PIN_SW3 = 32; // SW3 = Up (Active Low)

// Instantiate switches (Active Low)
DevSwitch sw1(PIN_SW1, false);
DevSwitch sw2(PIN_SW2, false);
DevSwitch sw3(PIN_SW3, false);

// Relay pin definitions (from HardwareESP32Config.md)
const uint8_t PIN_RELAY1 = 4;  // Relay1 = Fan (Active Low)
const uint8_t PIN_RELAY2 = 16; // Relay2 = Pump (Active Low)
const uint8_t PIN_RELAY3 = 17; // Relay3 = Heater (Active Low)

// Instantiate relays (Active Low)
DevRelayWithTimer relayFan(PIN_RELAY1, true);
DevRelayWithTimer relayPump(PIN_RELAY2, true);
DevRelayWithTimer relayHeater(PIN_RELAY3, true);

// Isolated inputs (from HardwareESP32Config.md)
const uint8_t PIN_ISO1 = 33; // ISO1 = TankLevelSensor1 (water dry) Active Low
const uint8_t PIN_ISO2 = 27; // ISO2 = TankLevelSensor2 (water overflow) Active Low

// Instantiate isolated inputs (Active Low)
DevIsoInput iso1(PIN_ISO1, false);
DevIsoInput iso2(PIN_ISO2, false);

// Display update timing
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 250; // ms

// WiFi variables
String ipAddress = "Not Connected";
bool wifiConnected = false;

// WiFi Reset detection
const unsigned long WIFI_RESET_HOLD_TIME = 5000; // 5 seconds
unsigned long sw1PressStart = 0;
bool sw1LongPressHandled = false;

// Web Server
AsyncWebServer server(80);

// ======================================
// MQTT CONFIGURATION
// ======================================

// MQTT Settings (HiveMQ Public Broker)
String mqtt_server = "broker.hivemq.com";
int mqtt_port = 1883;
String mqtt_username = "";  // Optional: for secured HiveMQ Cloud
String mqtt_password = "";  // Optional: for secured HiveMQ Cloud
String mqtt_topic_prefix = "smartfarm/device01"; // Unique prefix for each device
bool mqtt_enabled = false;

// MQTT Client
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// MQTT Connection Status
bool mqttConnected = false;
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000; // Try reconnect every 5 seconds
String mqtt_client_id = ""; // Unique Client ID based on MAC Address

// MQTT Publish Intervals
unsigned long lastMqttPublish = 0;
const unsigned long MQTT_PUBLISH_INTERVAL = 10000; // Publish sensor data every 10 seconds

// ======================================
// TELEGRAM CONFIGURATION
// ======================================

// Telegram Bot Settings
String telegram_bot_token = "8599089200:AAEv1m09R8ga3f9HLbi8rAsODZrUO9qagmQ";  // Bot Token from @BotFather
String telegram_chat_id = "8550467615";    // Chat ID (can get from @userinfobot)
bool telegram_enabled = true;

// Telegram Client
WiFiClientSecure telegramClient;
UniversalTelegramBot *bot = nullptr;

// Telegram Alert Settings
bool telegram_alert_temp_high = true;     // แจ้งเตือนอุณหภูมิสูง
bool telegram_alert_temp_low = true;      // แจ้งเตือนอุณหภูมิต่ำ
bool telegram_alert_humidity_high = true; // แจ้งเตือนความชื้นสูง
bool telegram_alert_humidity_low = true;  // แจ้งเตือนความชื้นต่ำ
bool telegram_alert_water_level = true;   // แจ้งเตือนระดับน้ำ
bool telegram_alert_relay_status = true;  // แจ้งเตือนสถานะรีเลย์เปลี่ยน
bool telegram_alert_system_startup = true;// แจ้งเตือนเมื่อระบบเริ่มต้น

// Telegram Alert Cooldown (to prevent flooding)
unsigned long lastTempHighAlert = 0;
unsigned long lastTempLowAlert = 0;
unsigned long lastHumidityHighAlert = 0;
unsigned long lastHumidityLowAlert = 0;
unsigned long lastWaterLevelAlert = 0;
const unsigned long TELEGRAM_ALERT_COOLDOWN = 300000; // 5 minutes cooldown

// Previous state tracking for relay status alerts
bool prevFanState = false;
bool prevPumpState = false;
bool prevHeaterState = false;

// ======================================
// STATE PERSISTENCE (บันทึกสถานะอัตโนมัติ)
// ======================================

// Auto-save timing
unsigned long lastStateSave = 0;
const unsigned long STATE_SAVE_INTERVAL = 30000; // บันทึกสถานะทุก 30 วินาที
bool stateChanged = false; // ตรวจสอบว่ามีการเปลี่ยนแปลงหรือไม่

// Debounce for immediate save on relay change
unsigned long lastRelayChangeTime = 0;
const unsigned long RELAY_SAVE_DEBOUNCE = 2000; // รอ 2 วินาทีหลังจากเปลี่ยนสถานะก่อนบันทึก

// Forward declarations
void showWelcome();
void updateDisplay();
void updateDisplayPage0(); // Main page
void updateDisplayPage1(); // XY-MD03 page
void updateDisplayPage2(); // Weather page
void showCountdown(int seconds);
void setupWiFi();
void checkWiFiResetButton();
void readTemperature();
void simulateTemperature();
void readXYMD03();
void simulateXYMD03();
void switchPage();
void fetchWeatherData();
void fetchAirQualityData();
String getAQIDescription(int aqi_value);

// Automation functions
void autoTemperatureControl();
void autoHumidityControl();
void autoWaterLevelControl();
void checkSchedules();
void saveConfigToSPIFFS();
void loadConfigFromSPIFFS();
void setupWebServer();
String getSensorDataJSON();
String getRelayStatusJSON();

// State persistence functions
void saveStateToSPIFFS();
void loadStateFromSPIFFS();
void markStateChanged();
void checkAndSaveState();

// MQTT functions
void setupMQTT();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishSensorData();
void publishRelayStatus();
String getMQTTTopicsJSON();

// Telegram functions
void setupTelegram();
void sendTelegramMessage(String message);
void checkTelegramAlerts();
void sendTelegramSystemInfo();
void sendTelegramTemperatureAlert(bool isHigh);
void sendTelegramHumidityAlert(bool isHigh);
void sendTelegramWaterLevelAlert(String message);
void sendTelegramRelayStatusAlert(String relayName, bool state);

// Callback handlers
void onSw1Click() {
  // SW1: Switch page
  switchPage();
}

void onSw2Click() {
  // SW2: Down (reserved for future use)
}

void onSw3Click() {
  // SW3: Up (reserved for future use)
}

// Relay control helpers
void toggleFan() {
  relayFan.toggle();
  Serial.printf("Fan: %s\n", relayFan.getState() ? "ON" : "OFF");
  markStateChanged(); // บันทึกสถานะเมื่อมีการเปลี่ยนแปลง
}

void togglePump() {
  relayPump.toggle();
  Serial.printf("Pump: %s\n", relayPump.getState() ? "ON" : "OFF");
  markStateChanged(); // บันทึกสถานะเมื่อมีการเปลี่ยนแปลง
}

void toggleHeater() {
  relayHeater.toggle();
  Serial.printf("Heater: %s\n", relayHeater.getState() ? "ON" : "OFF");
  markStateChanged(); // บันทึกสถานะเมื่อมีการเปลี่ยนแปลง
}

// ISO callbacks
void onIso1Active() {
  Serial.println("ISO1: TankLevelSensor1 - DRY (active)");
}

void onIso1Inactive() {
  Serial.println("ISO1: TankLevelSensor1 - OK (inactive)");
}

void onIso2Active() {
  Serial.println("ISO2: TankLevelSensor2 - OVERFLOW (active)");
}

void onIso2Inactive() {
  Serial.println("ISO2: TankLevelSensor2 - OK (inactive)");
}

// --- OLED helpers ---
void showWelcome() {
  display.clearDisplay();
  
  // Draw frame
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawRect(1, 1, SCREEN_WIDTH-2, SCREEN_HEIGHT-2, SSD1306_WHITE);
  
  // Title
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.print("ESP32");
  display.setCursor(8, 24);
  display.print("DevKit V2");
  
  // Subtitle
  display.setTextSize(1);
  display.setCursor(10, 43);
  display.print("Smart Farm Control");
  
  // WiFi Status
  display.setTextSize(1);
  if (wifiConnected) {
    // Show IP Address (center align bottom)
    display.setCursor(6, 56);
    display.print("IP:");
    display.print(ipAddress);
  } else {
    // Show SSID for config
    display.setCursor(12, 56);
    display.print("SSID:ESP32_Farm");
  }
  
  display.display();
  delay(3000);
}

void showCountdown(int seconds) {
  display.clearDisplay();
  
  // Border
  display.drawRect(10, 10, SCREEN_WIDTH-20, SCREEN_HEIGHT-20, SSD1306_WHITE);
  
  // Warning message
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 16);
  display.println("WiFi Reset in:");
  
  // Countdown number (large)
  display.setTextSize(3);
  display.setCursor(52, 32);
  display.println(seconds);
  
  display.display();
}

void setupWiFi() {
  display.clearDisplay();
  
  // Draw border
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  
  // Title
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 8);
  display.print("WiFi Setup Mode");
  display.drawFastHLine(4, 18, SCREEN_WIDTH-8, SSD1306_WHITE);
  
  // Instructions
  display.setCursor(6, 24);
  display.print("Connect to WiFi:");
  
  // SSID (highlighted)
  display.setTextSize(2);
  display.setCursor(6, 36);
  display.print("ESP32_Farm");
  
  // Bottom instruction
  display.setTextSize(1);
  display.setCursor(4, 54);
  display.print("IP:192.168.4.1");
  
  display.display();
  
  Serial.println("Starting WiFi Manager...");
  Serial.println("SSID: ESP32_Farm");
  Serial.println("IP: 192.168.4.1");
  
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180); // 3 minutes timeout
  
  // Try to connect
  if (wifiManager.autoConnect("ESP32_Farm")) {
    wifiConnected = true;
    ipAddress = WiFi.localIP().toString();
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(ipAddress);
    
    // Show success message
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(20, 24);
    display.print("WiFi Connected!");
    display.setCursor(6, 36);
    display.print("IP:");
    display.print(ipAddress);
    display.display();
    delay(2000);
  } else {
    wifiConnected = false;
    ipAddress = "Not Connected";
    Serial.println("WiFi connection failed");
  }
}

// Check for WiFi reset button during startup
void checkWiFiResetButton() {
  // Initialize SW1 for reset check
  pinMode(PIN_SW1, INPUT_PULLUP);
  
  // Check if SW1 is pressed at startup
  bool sw1Pressed = (digitalRead(PIN_SW1) == LOW); // Active Low
  
  if (sw1Pressed) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(8, 20);
    display.print("Hold for WiFi Reset");
    display.display();
    
    unsigned long pressStart = millis();
    bool resetTriggered = false;
    
    // Wait and check if button held for 5 seconds
    while ((millis() - pressStart) < WIFI_RESET_HOLD_TIME) {
      // Check if button is still pressed
      if (digitalRead(PIN_SW1) != LOW) {
        // Button released
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(20, 28);
        display.print("Reset Cancelled");
        display.display();
        delay(1000);
        return;
      }
      
      // Show countdown
      unsigned long elapsed = millis() - pressStart;
      if (elapsed >= 1000) {
        int remainingSeconds = 5 - (elapsed / 1000);
        if (remainingSeconds >= 0) {
          showCountdown(remainingSeconds);
        }
      }
      
      delay(100);
    }
    
    // Button held for 5 seconds - reset WiFi
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(8, 24);
    display.print("Resetting WiFi...");
    display.display();
    
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    delay(1000);
    
    display.clearDisplay();
    display.setCursor(8, 24);
    display.print("WiFi Reset!");
    display.setCursor(8, 36);
    display.print("Rebooting...");
    display.display();
    delay(2000);
    
    ESP.restart();
  }
}

// Simulate temperature when sensor is not connected
void simulateTemperature() {
  // Simulate temperature fluctuation between 20°C and 30°C
  simulatedTemp += tempDelta;
  
  if (simulatedTemp >= 30.0) {
    tempDelta = -0.5;
    simulatedTemp = 30.0;
  } else if (simulatedTemp <= 20.0) {
    tempDelta = 0.5;
    simulatedTemp = 20.0;
  }
  
  currentTemperature = simulatedTemp;
}

// Read temperature from DS18B20 sensor
void readTemperature() {
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  
  // Check if reading is valid (DS18B20 returns -127 or 85 if sensor is disconnected)
  if (tempC != DEVICE_DISCONNECTED_C && tempC != 85.0 && tempC > -50.0 && tempC < 125.0) {
    sensorConnected = true;
    currentTemperature = tempC;
  } else {
    sensorConnected = false;
    simulateTemperature();
  }
}

// Simulate XY-MD03 values when sensor is not connected
void simulateXYMD03() {
  // Simulate temperature fluctuation between 23°C and 30°C
  simTempXY += simTempDeltaXY;
  if (simTempXY >= 30.0) {
    simTempDeltaXY = -0.3;
    simTempXY = 30.0;
  } else if (simTempXY <= 23.0) {
    simTempDeltaXY = 0.3;
    simTempXY = 23.0;
  }
  
  // Simulate humidity fluctuation between 50% and 80%
  simHumXY += simHumDeltaXY;
  if (simHumXY >= 80.0) {
    simHumDeltaXY = -1.0;
    simHumXY = 80.0;
  } else if (simHumXY <= 50.0) {
    simHumDeltaXY = 1.0;
    simHumXY = 50.0;
  }
  
  xymd03_temperature = simTempXY;
  xymd03_humidity = simHumXY;
}

// Read XY-MD03 Temperature & Humidity Sensor
void readXYMD03() {
  bool success = xymd03.update();
  
  if (success) {
    xymd03_connected = true;
    xymd03_temperature = xymd03.getTemperature();
    xymd03_humidity = xymd03.getHumidity();
  } else {
    xymd03_connected = false;
    simulateXYMD03();
  }
}

// Switch between display pages
void switchPage() {
  currentPage++;
  if (currentPage >= MAX_PAGES) {
    currentPage = 0;
  }
  
  // Show brief feedback flash
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 24);
  display.print("PAGE ");
  display.print(currentPage + 1);
  display.display();
  delay(150);
  
  // Update display immediately
  updateDisplay();
  lastDisplayUpdate = millis(); // Reset display timer
}

// Main display page (Page 0)
void updateDisplayPage0() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ========== HEADER ==========
  display.setTextSize(1);
  display.setCursor(2, 0);
  display.print("ESP32 Farm");
  display.setCursor(108, 0);
  display.print("1/3"); // Page indicator

  // ========== TEMPERATURE DISPLAY ==========
  display.setTextSize(1);
  display.setCursor(2, 13);
  display.print("Temp: ");
  display.print(currentTemperature, 1);
  display.print(" C");
  
  // Sensor status indicator
  if (!sensorConnected) {
    display.print(" [SIM]");
  } else {
    display.print(" [OK]");
  }
  
  // ========== SWITCH STATUS ==========
  display.setCursor(2, 25);
  display.print("SW:");
  display.print("U["); display.print(sw3.isPressed() ? "X" : " "); display.print("]");
  display.print("D["); display.print(sw2.isPressed() ? "X" : " "); display.print("]");
  display.print("S["); display.print(sw1.isPressed() ? "X" : " "); display.print("]");
  
  // ========== RELAY STATUS ==========
  display.setCursor(2, 35);
  display.print("RL:");
  display.print("F["); display.print(relayFan.getState() ? "X" : " "); display.print("]");
  display.print("P["); display.print(relayPump.getState() ? "X" : " "); display.print("]");
  display.print("H["); display.print(relayHeater.getState() ? "X" : " "); display.print("]");
  
  // ========== ISO INPUT STATUS ==========
  display.setCursor(2, 45);
  display.print("ISO:");
  display.print("T1[");
  display.print(iso1.isActive() ? "DRY" : "OK");
  display.print("] T2[");
  display.print(iso2.isActive() ? "FUL" : "OK");
  display.print("]");

  // ========== BOTTOM STATUS BAR (WIFI) ==========
  if (wifiConnected) {
    display.setCursor(2, 56);
    display.print("IP:");
    display.print(ipAddress);
  } else {
    display.setCursor(2, 56);
    display.print("WiFi: OFF");
  }
  
  display.display();
}

// XY-MD03 display page (Page 1)
void updateDisplayPage1() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ========== HEADER ==========
  display.setTextSize(1);
  display.setCursor(2, 0);
  display.print("XY-MD03");
  display.setCursor(100, 0);
  display.print("2/3"); // Page indicator

  // ========== SENSOR STATUS ==========
  display.setCursor(2, 13);
  display.print("Status: ");
  if (xymd03_connected) {
    display.print("Connected");
  } else {
    display.print("SIM Mode");
  }

  // ========== TEMPERATURE ==========
  display.setTextSize(1);
  display.setCursor(2, 26);
  display.print("Temperature: ");
  display.print(xymd03_temperature, 1);
  display.print(" C");

  // ========== HUMIDITY ==========
  display.setCursor(2, 37);
  display.print("Humidity:    ");
  display.print(xymd03_humidity, 1);
  display.print(" %");

  // ========== INFO ==========
  display.setCursor(2, 48);
  display.print("Modbus RS485");

  // ========== INSTRUCTION ==========
  display.setCursor(2, 56);
  display.print("SW1=Next Page");
  
  display.display();
}

// Get AQI description text
String getAQIDescription(int aqi_value) {
  switch(aqi_value) {
    case 1: return "Good";
    case 2: return "Fair";
    case 3: return "Moderate";
    case 4: return "Poor";
    case 5: return "Very Poor";
    default: return "Unknown";
  }
}

// Fetch weather data from OpenWeather API
void fetchWeatherData() {
  if (!wifiConnected) {
    Serial.println("WiFi not connected. Cannot fetch weather data.");
    return;
  }

  HTTPClient http;
  
  // URL encode city name (replace spaces with %20 for URL)
  String encodedCity = cityName;
  encodedCity.replace(" ", "%20");
  
  // Build URL for current weather
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + encodedCity + 
               "&appid=" + OPENWEATHER_API_KEY + "&units=metric";
  
  Serial.println("Fetching weather data for: " + cityName);
  Serial.println("URL: " + url);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Weather API Response: " + payload);
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      // Extract weather data
      weather_temp = doc["main"]["temp"];
      weather_feels_like = doc["main"]["feels_like"];
      weather_humidity = doc["main"]["humidity"];
      weather_pressure = doc["main"]["pressure"];
      weather_wind_speed = doc["wind"]["speed"];
      weather_description = doc["weather"][0]["description"].as<String>();
      weather_main = doc["weather"][0]["main"].as<String>();
      
      // Get coordinates for air quality API
      weather_lat = doc["coord"]["lat"];
      weather_lon = doc["coord"]["lon"];
      
      weatherDataAvailable = true;
      
      Serial.println("Weather data updated successfully!");
      Serial.printf("Temp: %.1f°C, Humidity: %.1f%%, Description: %s\n", 
                    weather_temp, weather_humidity, weather_description.c_str());
      
      // Fetch air quality data using coordinates
      fetchAirQualityData();
      
    } else {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      weatherDataAvailable = false;
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    weatherDataAvailable = false;
  }
  
  http.end();
}

// Fetch air quality data from OpenWeather API
void fetchAirQualityData() {
  if (!wifiConnected || weather_lat == 0.0 || weather_lon == 0.0) {
    Serial.println("Cannot fetch air quality: WiFi not connected or coordinates not available.");
    return;
  }

  HTTPClient http;
  
  // Build URL for air pollution
  String url = "http://api.openweathermap.org/data/2.5/air_pollution?lat=" + 
               String(weather_lat, 4) + "&lon=" + String(weather_lon, 4) + 
               "&appid=" + OPENWEATHER_API_KEY;
  
  Serial.println("Fetching air quality data...");
  Serial.println("URL: " + url);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Air Quality API Response: " + payload);
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      // Extract air quality data
      aqi = doc["list"][0]["main"]["aqi"];
      pm2_5 = doc["list"][0]["components"]["pm2_5"];
      pm10 = doc["list"][0]["components"]["pm10"];
      co = doc["list"][0]["components"]["co"];
      no2 = doc["list"][0]["components"]["no2"];
      o3 = doc["list"][0]["components"]["o3"];
      
      Serial.println("Air quality data updated successfully!");
      Serial.printf("AQI: %d (%s), PM2.5: %.1f μg/m³, PM10: %.1f μg/m³\n", 
                    aqi, getAQIDescription(aqi).c_str(), pm2_5, pm10);
      
    } else {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}

// Weather display page (Page 2)
void updateDisplayPage2() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ========== HEADER ==========
  display.setTextSize(1);
  display.setCursor(2, 0);
  display.print(cityName);
  display.setCursor(100, 0);
  display.print("3/3"); // Page indicator

  if (!weatherDataAvailable) {
    // Show message if no data available
    display.setCursor(10, 28);
    display.print("Fetching data...");
    display.display();
    return;
  }

  // ========== WEATHER INFO ==========
  display.setCursor(2, 11);
  display.print("Temp: ");
  display.print(weather_temp, 1);
  display.print("C");
  
  display.setCursor(70, 11);
  display.print("H:");
  display.print((int)weather_humidity);
  display.print("%");

  display.setCursor(2, 21);
  display.print("Feels: ");
  display.print(weather_feels_like, 1);
  display.print("C");

  // Weather description (truncate if too long)
  display.setCursor(2, 31);
  String desc = weather_description;
  if (desc.length() > 20) {
    desc = desc.substring(0, 20);
  }
  display.print(desc);

  // ========== AIR QUALITY ==========
  display.setCursor(2, 41);
  display.print("AQI:");
  display.print(getAQIDescription(aqi));
  display.print(" (");
  display.print(aqi);
  display.print(")");

  display.setCursor(2, 51);
  display.print("PM2.5:");
  display.print(pm2_5, 1);
  display.print(" PM10:");
  display.print(pm10, 0);

  display.display();
}

// ===== AUTOMATION FUNCTIONS =====

// Auto Temperature Control
void autoTemperatureControl() {
  if (!autoTempEnabled) return;
  
  float temp = currentTemperature;
  
  // Fan Control (ระบายความร้อน)
  if (temp >= TEMP_FAN_ON && !relayFan.getState()) {
    relayFan.on();
    markStateChanged();
    Serial.printf("AUTO: Fan ON - Temp %.1f°C >= %.1f°C\n", temp, TEMP_FAN_ON);
  } else if (temp <= TEMP_FAN_OFF && relayFan.getState()) {
    relayFan.off();
    markStateChanged();
    Serial.printf("AUTO: Fan OFF - Temp %.1f°C <= %.1f°C\n", temp, TEMP_FAN_OFF);
  }
  
  // Heater Control (เพิ่มความร้อน)
  if (temp <= TEMP_HEATER_ON && !relayHeater.getState()) {
    relayHeater.on();
    markStateChanged();
    Serial.printf("AUTO: Heater ON - Temp %.1f°C <= %.1f°C\n", temp, TEMP_HEATER_ON);
  } else if (temp >= TEMP_HEATER_OFF && relayHeater.getState()) {
    relayHeater.off();
    markStateChanged();
    Serial.printf("AUTO: Heater OFF - Temp %.1f°C >= %.1f°C\n", temp, TEMP_HEATER_OFF);
  }
}

// Auto Humidity Control
void autoHumidityControl() {
  if (!autoHumEnabled) return;
  
  float humidity = xymd03_humidity;
  
  // ถ้าความชื้นต่ำเกินไป เปิดปั๊มน้ำ (พ่นหมอก)
  if (humidity < HUM_MIN && !relayPump.getState()) {
    // ตรวจสอบว่าน้ำไม่แห้งก่อน
    if (!iso1.isActive()) {
      relayPump.on();
      markStateChanged();
      Serial.printf("AUTO: Pump ON - Humidity %.1f%% < %.1f%%\n", humidity, HUM_MIN);
    } else {
      Serial.println("AUTO: Cannot turn on Pump - Tank is DRY!");
    }
  } 
  // ถ้าความชื้นสูงเกินไป เปิดพัดลม (ระบายความชื้น)
  else if (humidity > HUM_MAX) {
    if (!relayFan.getState()) {
      relayFan.on();
      markStateChanged();
      Serial.printf("AUTO: Fan ON - Humidity %.1f%% > %.1f%%\n", humidity, HUM_MAX);
    }
    if (relayPump.getState()) {
      relayPump.off();
      markStateChanged();
      Serial.println("AUTO: Pump OFF - Humidity too high");
    }
  }
  // ถ้าความชื้นปกติ ปิดปั๊ม
  else if (humidity >= HUM_MIN && humidity <= HUM_MAX) {
    if (relayPump.getState()) {
      relayPump.off();
      markStateChanged();
      Serial.printf("AUTO: Pump OFF - Humidity normal (%.1f%%)\n", humidity);
    }
  }
}

// Auto Water Level Safety Control
void autoWaterLevelControl() {
  // ISO1 = Tank Dry Sensor (น้ำแห้ง)
  // ISO2 = Tank Overflow Sensor (น้ำล้น)
  
  // ถ้าน้ำแห้ง (ISO1 Active) ปิดปั๊มทันที
  if (iso1.isActive() && relayPump.getState()) {
    relayPump.off();
    markStateChanged();
    Serial.println("SAFETY: Pump OFF - Tank is DRY!");
  }
  
  // ถ้าน้ำล้น (ISO2 Active) ปิดปั๊มทันที
  if (iso2.isActive() && relayPump.getState()) {
    relayPump.off();
    markStateChanged();
    Serial.println("SAFETY: Pump OFF - Tank OVERFLOW!");
  }
}

// Schedule System
void checkSchedules() {
  if (!scheduleEnabled) return;
  
  // Get current time (requires NTP sync)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return; // No time available
  }
  
  int currentMinute = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  
  // Prevent double execution in the same minute
  if (currentMinute == lastScheduleMinute) {
    return;
  }
  
  lastScheduleMinute = currentMinute;
  
  // Check each schedule
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (!schedules[i].enabled || schedules[i].relayNum == 0) continue;
    
    // Check day of week (bit0=Sun, bit1=Mon, ..., bit6=Sat)
    uint8_t todayBit = (1 << timeinfo.tm_wday);
    if (schedules[i].days != 0 && !(schedules[i].days & todayBit)) continue;
    
    if (timeinfo.tm_hour == schedules[i].hour && 
        timeinfo.tm_min == schedules[i].minute) {
      
      DevRelayWithTimer* relay = nullptr;
      const char* relayName = "";
      
      switch(schedules[i].relayNum) {
        case 1: relay = &relayFan; relayName = "Fan"; break;
        case 2: relay = &relayPump; relayName = "Pump"; break;
        case 3: relay = &relayHeater; relayName = "Heater"; break;
      }
      
      if (relay) {
        if (schedules[i].turnOn) {
          relay->on();
          markStateChanged();
          Serial.printf("SCHEDULE: %s ON at %02d:%02d\n", 
                       relayName, schedules[i].hour, schedules[i].minute);
        } else {
          relay->off();
          markStateChanged();
          Serial.printf("SCHEDULE: %s OFF at %02d:%02d\n", 
                       relayName, schedules[i].hour, schedules[i].minute);
        }
      }
    }
  }
}

// ======================================
// MQTT FUNCTIONS
// ======================================

// Setup MQTT Connection
void setupMQTT() {
  mqttClient.setServer(mqtt_server.c_str(), mqtt_port);
  mqttClient.setCallback(mqttCallback);
  
  // Generate unique Client ID based on MAC Address
  uint8_t mac[6];
  WiFi.macAddress(mac);
  mqtt_client_id = "ESP32Farm-";
  mqtt_client_id += String(mac[3], HEX);
  mqtt_client_id += String(mac[4], HEX);
  mqtt_client_id += String(mac[5], HEX);
  mqtt_client_id.toUpperCase();
  
  Serial.println("MQTT client initialized");
  Serial.printf("MQTT Server: %s:%d\n", mqtt_server.c_str(), mqtt_port);
  Serial.printf("MQTT Client ID: %s\n", mqtt_client_id.c_str());
  Serial.printf("Topic Prefix: %s\n", mqtt_topic_prefix.c_str());
}

// MQTT Callback for Subscribed Topics
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("MQTT Message received [%s]: %s\n", topic, message.c_str());
  
  // Parse topic and handle commands
  String topicStr = String(topic);
  
  // Control Relay 1 (Fan)
  if (topicStr == mqtt_topic_prefix + "/relay/1/set") {
    if (message == "ON" || message == "1") {
      relayFan.on();
      markStateChanged();
      publishRelayStatus();
      Serial.println("MQTT: Fan turned ON");
    } else if (message == "OFF" || message == "0") {
      relayFan.off();
      markStateChanged();
      publishRelayStatus();
      Serial.println("MQTT: Fan turned OFF");
    }
  }
  // Control Relay 2 (Pump)
  else if (topicStr == mqtt_topic_prefix + "/relay/2/set") {
    if (message == "ON" || message == "1") {
      relayPump.on();
      markStateChanged();
      publishRelayStatus();
      Serial.println("MQTT: Pump turned ON");
    } else if (message == "OFF" || message == "0") {
      relayPump.off();
      markStateChanged();
      publishRelayStatus();
      Serial.println("MQTT: Pump turned OFF");
    }
  }
  // Control Relay 3 (Heater)
  else if (topicStr == mqtt_topic_prefix + "/relay/3/set") {
    if (message == "ON" || message == "1") {
      relayHeater.on();
      markStateChanged();
      publishRelayStatus();
      Serial.println("MQTT: Heater turned ON");
    } else if (message == "OFF" || message == "0") {
      relayHeater.off();
      markStateChanged();
      publishRelayStatus();
      Serial.println("MQTT: Heater turned OFF");
    }
  }
}

// Reconnect to MQTT Broker
void reconnectMQTT() {
  if (!mqtt_enabled || !wifiConnected) {
    mqttConnected = false;
    return;
  }
  
  if (mqttClient.connected()) {
    mqttConnected = true;
    return;
  }
  
  unsigned long now = millis();
  if (now - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL) {
    return;
  }
  
  lastMqttReconnectAttempt = now;
  
  Serial.print("Attempting MQTT connection...");
  Serial.printf(" (Client ID: %s)\n", mqtt_client_id.c_str());
  
  // Attempt to connect using the unique Client ID
  bool connected = false;
  if (mqtt_username.length() > 0) {
    connected = mqttClient.connect(mqtt_client_id.c_str(), mqtt_username.c_str(), mqtt_password.c_str());
  } else {
    connected = mqttClient.connect(mqtt_client_id.c_str());
  }
  
  if (connected) {
    Serial.println("connected");
    mqttConnected = true;
    
    // Subscribe to control topics
    String topic1 = mqtt_topic_prefix + "/relay/1/set";
    String topic2 = mqtt_topic_prefix + "/relay/2/set";
    String topic3 = mqtt_topic_prefix + "/relay/3/set";
    
    mqttClient.subscribe(topic1.c_str());
    mqttClient.subscribe(topic2.c_str());
    mqttClient.subscribe(topic3.c_str());
    
    Serial.printf("Subscribed to: %s, %s, %s\n", topic1.c_str(), topic2.c_str(), topic3.c_str());
    
    // Publish initial status
    publishSensorData();
    publishRelayStatus();
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" will try again later");
    mqttConnected = false;
  }
}

// Publish Sensor Data to MQTT
void publishSensorData() {
  if (!mqttConnected || !mqttClient.connected()) {
    return;
  }
  
  // Publish DS18B20 Temperature to separate topic
  JsonDocument ds18b20Doc;
  ds18b20Doc["temperature"] = currentTemperature;
  ds18b20Doc["connected"] = sensorConnected;
  ds18b20Doc["unit"] = "celsius";
  
  String ds18b20Json;
  serializeJson(ds18b20Doc, ds18b20Json);
  String ds18b20Topic = mqtt_topic_prefix + "/ds18b20";
  mqttClient.publish(ds18b20Topic.c_str(), ds18b20Json.c_str());
  Serial.printf("MQTT Published DS18B20 to %s\n", ds18b20Topic.c_str());
  
  // Publish XY-MD03 Temperature & Humidity to separate topic
  JsonDocument xymd03Doc;
  xymd03Doc["temperature"] = xymd03_temperature;
  xymd03Doc["humidity"] = xymd03_humidity;
  xymd03Doc["connected"] = xymd03_connected;
  xymd03Doc["temp_unit"] = "celsius";
  xymd03Doc["humidity_unit"] = "percent";
  
  String xymd03Json;
  serializeJson(xymd03Doc, xymd03Json);
  String xymd03Topic = mqtt_topic_prefix + "/xymd03";
  mqttClient.publish(xymd03Topic.c_str(), xymd03Json.c_str());
  Serial.printf("MQTT Published XY-MD03 to %s\n", xymd03Topic.c_str());
  
  // Publish all sensors combined (backward compatibility)
  JsonDocument doc;
  
  // DS18B20 Temperature
  doc["ds18b20"]["temperature"] = currentTemperature;
  doc["ds18b20"]["connected"] = sensorConnected;
  
  // XY-MD03 Temperature & Humidity
  doc["xymd03"]["temperature"] = xymd03_temperature;
  doc["xymd03"]["humidity"] = xymd03_humidity;
  doc["xymd03"]["connected"] = xymd03_connected;
  
  // Weather Data
  doc["weather"]["temperature"] = weather_temp;
  doc["weather"]["humidity"] = weather_humidity;
  doc["weather"]["description"] = weather_description;
  
  // Isolated Inputs
  doc["iso1"] = iso1.isActive();
  doc["iso2"] = iso2.isActive();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  String topic = mqtt_topic_prefix + "/sensors";
  mqttClient.publish(topic.c_str(), jsonString.c_str());
  
  Serial.printf("MQTT Published all sensors to %s\n", topic.c_str());
}

// Publish Relay Status to MQTT
void publishRelayStatus() {
  if (!mqttConnected || !mqttClient.connected()) {
    return;
  }
  
  JsonDocument doc;
  doc["relay1"] = relayFan.getState();
  doc["relay2"] = relayPump.getState();
  doc["relay3"] = relayHeater.getState();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  String topic = mqtt_topic_prefix + "/relays/status";
  mqttClient.publish(topic.c_str(), jsonString.c_str());
  
  Serial.printf("MQTT Published relay status to %s\n", topic.c_str());
}

// Get MQTT Topics as JSON
String getMQTTTopicsJSON() {
  JsonDocument doc;
  
  doc["enabled"] = mqtt_enabled;
  doc["connected"] = mqttConnected;
  doc["server"] = mqtt_server;
  doc["port"] = mqtt_port;
  doc["prefix"] = mqtt_topic_prefix;
  doc["clientId"] = mqtt_client_id;
  
  // List all topics
  JsonObject topics = doc["topics"].to<JsonObject>();
  topics["ds18b20"] = mqtt_topic_prefix + "/ds18b20";
  topics["xymd03"] = mqtt_topic_prefix + "/xymd03";
  topics["sensors"] = mqtt_topic_prefix + "/sensors";
  topics["relay_status"] = mqtt_topic_prefix + "/relays/status";
  topics["relay1_set"] = mqtt_topic_prefix + "/relay/1/set";
  topics["relay2_set"] = mqtt_topic_prefix + "/relay/2/set";
  topics["relay3_set"] = mqtt_topic_prefix + "/relay/3/set";
  
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

// ======================================
// TELEGRAM FUNCTIONS
// ======================================

// Setup Telegram Bot
void setupTelegram() {
  if (!telegram_enabled || telegram_bot_token.length() == 0) {
    Serial.println("Telegram disabled or no token configured");
    return;
  }
  
  // Set up secure client for Telegram API
  telegramClient.setInsecure(); // For simplicity, skip certificate validation
  
  // Create bot instance
  if (bot != nullptr) {
    delete bot;
  }
  bot = new UniversalTelegramBot(telegram_bot_token, telegramClient);
  
  Serial.println("Telegram Bot initialized");
  Serial.printf("Bot Token: %s...%s\n", 
                telegram_bot_token.substring(0, 10).c_str(),
                telegram_bot_token.substring(telegram_bot_token.length() - 10).c_str());
  Serial.printf("Chat ID: %s\n", telegram_chat_id.c_str());
}

// Send Telegram Message
void sendTelegramMessage(String message) {
  if (!telegram_enabled || bot == nullptr || telegram_chat_id.length() == 0) {
    return;
  }
  
  // Add system prefix
  String fullMessage = "🌱 *Smart Farm Alert*\n\n" + message;
  
  // Send message
  bool sent = bot->sendMessage(telegram_chat_id, fullMessage, "Markdown");
  
  if (sent) {
    Serial.println("Telegram message sent successfully");
  } else {
    Serial.println("Failed to send Telegram message");
  }
}

// Send System Info
void sendTelegramSystemInfo() {
  String message = "📊 *ข้อมูลระบบ Smart Farm*\n\n";
  
  // WiFi Info
  message += "🌐 *WiFi*\n";
  message += "• IP: " + ipAddress + "\n";
  message += "• Signal: " + String(WiFi.RSSI()) + " dBm\n\n";
  
  // Temperature Sensors
  message += "🌡️ *เซ็นเซอร์อุณหภูมิ*\n";
  message += "• DS18B20: " + String(currentTemperature, 1) + "°C";
  if (!sensorConnected) message += " (ไม่เชื่อมต่อ)";
  message += "\n";
  
  message += "• XY-MD03: " + String(xymd03_temperature, 1) + "°C";
  if (!xymd03_connected) message += " (ไม่เชื่อมต่อ)";
  message += "\n\n";
  
  // Humidity
  message += "💧 *ความชื้น*\n";
  message += "• XY-MD03: " + String(xymd03_humidity, 1) + "%";
  if (!xymd03_connected) message += " (ไม่เชื่อมต่อ)";
  message += "\n\n";
  
  // Relay Status
  message += "⚡ *สถานะรีเลย์*\n";
  message += "• พัดลม: " + String(relayFan.getState() ? "🟢 เปิด" : "🔴 ปิด") + "\n";
  message += "• ปั๊มน้ำ: " + String(relayPump.getState() ? "🟢 เปิด" : "🔴 ปิด") + "\n";
  message += "• ฮีตเตอร์: " + String(relayHeater.getState() ? "🟢 เปิด" : "🔴 ปิด") + "\n\n";
  
  // Water Level
  message += "🚰 *ระดับน้ำ*\n";
  bool waterDry = iso1.isActive();
  bool waterOverflow = iso2.isActive();
  if (waterDry) {
    message += "• ⚠️ น้ำแห้ง\n";
  } else if (waterOverflow) {
    message += "• ⚠️ น้ำล้น\n";
  } else {
    message += "• ✅ ปกติ\n";
  }
  
  // Automation Status
  message += "\n🤖 *ระบบอัตโนมัติ*\n";
  message += "• ควบคุมอุณหภูมิ: " + String(autoTempEnabled ? "✅ เปิด" : "❌ ปิด") + "\n";
  message += "• ควบคุมความชื้น: " + String(autoHumEnabled ? "✅ เปิด" : "❌ ปิด") + "\n";
  message += "• ระบบตั้งเวลา: " + String(scheduleEnabled ? "✅ เปิด" : "❌ ปิด") + "\n";
  
  sendTelegramMessage(message);
}

// Send Temperature Alert
void sendTelegramTemperatureAlert(bool isHigh) {
  unsigned long now = millis();
  
  if (isHigh) {
    if (now - lastTempHighAlert < TELEGRAM_ALERT_COOLDOWN) return;
    lastTempHighAlert = now;
    
    String message = "🔥 *แจ้งเตือน: อุณหภูมิสูง!*\n\n";
    message += "อุณหภูมิปัจจุบัน: *" + String(xymd03_temperature, 1) + "°C*\n";
    message += "เกินค่าที่กำหนด: " + String(TEMP_FAN_ON, 1) + "°C\n\n";
    message += "💨 พัดลมกำลัง" + String(relayFan.getState() ? "เปิด" : "ปิด");
    
    sendTelegramMessage(message);
  } else {
    if (now - lastTempLowAlert < TELEGRAM_ALERT_COOLDOWN) return;
    lastTempLowAlert = now;
    
    String message = "❄️ *แจ้งเตือน: อุณหภูมิต่ำ!*\n\n";
    message += "อุณหภูมิปัจจุบัน: *" + String(xymd03_temperature, 1) + "°C*\n";
    message += "ต่ำกว่าค่าที่กำหนด: " + String(TEMP_HEATER_ON, 1) + "°C\n\n";
    message += "🔥 ฮีตเตอร์กำลัง" + String(relayHeater.getState() ? "เปิด" : "ปิด");
    
    sendTelegramMessage(message);
  }
}

// Send Humidity Alert
void sendTelegramHumidityAlert(bool isHigh) {
  unsigned long now = millis();
  
  if (isHigh) {
    if (now - lastHumidityHighAlert < TELEGRAM_ALERT_COOLDOWN) return;
    lastHumidityHighAlert = now;
    
    String message = "💧 *แจ้งเตือน: ความชื้นสูง!*\n\n";
    message += "ความชื้นปัจจุบัน: *" + String(xymd03_humidity, 1) + "%*\n";
    message += "เกินค่าที่กำหนด: " + String(HUM_MAX, 1) + "%\n\n";
    message += "💨 พัดลมกำลัง" + String(relayFan.getState() ? "เปิด" : "ปิด");
    
    sendTelegramMessage(message);
  } else {
    if (now - lastHumidityLowAlert < TELEGRAM_ALERT_COOLDOWN) return;
    lastHumidityLowAlert = now;
    
    String message = "🏜️ *แจ้งเตือน: ความชื้นต่ำ!*\n\n";
    message += "ความชื้นปัจจุบัน: *" + String(xymd03_humidity, 1) + "%*\n";
    message += "ต่ำกว่าค่าที่กำหนด: " + String(HUM_MIN, 1) + "%\n\n";
    message += "💦 ปั๊มน้ำกำลัง" + String(relayPump.getState() ? "เปิด" : "ปิด");
    
    sendTelegramMessage(message);
  }
}

// Send Water Level Alert
void sendTelegramWaterLevelAlert(String alertMessage) {
  unsigned long now = millis();
  if (now - lastWaterLevelAlert < TELEGRAM_ALERT_COOLDOWN) return;
  lastWaterLevelAlert = now;
  
  String message = "🚰 *แจ้งเตือน: ระดับน้ำ!*\n\n";
  message += alertMessage;
  
  sendTelegramMessage(message);
}

// Send Relay Status Change Alert
void sendTelegramRelayStatusAlert(String relayName, bool state) {
  String emoji = state ? "🟢" : "🔴";
  String status = state ? "เปิด" : "ปิด";
  
  String message = emoji + " *" + relayName + "* ถูก*" + status + "*\n\n";
  message += "เวลา: " + String(millis() / 1000) + " วินาที";
  
  sendTelegramMessage(message);
}

// Check and Send Telegram Alerts
void checkTelegramAlerts() {
  if (!telegram_enabled || bot == nullptr) {
    return;
  }
  
  // Check temperature alerts
  if (telegram_alert_temp_high && xymd03_connected && xymd03_temperature > TEMP_FAN_ON) {
    sendTelegramTemperatureAlert(true);
  }
  if (telegram_alert_temp_low && xymd03_connected && xymd03_temperature < TEMP_HEATER_ON) {
    sendTelegramTemperatureAlert(false);
  }
  
  // Check humidity alerts
  if (telegram_alert_humidity_high && xymd03_connected && xymd03_humidity > HUM_MAX) {
    sendTelegramHumidityAlert(true);
  }
  if (telegram_alert_humidity_low && xymd03_connected && xymd03_humidity < HUM_MIN) {
    sendTelegramHumidityAlert(false);
  }
  
  // Check water level alerts
  if (telegram_alert_water_level) {
    bool waterDry = iso1.isActive();
    bool waterOverflow = iso2.isActive();
    
    if (waterDry) {
      sendTelegramWaterLevelAlert("⚠️ *แท้งค์น้ำแห้ง!*\nกรุณาเติมน้ำ");
    } else if (waterOverflow) {
      sendTelegramWaterLevelAlert("⚠️ *น้ำล้น!*\nกรุณาตรวจสอบระบบ");
    }
  }
  
  // Check relay status changes
  if (telegram_alert_relay_status) {
    bool currentFanState = relayFan.getState();
    bool currentPumpState = relayPump.getState();
    bool currentHeaterState = relayHeater.getState();
    
    if (currentFanState != prevFanState) {
      sendTelegramRelayStatusAlert("พัดลม", currentFanState);
      prevFanState = currentFanState;
    }
    if (currentPumpState != prevPumpState) {
      sendTelegramRelayStatusAlert("ปั๊มน้ำ", currentPumpState);
      prevPumpState = currentPumpState;
    }
    if (currentHeaterState != prevHeaterState) {
      sendTelegramRelayStatusAlert("ฮีตเตอร์", currentHeaterState);
      prevHeaterState = currentHeaterState;
    }
  }
}

// Save config to SPIFFS
void saveConfigToSPIFFS() {
  File file = SPIFFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  
  JsonDocument doc;
  doc["tempFanOn"] = TEMP_FAN_ON;
  doc["tempFanOff"] = TEMP_FAN_OFF;
  doc["tempHeaterOn"] = TEMP_HEATER_ON;
  doc["tempHeaterOff"] = TEMP_HEATER_OFF;
  doc["humMin"] = HUM_MIN;
  doc["humMax"] = HUM_MAX;
  doc["autoTempEnabled"] = autoTempEnabled;
  doc["autoHumEnabled"] = autoHumEnabled;
  doc["scheduleEnabled"] = scheduleEnabled;
  doc["cityName"] = cityName;
  
  // Save MQTT settings
  doc["mqttEnabled"] = mqtt_enabled;
  doc["mqttServer"] = mqtt_server;
  doc["mqttPort"] = mqtt_port;
  doc["mqttUsername"] = mqtt_username;
  doc["mqttPassword"] = mqtt_password;
  doc["mqttTopicPrefix"] = mqtt_topic_prefix;
  
  // Save Telegram settings
  doc["telegramEnabled"] = telegram_enabled;
  doc["telegramBotToken"] = telegram_bot_token;
  doc["telegramChatId"] = telegram_chat_id;
  doc["telegramAlertTempHigh"] = telegram_alert_temp_high;
  doc["telegramAlertTempLow"] = telegram_alert_temp_low;
  doc["telegramAlertHumidityHigh"] = telegram_alert_humidity_high;
  doc["telegramAlertHumidityLow"] = telegram_alert_humidity_low;
  doc["telegramAlertWaterLevel"] = telegram_alert_water_level;
  doc["telegramAlertRelayStatus"] = telegram_alert_relay_status;
  doc["telegramAlertSystemStartup"] = telegram_alert_system_startup;
  
  // Save relay states (สถานะรีเลย์ล่าสุดก่อนไฟดับ/รีเซต)
  doc["relayFanState"] = relayFan.getState();
  doc["relayPumpState"] = relayPump.getState();
  doc["relayHeaterState"] = relayHeater.getState();
  
  // Save schedules
  JsonArray schedArray = doc["schedules"].to<JsonArray>();
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    schedArray[i]["hour"] = schedules[i].hour;
    schedArray[i]["minute"] = schedules[i].minute;
    schedArray[i]["relayNum"] = schedules[i].relayNum;
    schedArray[i]["turnOn"] = schedules[i].turnOn;
    schedArray[i]["enabled"] = schedules[i].enabled;
    schedArray[i]["days"] = schedules[i].days;
  }
  
  serializeJson(doc, file);
  file.close();
  Serial.println("Config saved to SPIFFS");
}

// Load config from SPIFFS
void loadConfigFromSPIFFS() {
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    Serial.println("No config file found, using defaults");
    return;
  }
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.println("Failed to parse config file");
    return;
  }
  
  // Load settings
  TEMP_FAN_ON = doc["tempFanOn"] | TEMP_FAN_ON;
  TEMP_FAN_OFF = doc["tempFanOff"] | TEMP_FAN_OFF;
  TEMP_HEATER_ON = doc["tempHeaterOn"] | TEMP_HEATER_ON;
  TEMP_HEATER_OFF = doc["tempHeaterOff"] | TEMP_HEATER_OFF;
  HUM_MIN = doc["humMin"] | HUM_MIN;
  HUM_MAX = doc["humMax"] | HUM_MAX;
  autoTempEnabled = doc["autoTempEnabled"] | autoTempEnabled;
  autoHumEnabled = doc["autoHumEnabled"] | autoHumEnabled;
  scheduleEnabled = doc["scheduleEnabled"] | scheduleEnabled;
  cityName = doc["cityName"] | cityName;
  
  // Load MQTT settings
  mqtt_enabled = doc["mqttEnabled"] | mqtt_enabled;
  mqtt_server = doc["mqttServer"] | mqtt_server;
  mqtt_port = doc["mqttPort"] | mqtt_port;
  mqtt_username = doc["mqttUsername"] | mqtt_username;
  mqtt_password = doc["mqttPassword"] | mqtt_password;
  mqtt_topic_prefix = doc["mqttTopicPrefix"] | mqtt_topic_prefix;
  
  // Load Telegram settings
  telegram_enabled = doc["telegramEnabled"] | telegram_enabled;
  telegram_bot_token = doc["telegramBotToken"] | telegram_bot_token;
  telegram_chat_id = doc["telegramChatId"] | telegram_chat_id;
  telegram_alert_temp_high = doc["telegramAlertTempHigh"] | telegram_alert_temp_high;
  telegram_alert_temp_low = doc["telegramAlertTempLow"] | telegram_alert_temp_low;
  telegram_alert_humidity_high = doc["telegramAlertHumidityHigh"] | telegram_alert_humidity_high;
  telegram_alert_humidity_low = doc["telegramAlertHumidityLow"] | telegram_alert_humidity_low;
  telegram_alert_water_level = doc["telegramAlertWaterLevel"] | telegram_alert_water_level;
  telegram_alert_relay_status = doc["telegramAlertRelayStatus"] | telegram_alert_relay_status;
  telegram_alert_system_startup = doc["telegramAlertSystemStartup"] | telegram_alert_system_startup;
  
  // Load and restore relay states (คืนค่าสถานะรีเลย์ที่บันทึกไว้)
  bool savedFanState = doc["relayFanState"] | false;
  bool savedPumpState = doc["relayPumpState"] | false;
  bool savedHeaterState = doc["relayHeaterState"] | false;
  
  // Restore relay states
  if (savedFanState) {
    relayFan.on();
    Serial.println("Restored Fan state: ON");
  } else {
    relayFan.off();
    Serial.println("Restored Fan state: OFF");
  }
  
  if (savedPumpState) {
    relayPump.on();
    Serial.println("Restored Pump state: ON");
  } else {
    relayPump.off();
    Serial.println("Restored Pump state: OFF");
  }
  
  if (savedHeaterState) {
    relayHeater.on();
    Serial.println("Restored Heater state: ON");
  } else {
    relayHeater.off();
    Serial.println("Restored Heater state: OFF");
  }
  
  // Update previous state tracking for alerts
  prevFanState = savedFanState;
  prevPumpState = savedPumpState;
  prevHeaterState = savedHeaterState;
  
  // Load schedules
  JsonArray schedArray = doc["schedules"];
  if (schedArray) {
    int idx = 0;
    for (JsonObject sched : schedArray) {
      if (idx >= MAX_SCHEDULES) break;
      schedules[idx].hour = sched["hour"];
      schedules[idx].minute = sched["minute"];
      schedules[idx].relayNum = sched["relayNum"];
      schedules[idx].turnOn = sched["turnOn"];
      schedules[idx].enabled = sched["enabled"];
      schedules[idx].days = sched["days"].isNull() ? 0x7F : (uint8_t)sched["days"].as<int>(); // Default: ทุกวัน
      idx++;
    }
  }
  
  Serial.println("Config loaded from SPIFFS");
}

// ======================================
// STATE PERSISTENCE FUNCTIONS (บันทึก/โหลดสถานะอัตโนมัติ)
// ======================================

// Save current runtime state to SPIFFS (เร็วกว่า saveConfigToSPIFFS)
void saveStateToSPIFFS() {
  File file = SPIFFS.open("/state.json", "w");
  if (!file) {
    Serial.println("Failed to open state file for writing");
    return;
  }
  
  JsonDocument doc;
  
  // Save relay states
  doc["fanState"] = relayFan.getState();
  doc["pumpState"] = relayPump.getState();
  doc["heaterState"] = relayHeater.getState();
  
  // Save automation states
  doc["autoTempEnabled"] = autoTempEnabled;
  doc["autoHumEnabled"] = autoHumEnabled;
  doc["scheduleEnabled"] = scheduleEnabled;
  
  // Save last sensor readings (for display before new reading)
  doc["lastTemp"] = currentTemperature;
  doc["lastXYTemp"] = xymd03_temperature;
  doc["lastXYHum"] = xymd03_humidity;
  
  // Save timestamp
  doc["timestamp"] = millis();
  
  serializeJson(doc, file);
  file.close();
  
  Serial.println("✓ State saved to SPIFFS");
  stateChanged = false; // Reset change flag
}

// Load runtime state from SPIFFS
void loadStateFromSPIFFS() {
  File file = SPIFFS.open("/state.json", "r");
  if (!file) {
    Serial.println("No state file found, using defaults");
    return;
  }
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.println("Failed to parse state file");
    return;
  }
  
  // Restore relay states
  bool fanState = doc["fanState"] | false;
  bool pumpState = doc["pumpState"] | false;
  bool heaterState = doc["heaterState"] | false;
  
  if (fanState) {
    relayFan.on();
    Serial.println("↻ Restored Fan: ON");
  } else {
    relayFan.off();
    Serial.println("↻ Restored Fan: OFF");
  }
  
  if (pumpState) {
    relayPump.on();
    Serial.println("↻ Restored Pump: ON");
  } else {
    relayPump.off();
    Serial.println("↻ Restored Pump: OFF");
  }
  
  if (heaterState) {
    relayHeater.on();
    Serial.println("↻ Restored Heater: ON");
  } else {
    relayHeater.off();
    Serial.println("↻ Restored Heater: OFF");
  }
  
  // Restore automation states
  autoTempEnabled = doc["autoTempEnabled"] | autoTempEnabled;
  autoHumEnabled = doc["autoHumEnabled"] | autoHumEnabled;
  scheduleEnabled = doc["scheduleEnabled"] | scheduleEnabled;
  
  // Restore last sensor readings (optional - will be updated soon)
  currentTemperature = doc["lastTemp"] | currentTemperature;
  xymd03_temperature = doc["lastXYTemp"] | xymd03_temperature;
  xymd03_humidity = doc["lastXYHum"] | xymd03_humidity;
  
  // Update previous state tracking
  prevFanState = fanState;
  prevPumpState = pumpState;
  prevHeaterState = heaterState;
  
  unsigned long savedTime = doc["timestamp"] | 0;
  Serial.printf("✓ State loaded from SPIFFS (saved at: %lu ms)\n", savedTime);
}

// Mark that state has changed (เรียกเมื่อมีการเปลี่ยนแปลง)
void markStateChanged() {
  stateChanged = true;
  lastRelayChangeTime = millis();
}

// Check and save state if changed (เรียกใน loop)
void checkAndSaveState() {
  unsigned long now = millis();
  
  // Immediate save after relay change (with debounce)
  if (stateChanged && (now - lastRelayChangeTime >= RELAY_SAVE_DEBOUNCE)) {
    saveStateToSPIFFS();
    // Also update main config to keep it in sync
    saveConfigToSPIFFS();
    return;
  }
  
  // Periodic save (backup)
  if (now - lastStateSave >= STATE_SAVE_INTERVAL) {
    lastStateSave = now;
    if (stateChanged) {
      saveStateToSPIFFS();
      saveConfigToSPIFFS();
    }
  }
}

// ===== WEB SERVER FUNCTIONS =====

// Get sensor data as JSON
String getSensorDataJSON() {
  JsonDocument doc;
  
  // DS18B20
  JsonObject ds18b20 = doc["ds18b20"].to<JsonObject>();
  ds18b20["temperature"] = currentTemperature;
  ds18b20["connected"] = sensorConnected;
  
  // XY-MD03
  JsonObject xymd03 = doc["xymd03"].to<JsonObject>();
  xymd03["temperature"] = xymd03_temperature;
  xymd03["humidity"] = xymd03_humidity;
  xymd03["connected"] = xymd03_connected;
  
  // ISO Inputs
  JsonObject iso1Obj = doc["iso1"].to<JsonObject>();
  iso1Obj["active"] = iso1.isActive();
  
  JsonObject iso2Obj = doc["iso2"].to<JsonObject>();
  iso2Obj["active"] = iso2.isActive();
  
  // Weather
  JsonObject weather = doc["weather"].to<JsonObject>();
  weather["available"] = weatherDataAvailable;
  weather["city"] = cityName;
  weather["temperature"] = weather_temp;
  weather["humidity"] = weather_humidity;
  weather["feelsLike"] = weather_feels_like;
  weather["windSpeed"] = weather_wind_speed;
  weather["description"] = weather_description;
  weather["aqi"] = aqi;
  weather["pm25"] = pm2_5;
  weather["pm10"] = pm10;
  
  // Relays
  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["fan"] = relayFan.getState();
  relays["pump"] = relayPump.getState();
  relays["heater"] = relayHeater.getState();
  
  String output;
  serializeJson(doc, output);
  return output;
}

// Setup Web Server
void setupWebServer() {
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
  
  // Enable CORS for all API requests
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // Serve index.html from SPIFFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  // Serve settings.html from SPIFFS
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/settings.html", "text/html");
  });
  
  // API: Get system info
  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("API: /api/info called");
    JsonDocument doc;
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // API: Get all sensor data
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("API: /api/sensors called");
    String json = getSensorDataJSON();
    Serial.print("JSON Response: ");
    Serial.println(json);
    request->send(200, "application/json", json);
  });
  
  // API: Control Relay 1 (Fan)
  server.on("/api/relay/1/on", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayFan.on();
    markStateChanged();
    JsonDocument doc;
    doc["success"] = true;
    doc["relay"] = 1;
    doc["state"] = true;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    Serial.println("Web: Fan ON");
  });
  
  server.on("/api/relay/1/off", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayFan.off();
    markStateChanged();
    JsonDocument doc;
    doc["success"] = true;
    doc["relay"] = 1;
    doc["state"] = false;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    Serial.println("Web: Fan OFF");
  });
  
  // API: Control Relay 2 (Pump)
  server.on("/api/relay/2/on", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayPump.on();
    markStateChanged();
    JsonDocument doc;
    doc["success"] = true;
    doc["relay"] = 2;
    doc["state"] = true;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    Serial.println("Web: Pump ON");
  });
  
  server.on("/api/relay/2/off", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayPump.off();
    markStateChanged();
    JsonDocument doc;
    doc["success"] = true;
    doc["relay"] = 2;
    doc["state"] = false;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    Serial.println("Web: Pump OFF");
  });
  
  // API: Control Relay 3 (Heater)
  server.on("/api/relay/3/on", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayHeater.on();
    markStateChanged();
    JsonDocument doc;
    doc["success"] = true;
    doc["relay"] = 3;
    doc["state"] = true;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    Serial.println("Web: Heater ON");
  });
  
  server.on("/api/relay/3/off", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayHeater.off();
    markStateChanged();
    JsonDocument doc;
    doc["success"] = true;
    doc["relay"] = 3;
    doc["state"] = false;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
    Serial.println("Web: Heater OFF");
  });
  
  // API: Get automation config
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("API: /api/config GET called");
    JsonDocument doc;
    
    doc["tempFanOn"] = TEMP_FAN_ON;
    doc["tempFanOff"] = TEMP_FAN_OFF;
    doc["tempHeaterOn"] = TEMP_HEATER_ON;
    doc["tempHeaterOff"] = TEMP_HEATER_OFF;
    doc["humMin"] = HUM_MIN;
    doc["humMax"] = HUM_MAX;
    doc["autoTempEnabled"] = autoTempEnabled;
    doc["autoHumEnabled"] = autoHumEnabled;
    doc["scheduleEnabled"] = scheduleEnabled;
    doc["cityName"] = cityName;
    
    // Add schedules
    JsonArray schedArray = doc["schedules"].to<JsonArray>();
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      if (schedules[i].relayNum == 0) continue; // Skip empty slots
      JsonObject sched = schedArray.add<JsonObject>();
      sched["id"] = i;
      sched["hour"] = schedules[i].hour;
      sched["minute"] = schedules[i].minute;
      sched["relayNum"] = schedules[i].relayNum;
      sched["turnOn"] = schedules[i].turnOn;
      sched["enabled"] = schedules[i].enabled;
      sched["days"] = schedules[i].days;
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // API: Save automation config
  server.on("/api/config", HTTP_POST, 
    [](AsyncWebServerRequest *request){}, 
    NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      Serial.println("API: /api/config POST called");
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (char*)data);
      
      if (error) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      // Update settings (use .as<>() to get values)
      if (!doc["tempFanOn"].isNull()) TEMP_FAN_ON = doc["tempFanOn"].as<float>();
      if (!doc["tempFanOff"].isNull()) TEMP_FAN_OFF = doc["tempFanOff"].as<float>();
      if (!doc["tempHeaterOn"].isNull()) TEMP_HEATER_ON = doc["tempHeaterOn"].as<float>();
      if (!doc["tempHeaterOff"].isNull()) TEMP_HEATER_OFF = doc["tempHeaterOff"].as<float>();
      if (!doc["humMin"].isNull()) HUM_MIN = doc["humMin"].as<float>();
      if (!doc["humMax"].isNull()) HUM_MAX = doc["humMax"].as<float>();
      if (!doc["autoTempEnabled"].isNull()) autoTempEnabled = doc["autoTempEnabled"].as<bool>();
      if (!doc["autoHumEnabled"].isNull()) autoHumEnabled = doc["autoHumEnabled"].as<bool>();
      if (!doc["scheduleEnabled"].isNull()) scheduleEnabled = doc["scheduleEnabled"].as<bool>();
      
      if (!doc["cityName"].isNull()) {
        cityName = doc["cityName"].as<String>();
        // Fetch new weather data for the new city
        if (wifiConnected) {
          fetchWeatherData();
        }
      }
      
      // Update schedules if provided
      JsonArray schedArray = doc["schedules"].as<JsonArray>();
      if (!schedArray.isNull()) {
        // Reset all schedules before loading new ones
        for (int i = 0; i < MAX_SCHEDULES; i++) {
          schedules[i] = {0, 0, 0, false, false, 0x7F};
        }
        int idx = 0;
        for (JsonObject sched : schedArray) {
          if (idx >= MAX_SCHEDULES) break;
          if (!sched["hour"].isNull()) schedules[idx].hour = sched["hour"].as<int>();
          if (!sched["minute"].isNull()) schedules[idx].minute = sched["minute"].as<int>();
          if (!sched["relayNum"].isNull()) schedules[idx].relayNum = sched["relayNum"].as<int>();
          if (!sched["turnOn"].isNull()) schedules[idx].turnOn = sched["turnOn"].as<bool>();
          if (!sched["enabled"].isNull()) schedules[idx].enabled = sched["enabled"].as<bool>();
          if (!sched["days"].isNull()) schedules[idx].days = sched["days"].as<int>();
          else schedules[idx].days = 0x7F;
          idx++;
        }
      }
      
      // Save to SPIFFS
      saveConfigToSPIFFS();
      
      request->send(200, "application/json", "{\"success\":true}");
      Serial.println("Config updated and saved");
  });
  
  // API: Get automation status
  server.on("/api/automation/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["autoTempEnabled"] = autoTempEnabled;
    doc["autoHumEnabled"] = autoHumEnabled;
    doc["scheduleEnabled"] = scheduleEnabled;
    doc["tempFanOn"] = TEMP_FAN_ON;
    doc["tempFanOff"] = TEMP_FAN_OFF;
    doc["currentTemp"] = currentTemperature;
    doc["currentHum"] = xymd03_humidity;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // ===== MQTT API Endpoints =====
  
  // API: Get MQTT configuration
  server.on("/api/mqtt/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("API: /api/mqtt/config GET called");
    JsonDocument doc;
    doc["enabled"] = mqtt_enabled;
    doc["server"] = mqtt_server;
    doc["port"] = mqtt_port;
    doc["username"] = mqtt_username;
    doc["password"] = mqtt_password;
    doc["topicPrefix"] = mqtt_topic_prefix;
    doc["connected"] = mqttConnected;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // API: Update MQTT configuration
  server.on("/api/mqtt/config", HTTP_POST, 
    [](AsyncWebServerRequest *request){}, 
    NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      Serial.println("API: /api/mqtt/config POST called");
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (char*)data);
      
      if (error) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      // Update MQTT settings
      if (!doc["enabled"].isNull()) mqtt_enabled = doc["enabled"].as<bool>();
      if (!doc["server"].isNull()) mqtt_server = doc["server"].as<String>();
      if (!doc["port"].isNull()) mqtt_port = doc["port"].as<int>();
      if (!doc["username"].isNull()) mqtt_username = doc["username"].as<String>();
      if (!doc["password"].isNull()) mqtt_password = doc["password"].as<String>();
      if (!doc["topicPrefix"].isNull()) mqtt_topic_prefix = doc["topicPrefix"].as<String>();
      
      // Save to SPIFFS
      saveConfigToSPIFFS();
      
      // Reinitialize MQTT with new settings
      if (mqtt_enabled && wifiConnected) {
        mqttClient.disconnect();
        setupMQTT();
        reconnectMQTT();
      } else if (!mqtt_enabled) {
        mqttClient.disconnect();
        mqttConnected = false;
      }
      
      request->send(200, "application/json", "{\"success\":true}");
      Serial.println("MQTT config updated and saved");
  });
  
  // API: Get MQTT Topics
  server.on("/api/mqtt/topics", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("API: /api/mqtt/topics GET called");
    String json = getMQTTTopicsJSON();
    request->send(200, "application/json", json);
  });
  
  // API: Get MQTT Status
  server.on("/api/mqtt/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["enabled"] = mqtt_enabled;
    doc["connected"] = mqttConnected;
    doc["server"] = mqtt_server;
    doc["port"] = mqtt_port;
    doc["topicPrefix"] = mqtt_topic_prefix;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // ===== TELEGRAM API ENDPOINTS =====
  
  // Get Telegram configuration
  server.on("/api/telegram/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["enabled"] = telegram_enabled;
    doc["botToken"] = telegram_bot_token;
    doc["chatId"] = telegram_chat_id;
    doc["alertTempHigh"] = telegram_alert_temp_high;
    doc["alertTempLow"] = telegram_alert_temp_low;
    doc["alertHumidityHigh"] = telegram_alert_humidity_high;
    doc["alertHumidityLow"] = telegram_alert_humidity_low;
    doc["alertWaterLevel"] = telegram_alert_water_level;
    doc["alertRelayStatus"] = telegram_alert_relay_status;
    doc["alertSystemStartup"] = telegram_alert_system_startup;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // Update Telegram configuration
  server.on("/api/telegram/config", HTTP_POST, 
    [](AsyncWebServerRequest *request) {}, 
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
      }
      
      // Update Telegram settings
      if (doc.containsKey("enabled")) telegram_enabled = doc["enabled"];
      if (doc.containsKey("botToken")) telegram_bot_token = doc["botToken"].as<String>();
      if (doc.containsKey("chatId")) telegram_chat_id = doc["chatId"].as<String>();
      if (doc.containsKey("alertTempHigh")) telegram_alert_temp_high = doc["alertTempHigh"];
      if (doc.containsKey("alertTempLow")) telegram_alert_temp_low = doc["alertTempLow"];
      if (doc.containsKey("alertHumidityHigh")) telegram_alert_humidity_high = doc["alertHumidityHigh"];
      if (doc.containsKey("alertHumidityLow")) telegram_alert_humidity_low = doc["alertHumidityLow"];
      if (doc.containsKey("alertWaterLevel")) telegram_alert_water_level = doc["alertWaterLevel"];
      if (doc.containsKey("alertRelayStatus")) telegram_alert_relay_status = doc["alertRelayStatus"];
      if (doc.containsKey("alertSystemStartup")) telegram_alert_system_startup = doc["alertSystemStartup"];
      
      // Save to SPIFFS
      saveConfigToSPIFFS();
      
      // Reinitialize Telegram
      setupTelegram();
      
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Telegram config updated\"}");
      Serial.println("Telegram configuration updated via API");
    });
  
  // Send test message
  server.on("/api/telegram/test", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!telegram_enabled) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"Telegram is disabled\"}");
      return;
    }
    
    sendTelegramSystemInfo();
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Test message sent\"}");
  });
  
  // Get Telegram status
  server.on("/api/telegram/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["enabled"] = telegram_enabled;
    doc["configured"] = (telegram_bot_token.length() > 0 && telegram_chat_id.length() > 0);
    doc["botToken"] = telegram_bot_token.length() > 0 ? "***" + telegram_bot_token.substring(telegram_bot_token.length() - 10) : "";
    doc["chatId"] = telegram_chat_id;
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // 404 handler
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });
  
  // Start server
  server.begin();
  Serial.println("Web Server started!");
  Serial.print("Open http://");
  Serial.print(WiFi.localIP());
  Serial.println("/ in your browser");
}

// Main update display function - route to correct page
void updateDisplay() {
  switch (currentPage) {
    case 0:
      updateDisplayPage0();
      break;
    case 1:
      updateDisplayPage1();
      break;
    case 2:
      updateDisplayPage2();
      break;
    default:
      currentPage = 0;
      updateDisplayPage0();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(10);

  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  }
  
  // Check for WiFi reset button (SW1) at startup
  checkWiFiResetButton();
  
  // Setup WiFi
  setupWiFi();
  
  // Show welcome with IP
  showWelcome();

  // Initialize DS18B20 Temperature Sensor
  tempSensor.begin();
  Serial.println("DS18B20 Temperature Sensor Initialized on GPIO14");
  Serial.print("Found ");
  Serial.print(tempSensor.getDeviceCount());
  Serial.println(" device(s).");
  
  // Initial temperature reading
  readTemperature();

  // Note: XY-MD03 uses Serial0 (RS485) - ต้องสลับ switch เป็นโหมด RS485
  // เมื่อใช้ XY-MD03 Serial Monitor จะไม่ทำงาน
  // Initialize XY-MD03 Temperature & Humidity Sensor (Modbus RTU)
  // Comment out these lines if not using XY-MD03 or using USB Serial Monitor
  Serial.end(); // ปิด Serial Monitor mode
  xymd03.begin(9600); // เริ่มต้น Modbus RTU mode
  delay(100);
  // Initial XY-MD03 reading
  readXYMD03();

  // Initialize switches
  sw1.begin();
  sw2.begin();
  sw3.begin();

  // Initialize relays
  relayFan.begin();
  relayPump.begin();
  relayHeater.begin();

  // Initialize isolated inputs
  iso1.begin();
  iso2.begin();

  // Register ISO callbacks
  iso1.onActive(onIso1Active);
  iso1.onInactive(onIso1Inactive);
  iso2.onActive(onIso2Active);
  iso2.onInactive(onIso2Inactive);

  // Register click callbacks
  sw1.onClick(onSw1Click);
  sw2.onClick(onSw2Click);
  sw3.onClick(onSw3Click);

  // Initial weather data fetch (if WiFi connected)
  if (wifiConnected) {
    Serial.println("Fetching initial weather data...");
    
    // Configure NTP for schedule system
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // GMT+7 for Thailand
    Serial.println("NTP configured for schedule system");
    
    // Start Web Server
    setupWebServer();
    
    // Load config from SPIFFS
    loadConfigFromSPIFFS();
    Serial.println("Automation system initialized");
    
    // Load runtime state from SPIFFS (restore relay states after power loss)
    loadStateFromSPIFFS();
    Serial.println("✓ Runtime state restored");
    
    // Setup MQTT
    setupMQTT();
    if (mqtt_enabled) {
      reconnectMQTT();
    }
    
    // Setup Telegram
    setupTelegram();
    
    // Send startup notification after a delay (to ensure everything is initialized)
    if (telegram_enabled && telegram_alert_system_startup) {
      delay(2000); // Wait 2 seconds
      String message = "🚀 *ระบบ Smart Farm เริ่มทำงาน*\n\n";
      message += "✅ ระบบพร้อมใช้งาน\n";
      message += "📍 IP: " + ipAddress + "\n";
      message += "🌐 WiFi Signal: " + String(WiFi.RSSI()) + " dBm";
      sendTelegramMessage(message);
    }
    
    fetchWeatherData();
  }
}

void loop() {
  // Poll switches (debounce and edge detection handled by class)
  sw1.update();
  sw2.update();
  sw3.update();

  // Poll isolated inputs
  iso1.update();
  iso2.update();

  // Note: Serial control is disabled when using XY-MD03 (Serial0 used for Modbus)
  // Serial control: press keys to toggle relays (only works in USB mode)
  // if (Serial.available()) {
  //   char c = (char)Serial.read();
  //   switch (c) {
  //     case 'f': case 'F': toggleFan(); break;
  //     case 'p': case 'P': togglePump(); break;
  //     case 'h': case 'H': toggleHeater(); break;
  //     case '1': toggleFan(); break;
  //     case '2': togglePump(); break;
  //     case '3': toggleHeater(); break;
  //     default: break;
  //   }
  // }

  delay(10);

  // ===== AUTOMATION LOGIC =====
  // Check automation every 5 seconds
  if (millis() - lastAutoCheck >= AUTO_CHECK_INTERVAL) {
    lastAutoCheck = millis();
    
    // Safety First: Always check water level
    autoWaterLevelControl();
    
    // Temperature control
    if (autoTempEnabled) {
      autoTemperatureControl();
    }
    
    // Humidity control
    if (autoHumEnabled) {
      autoHumidityControl();
    }
    
    // Schedule system
    if (scheduleEnabled) {
      checkSchedules();
    }
  }

  // Update XY-MD03 reading at interval
  if (millis() - lastXYMD03Update >= XYMD03_UPDATE_INTERVAL) {
    lastXYMD03Update = millis();
    readXYMD03();
  }

  // Update temperature reading at interval
  if (millis() - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
    lastTempUpdate = millis();
    readTemperature();
  }

  // Update weather data at interval (every 10 minutes)
  if (wifiConnected && (millis() - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL)) {
    lastWeatherUpdate = millis();
    Serial.println("Periodic weather update...");
    fetchWeatherData();
  }

  // Update display at interval
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
  
  // ===== MQTT HANDLING =====
  // Reconnect to MQTT if needed
  if (mqtt_enabled && wifiConnected) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    
    // Process MQTT messages
    if (mqttClient.connected()) {
      mqttClient.loop();
      
      // Publish sensor data periodically
      if (millis() - lastMqttPublish >= MQTT_PUBLISH_INTERVAL) {
        lastMqttPublish = millis();
        publishSensorData();
      }
    }
  }
  
  // ===== TELEGRAM HANDLING =====
  // Check and send Telegram alerts
  if (telegram_enabled && wifiConnected) {
    checkTelegramAlerts();
  }
  
  // ===== STATE PERSISTENCE =====
  // Auto-save state when changed (with debounce)
  checkAndSaveState();
}

