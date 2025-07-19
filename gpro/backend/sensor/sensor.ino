#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>

const char* ssid = "OnePlus Nord CE4";
const char* pass = "jumbo8686";


char serverAddress[] = "ui-e3hy.onrender.com";
int port = 443;

WiFiSSLClient wifi;  
HttpClient client = HttpClient(wifi, serverAddress, port);

const int TX_NUM = 7;
const int txPins[TX_NUM] = {2, 3, 4, 5, 6, 7, 8};
const int RX_NUM = 7;
const int rx_s0 = 9;
const int rx_s1 = 10;
const int rx_s2 = 11;
const int analogPin = A0;

const int FILTER_STRENGTH = 5;
int rx_history[RX_NUM][FILTER_STRENGTH] = {0};

int lowPassFilter(int rxChannel, int newReading) {
  for (int i = FILTER_STRENGTH - 1; i > 0; i--) {
    rx_history[rxChannel][i] = rx_history[rxChannel][i - 1];
  }
  rx_history[rxChannel][0] = newReading;
  int sum = 0;
  for (int i = 0; i < FILTER_STRENGTH; i++) {
    sum += rx_history[rxChannel][i];
  }
  return sum / FILTER_STRENGTH;
}

void selectRX(int rx) {
  digitalWrite(rx_s0, rx & 0x01);
  digitalWrite(rx_s1, (rx >> 1) & 0x01);
  digitalWrite(rx_s2, (rx >> 2) & 0x01);
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < TX_NUM; i++) {
    pinMode(txPins[i], OUTPUT);
    digitalWrite(txPins[i], LOW);
  }
  pinMode(rx_s0, OUTPUT);
  pinMode(rx_s1, OUTPUT);
  pinMode(rx_s2, OUTPUT);

  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  static unsigned long lastSample = 0;
  if (millis() - lastSample >= 500) {
    lastSample = millis();

    for (int tx = 0; tx < TX_NUM; tx++) {
      digitalWrite(txPins[tx], HIGH);
      delayMicroseconds(10);

      int rx_values[RX_NUM];
      for (int rx = 0; rx < RX_NUM; rx++) {
        selectRX(rx);
        delayMicroseconds(10);
        int val = analogRead(analogPin);
        rx_values[rx] = lowPassFilter(rx, val);
      }
      digitalWrite(txPins[tx], LOW);

      String json = "{\"time\":" + String(millis()) + ",\"tx\":" + String(tx) + ",\"rx\":[";
      for (int i = 0; i < RX_NUM; i++) {
        json += String(rx_values[i]);
        if (i < RX_NUM - 1) json += ",";
      }
      json += "]}";

      Serial.print("Sending data: ");
      Serial.println(json);

      client.beginRequest();
      client.post("/api/post"); // Make sure your Flask route is /api/post
      client.sendHeader("Content-Type", "application/json");
      client.sendHeader("Content-Length", json.length());
      client.beginBody();
      client.print(json);
      client.endRequest();

      int statusCode = client.responseStatusCode();
      String response = client.responseBody();
      Serial.print("TX: ");
      Serial.print(tx);
      Serial.print(" Status: ");
      Serial.print(statusCode);
      Serial.print(" Response: ");
      Serial.println(response);
    }
  }
}
