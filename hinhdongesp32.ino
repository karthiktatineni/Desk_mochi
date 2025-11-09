#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "all_frames.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUTTON_PIN 14
#define FRAME_DELAY 70
#define FRAME_WIDTH 128
#define FRAME_HEIGHT 64

Preferences preferences;
WebServer server(80);
String ssid = "";
String password = "";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800);

bool showTimeScreen = false;
unsigned long showTimeStart = 0;
bool connectedToWiFi = false;
int currentFrame = 0;

// 🌦️ Weather variables
String city = "Unknown";
String country = "";
float latitude = 0;
float longitude = 0;
float temperature = 0;
String condition = "";
unsigned long lastWeatherUpdate = 0;

// 🌍 OpenWeather API Key
const String apiKey = "YOUR_OPENWEATHER_API";  // Replace with your real key

// ------------------- HELPER FUNCTIONS -------------------
void showWelcomeMessage() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 15);
  display.println("Hi there");

  // Draw heart below the text ❤️
  
  static const unsigned char PROGMEM heartBitmap[] = {
    0b00001100, 0b00110000,
    0b00011110, 0b01111000,
    0b00111111, 0b11111100,
    0b01111111, 0b11111110,
    0b01111111, 0b11111110,
    0b01111111, 0b11111110,
    0b00111111, 0b11111100,
    0b00011111, 0b11111000,
    0b00001111, 0b11110000,
    0b00000111, 0b11100000,
    0b00000011, 0b11000000,
    0b00000001, 0b10000000,
    0b00000000, 0b00000000,
    0b00000000, 0b00000000
  };

  display.drawBitmap(50, 40, heartBitmap, 16, 9, SSD1306_WHITE);
  display.display();
  delay(1500);
}


void playAnimation() {
  display.clearDisplay();
  display.drawBitmap(0, 0, frames[currentFrame],
                     FRAME_WIDTH, FRAME_HEIGHT, SSD1306_WHITE);
  display.display();
  currentFrame++;
  if (currentFrame >= TOTAL_FRAMES) currentFrame = 0;
  delay(FRAME_DELAY);
}

// 🌐 Detect location using IP
bool getLocation() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin("http://ip-api.com/json/");
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.println("⚠️ JSON parse error (location)");
      return false;
    }

    city = doc["city"].as<String>();
    country = doc["countryCode"].as<String>();
    latitude = doc["lat"].as<float>();
    longitude = doc["lon"].as<float>();

    Serial.printf("📍 Location: %s, %s (%.2f, %.2f)\n",
                  city.c_str(), country.c_str(), latitude, longitude);
    http.end();
    return true;
  } else {
    Serial.println("⚠️ Failed to detect location");
    http.end();
    return false;
  }
}

// 🌦️ Fetch weather using OpenWeather API
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastWeatherUpdate < 1800000 && temperature != 0) return;
  if (!getLocation()) return;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(latitude, 2) +
               "&lon=" + String(longitude, 2) + "&appid=" + apiKey + "&units=metric";

  Serial.println("🌤️ Calling OpenWeather API...");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.println("⚠️ JSON parse failed (weather)");
      http.end();
      return;
    }

    if (doc.containsKey("main") && doc["main"]["temp"].is<float>()) {
      temperature = doc["main"]["temp"].as<float>();
      condition = doc["weather"][0]["main"].as<String>();
      Serial.printf("🌡️ %.1f°C | %s | %s\n", temperature, condition.c_str(), city.c_str());
      Serial.println("✅ Weather data updated");
    } else {
      Serial.println("⚠️ Temperature field missing");
      temperature = 0;
      condition = "Unknown";
    }

    lastWeatherUpdate = millis();
  } else {
    Serial.printf("⚠️ Weather fetch failed. Code: %d\n", code);
  }
  http.end();
}

void showTimeAndDate() {
  fetchWeather();  // Update weather if 30 min passed
  timeClient.update();

  String timeStr = timeClient.getFormattedTime();
  time_t rawTime = timeClient.getEpochTime();
  struct tm *ti = localtime(&rawTime);
  char dateBuff[20];
  strftime(dateBuff, sizeof(dateBuff), "%d %b %Y", ti);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 🕒 Time
  display.setTextSize(2);
  display.setCursor(15, 3);
  display.println(timeStr);

  // 📅 Date
  display.setTextSize(1);
  display.setCursor(28, 25);
  display.println(dateBuff);

  // 🌦️ Weather Info
  display.setCursor(0, 40);
  display.print(city);
  display.print(" | ");
  display.print(temperature, 1);
  display.print("C");

  display.setCursor(0, 52);
  display.print(condition);

  display.display();
}

// ---------- WiFi Setup Portal ----------
void handleRoot() {
  String html = "<html><body style='text-align:center; font-family:sans-serif;'>"
                "<h2>Mochi Wi-Fi Setup</h2>"
                "<form action='/save'>"
                "SSID:<br><input name='ssid'><br>"
                "Password:<br><input name='pass' type='password'><br><br>"
                "<input type='submit' value='Save'>"
                "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  ssid = server.arg("ssid");
  password = server.arg("pass");

  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  preferences.end();

  server.send(200, "text/html", "<html><body><h3>Credentials saved! Restarting...</h3></body></html>");
  delay(2000);
  ESP.restart();
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Mochi");
  Serial.println("📶 Access Point: Mochi");
  Serial.println("➡️ Open 192.168.4.1 in browser");

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("Connect to WiFi:");
  display.println("SSID: Mochi");
  display.println("No password");
  display.println("Go to 192.168.4.1");
  display.display();

  while (true) {
    server.handleClient();
    delay(10);
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.setRotation(2);

  showWelcomeMessage();

  preferences.begin("wifi-creds", false);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  preferences.end();

  if (ssid == "" || password == "") {
    Serial.println("⚠️ No WiFi credentials found. Starting portal...");
    startConfigPortal();
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi!");
    connectedToWiFi = true;
    timeClient.begin();
    getLocation();
    fetchWeather();
  } else {
    Serial.println("\n❌ WiFi Failed. Starting portal...");
    startConfigPortal();
  }
}

// ---------- Loop ----------
void loop() {
  if (digitalRead(BUTTON_PIN) == LOW && !showTimeScreen) {
    showTimeScreen = true;
    showTimeStart = millis();
  }

  if (showTimeScreen && connectedToWiFi) {
    showTimeAndDate();
    if (millis() - showTimeStart > 10000) {
      showTimeScreen = false;
      display.clearDisplay();
      display.display();
    }
  } else {
    playAnimation();
  }
}
