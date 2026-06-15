/**
 * AeroGuard Local IoT Air Quality Sensor Firmware
 * Compatible with ESP32 Dev Modules
 * 
 * Bypasses Blynk. Communicates over local Wi-Fi via non-blocking HTTP POST JSON webhooks.
 * Features:
 *   - Local hardware feedback loops (LEDs, Relay, Buzzer)
 *   - Local I2C SSD1306 OLED Status Display (128x64)
 *   - Non-blocking execution flow using millis() timers
 *   - Async Wi-Fi connection handler
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Wi-Fi Access Point Details
const char* ssid = "Prajwal";
const char* pass = "0987654321";

// ==========================================
// CRITICAL CONFIGURATION: Flask Server Endpoint
// ==========================================
// REPLACE "192.168.X.X" with your actual laptop local IPv4 address.
// Example: "http://192.168.1.100:5000/update"
const char* flaskServerUrl = "http://<YOUR_LAPTOP_IP_HERE>:5000/update";

// Hardware PIN Definitions
#define MQ135_PIN 35      // MQ-135 Gas Sensor (Analog ADC1)
#define LED_GREEN 12      // Pin 12 - Safe Status LED
#define LED_YELLOW 14     // Pin 14 - Warning Status LED
#define LED_RED 27        // Pin 27 - Critical Hazard LED
#define FAN_RELAY 26      // Pin 26 - Fan Control Relay
#define BUZZER_PIN 13     // Pin 13 - Alarm Piezo Buzzer

// OLED Configuration (I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1     // Reset pin # (or -1 if sharing Arduino reset pin)
#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Non-blocking Timer Intervals
unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 1000;  // Send data & refresh screen every 1s

unsigned long lastBuzzerPulseTime = 0;
const unsigned long buzzerPulseInterval = 300; // Warning beep pulse speed (ms)
bool buzzerPulseState = false;

// Global Sensor & State variables
int currentPPM = 0;
int fanState = 0;      // 0 = OFF, 1 = ON
int buzzerState = 0;   // 0 = OFF, 1 = ON
String airConditionText = "Initializing";

void setup() {
  Serial.begin(115200);

  // Initialize Pin Modes
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(FAN_RELAY, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Keep output pins LOW initially
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(FAN_RELAY, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize OLED Screen Immediately (completely non-blocking for Wi-Fi)
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 OLED allocation failed. Check wiring."));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("====================="));
    display.println(F("     AeroGuard       "));
    display.println(F("====================="));
    display.println(F("\nSystem Initializing..."));
    display.display();
  }

  // Connect to Wi-Fi (non-blocking begin)
  Serial.print("Connecting to Wi-Fi network: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Read MQ-135 Gas Sensor & Map to dynamic PPM ranges
  // ESP32 ADC resolution is 12-bit (0-4095). We map it to approximate 0-1500 PPM.
  int rawADC = analogRead(MQ135_PIN);
  currentPPM = map(rawADC, 0, 4095, 0, 1500);

  // 2. Local Hardware Control Loop Pin Logic
  if (currentPPM < 400) {
    // [SAFE ZONE]
    airConditionText = "SAFE";
    fanState = 0;
    buzzerState = 0;
    
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    digitalWrite(FAN_RELAY, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  } 
  else if (currentPPM >= 400 && currentPPM <= 800) {
    // [WARNING ZONE]
    airConditionText = "WARNING";
    fanState = 0;
    
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_RED, LOW);
    digitalWrite(FAN_RELAY, LOW);
    
    // Intermittent Pulsing Buzzer Alarm (Non-blocking toggle)
    if (currentMillis - lastBuzzerPulseTime >= buzzerPulseInterval) {
      lastBuzzerPulseTime = currentMillis;
      buzzerPulseState = !buzzerPulseState;
      digitalWrite(BUZZER_PIN, buzzerPulseState ? HIGH : LOW);
      buzzerState = buzzerPulseState ? 1 : 0;
    }
  } 
  else {
    // [CRITICAL ZONE]
    airConditionText = "CRITICAL";
    fanState = 1;
    buzzerState = 1;
    
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(FAN_RELAY, HIGH);
    digitalWrite(BUZZER_PIN, HIGH); // Continuous alarm
  }

  // 3. Telemetry Send & Display Update Loop (runs every 1 second)
  if (currentMillis - lastTelemetryTime >= telemetryInterval) {
    lastTelemetryTime = currentMillis;

    // A. Update Physical OLED display
    updateOLED();

    // B. Send JSON webhook update payload if Wi-Fi is connected
    if (WiFi.status() == WL_CONNECTED) {
      sendTelemetry();
    } else {
      Serial.println("Wi-Fi status: Disconnected. Reconnecting...");
      // Re-trigger connecting sequence if disconnected
      if (WiFi.status() == WL_DISCONNECTED) {
        WiFi.disconnect();
        WiFi.begin(ssid, pass);
      }
    }
  }
}

/**
 * Packs sensor metrics and states, transmits HTTP POST request to local Flask server.
 */
void sendTelemetry() {
  HTTPClient http;
  
  // Initialize connection to flask server
  http.begin(flaskServerUrl);
  http.addHeader("Content-Type", "application/json");

  // Create JSON Document (ArduinoJson 6/7 syntax)
  StaticJsonDocument<200> doc;
  doc["ppm"] = currentPPM;
  doc["fan"] = fanState;
  doc["buzzer"] = buzzerState;

  String requestBody;
  serializeJson(doc, requestBody);

  // Send the HTTP POST request
  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("Telemetry Sent. Response Code: ");
    Serial.println(httpResponseCode);
    Serial.println(response);
  } else {
    Serial.print("Error sending HTTP POST. Code: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

/**
 * Updates the SSD1306 physical OLED display with current data.
 */
void updateOLED() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected to Wi-Fi. IP: ");
    Serial.println(WiFi.localIP());
  }

  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("AeroGuard ");
  if (WiFi.status() == WL_CONNECTED) {
    display.println("[ONLINE]");
  } else {
    display.println("[OFFLINE]");
  }
  display.println("---------------------");

  // Gas Concentration Metric
  display.setCursor(0, 20);
  display.print("Gas Level: ");
  display.setTextSize(2);
  display.print(currentPPM);
  display.setTextSize(1);
  display.println(" PPM");

  // Air Safety Status
  display.setCursor(0, 42);
  display.print("Status   : ");
  display.setTextColor(SSD1306_WHITE);
  display.println(airConditionText);

  // Exhaust Fan State
  display.setCursor(0, 54);
  display.print("Exh. Fan : ");
  display.println(fanState == 1 ? "ON" : "OFF");

  display.display();
}
