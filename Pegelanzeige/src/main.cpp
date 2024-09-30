#include <WiFi.h>
#include <WiFiManager.h>  // Für die Konfiguration über einen Access Point
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include "esp_sleep.h"
#include <Preferences.h>  // Für das Speichern von Konfigurationsdaten

// Display-Konfiguration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_SDA      21
#define OLED_SCL      22

// Adafruit_SSD1306 Display-Objekt
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LED-Konfiguration für WS2812B
#define LED_PIN     2
#define NUM_LEDS    22
CRGB leds[NUM_LEDS];

// Helligkeit der LEDs (Wert zwischen 0 und 255)
#define BRIGHTNESS  50

// PIR-Sensor auf Pin D4
#define PIR_PIN     4

// Preferences-Objekt zum Speichern der Konfigurationsdaten
Preferences preferences;

// Standardwerte für die Konfiguration
#define DEFAULT_CITY "Goslar"
#define DEFAULT_COUNTRY_CODE "DE"
#define DEFAULT_API_KEY "01ec2551699c848e1fe0e0118d9b9ada"

// Variablen für Orts- und API-Daten
String city = DEFAULT_CITY;
String countryCode = DEFAULT_COUNTRY_CODE;
String openWeatherMapApiKey = DEFAULT_API_KEY;

unsigned long lastTime = 0;
unsigned long timerDelay = 10000;
unsigned long motionDetectedTime = 0;  // Zeitstempel für Bewegung

// WiFiManager-Objekt erstellen
WiFiManager wm;

// WiFiManager-Parameter erstellen
WiFiManagerParameter custom_city("city", "Stadt", city.c_str(), 32);
WiFiManagerParameter custom_country("country", "Länder-Code", countryCode.c_str(), 4);
WiFiManagerParameter custom_apiKey("apikey", "API-Schlüssel", openWeatherMapApiKey.c_str(), 64);

// Funktion für HTTP GET Request
String httpGETRequest(const char* serverName);
void parseWeatherData(const String& jsonBuffer);
void displayWeatherData(float temperature, int pressure, int humidity, float windSpeed, String windDir, int windDeg);
void updateLEDs(float temperature);
String getWindDirection(int degrees);
void drawCompass(int windDeg);
void enterLowPowerMode();
void wakeUp();
void checkStandby();

// Callback-Funktion, wenn AP gestartet wird
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("AP-Modus gestartet. Konfiguriere das Gerät über das Webinterface.");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("AP-Modus gestartet");
  display.display();
}

void setup() {
  Serial.begin(115200);

  // PIR-Sensor als Eingang setzen
  pinMode(PIR_PIN, INPUT);

  // Display initialisieren
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED-Display konnte nicht initialisiert werden"));
    for (;;);
  }

  // Display Einschalten und Begrüßung
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Hallo :)");
  display.display();
  delay(2000);
  display.clearDisplay();

  // LEDs initialisieren
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // Preferences initialisieren
  preferences.begin("config", false);

  // Gespeicherte Werte laden, falls vorhanden
  city = preferences.getString("city", DEFAULT_CITY);
  countryCode = preferences.getString("country", DEFAULT_COUNTRY_CODE);
  openWeatherMapApiKey = preferences.getString("apikey", DEFAULT_API_KEY);

  // WiFiManager konfigurieren
  wm.setAPCallback(configModeCallback);  // Callback-Funktion, wenn AP gestartet wird

  // WiFiManager-Parameter hinzufügen
  wm.addParameter(&custom_city);
  wm.addParameter(&custom_country);
  wm.addParameter(&custom_apiKey);

  // AP oder Verbindung zum gespeicherten WLAN herstellen
  if (!wm.autoConnect("ESP32_AP")) {
    Serial.println("WLAN-Verbindung fehlgeschlagen. Neustart...");
    ESP.restart();
  }

  Serial.println("WLAN verbunden!");

  // Nach erfolgreicher Verbindung gespeicherte Parameter aktualisieren
  city = custom_city.getValue();
  countryCode = custom_country.getValue();
  openWeatherMapApiKey = custom_apiKey.getValue();

  // Gespeicherte Werte aktualisieren
  preferences.putString("city", city);
  preferences.putString("country", countryCode);
  preferences.putString("apikey", openWeatherMapApiKey);

  // Display WLAN-Verbindung anzeigen
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WLAN verbunden");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
  display.clearDisplay();
}

void loop() {
  if (digitalRead(PIR_PIN) == HIGH) {
    // Wenn Bewegung erkannt wird, das System "aufwecken"
    motionDetectedTime = millis();  // Zeitstempel der Bewegung speichern
    wakeUp();

    if ((millis() - lastTime) > timerDelay) {
      if (WiFi.status() == WL_CONNECTED) {
        String serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + openWeatherMapApiKey + "&units=metric";
        String jsonResponse = httpGETRequest(serverPath.c_str());

        if (!jsonResponse.isEmpty()) {
          parseWeatherData(jsonResponse);
        }
      } else {
        Serial.println("WLAN getrennt");
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.println("WLAN getrennt");
        display.display();
      }
      lastTime = millis();
    }
  }

  checkStandby();  // Überprüfen, ob 5 Minuten vergangen sind
}

// Funktion für HTTP GET Request
String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverName);
  int httpResponseCode = http.GET();

  String payload = "{}";

  if (httpResponseCode > 0) {
    Serial.print("HTTP-Antwortcode: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  } else {
    Serial.print("Fehlercode: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return payload;
}

// Funktion zum Parsen der Wetterdaten
void parseWeatherData(const String& jsonBuffer) {
  // Verwende JsonDocument statt StaticJsonDocument
  DynamicJsonDocument doc(1024); // Dynamische Größe verwenden

  DeserializationError error = deserializeJson(doc, jsonBuffer);

  if (error) {
    Serial.print(F("JSON-Deserialisierungsfehler: "));
    Serial.println(error.f_str());
    return;
  }

  float temperature = doc["main"]["temp"];
  int pressure = doc["main"]["pressure"];
  int humidity = doc["main"]["humidity"];
  float windSpeed = doc["wind"]["speed"].as<float>() * 3.6;  // Umrechnung von m/s in km/h
  int windDegrees = doc["wind"]["deg"];

  String windDirection = getWindDirection(windDegrees);

  displayWeatherData(temperature, pressure, humidity, windSpeed, windDirection, windDegrees);
  updateLEDs(temperature);
}

// Funktion zur Anzeige der Wetterdaten
void displayWeatherData(float temperature, int pressure, int humidity, float windSpeed, String windDir, int windDeg) {
  display.clearDisplay();
  display.setTextSize(1);

  // Temperatur anzeigen
  display.setCursor(0, 0);
  display.println("Temp:");
  display.setTextSize(2);
  display.print(temperature);
  display.println(" C");
  display.display();
  delay(3000);

  // Luftdruck anzeigen
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Luftdruck:");
  display.setTextSize(2);
  display.print(pressure);
  display.println(" hPa");
  display.display();
  delay(3000);

  // Luftfeuchtigkeit anzeigen
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Feuchtigkeit:");
  display.setTextSize(2);
  display.print(humidity);
  display.println(" %");
  display.display();
  delay(3000);

  // Windgeschwindigkeit anzeigen
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Windgeschw.:");
  display.setTextSize(2);
  display.print(windSpeed);
  display.println(" km/h");
  display.display();
  delay(3000);

  // Windrichtung anzeigen
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Windrichtung:");
  display.display();
  drawCompass(windDeg);
  delay(200);
}

// Funktion zur Anzeige der LEDs je nach Temperatur
void updateLEDs(float temperature) {
  int numLedsToLight = map(temperature, -10, 40, 0, NUM_LEDS);  // Mapping der Temperatur auf die Anzahl der LEDs

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < numLedsToLight) {
      // Setze die Farbe basierend auf der Temperatur
      if (temperature <= 0) {
        leds[i] = CRGB::Blue;
      } else if (temperature > 0 && temperature <= 20) {
        leds[i] = CRGB::Green;
      } else if (temperature > 20 && temperature <= 30) {
        leds[i] = CRGB::Yellow;
      } else {
        leds[i] = CRGB::Red;
      }
    } else {
      leds[i] = CRGB::Black;  // LEDs aus
    }
  }
  FastLED.show();
}

// Funktion zur Umrechnung der Windrichtung in String
String getWindDirection(int degrees) {
  if (degrees >= 337.5 || degrees < 22.5) return "N";
  if (degrees >= 22.5 && degrees < 67.5) return "NE";
  if (degrees >= 67.5 && degrees < 112.5) return "E";
  if (degrees >= 112.5 && degrees < 157.5) return "SE";
  if (degrees >= 157.5 && degrees < 202.5) return "S";
  if (degrees >= 202.5 && degrees < 247.5) return "SW";
  if (degrees >= 247.5 && degrees < 292.5) return "W";
  if (degrees >= 292.5 && degrees < 337.5) return "NW";
  return "N/A";
}

// Funktion zur Darstellung eines Kompasspfeils für die Windrichtung
void drawCompass(int windDeg) {
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 2 + 8;  // Kreis leicht nach unten verschieben
  int radius = 20;

  display.drawCircle(centerX, centerY, radius, SSD1306_WHITE);

  // Berechne den Endpunkt des Pfeils basierend auf der Windrichtung
  float angleRad = windDeg * PI / 180.0;  // Umwandlung von Grad in Bogenmaß
  int arrowX = centerX + (int)(radius * sin(angleRad));
  int arrowY = centerY - (int)(radius * cos(angleRad));

  // Zeichne den Pfeil
  display.drawLine(centerX, centerY, arrowX, arrowY, SSD1306_WHITE);
  display.display();
}

// Low-Power-Modus aktivieren
void enterLowPowerMode() {
  // LEDs ausschalten
  FastLED.clear();
  FastLED.show();

  // Display ausschalten
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  // ESP32 in den Deep-Sleep-Modus versetzen
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1);  // PIR-Sensor weckt ESP32 bei Bewegung auf
  esp_deep_sleep_start();
}

// Funktion, um den ESP32 "aufzuwecken"
void wakeUp() {
  // LEDs und Display reaktivieren
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.show();

  display.ssd1306_command(SSD1306_DISPLAYON);
}

// Funktion zur Überprüfung der Standby-Bedingung
void checkStandby() {
  if (millis() - motionDetectedTime > 5 * 60 * 1000) {  // 5 Minuten
    enterLowPowerMode();
  }
}
