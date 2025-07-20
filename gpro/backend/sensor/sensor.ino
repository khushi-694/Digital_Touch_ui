#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>

const char* ssid = "Khushi's F15";
const char* pass = "SAKSHAM1!";


char serverAddress[] = "digital-touch-ui.onrender.com";
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
  const unsigned long SEND_INTERVAL_MS = 200; // Target 0.2 second interval

  if (millis() - lastSample >= SEND_INTERVAL_MS) {
    lastSample = millis();

    // Data structure to hold all TX-RX sets for this sample
    // Example: {"time": T, "samples": [{"tx":0, "rx":[...]}, {"tx":1, "rx":[...]}, ...]}
    String overallJson = "{\"time\":" + String(millis()) + ",\"samples\":[";

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

      // Build JSON for this specific TX-RX set
      overallJson += "{\"tx\":" + String(tx) + ",\"rx\":[";
      for (int i = 0; i < RX_NUM; i++) {
        overallJson += String(rx_values[i]);
        if (i < RX_NUM - 1) overallJson += ",";
      }
      overallJson += "]}";

      if (tx < TX_NUM - 1) {
        overallJson += ","; // Add comma if not the last TX
      }
    }
    overallJson += "]}"; // Close the 'samples' array and the main JSON object

    Serial.print("Sending combined data: ");
    Serial.println(overallJson);

    // Send combined HTTP POST request
    client.beginRequest();
    client.post("/api/post");
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Content-Length", overallJson.length());
    client.beginBody();
    client.print(overallJson);
    client.endRequest();

    // Read and print server response for the combined request
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();
    Serial.print("Combined Request Status: ");
    Serial.print(statusCode);
    Serial.print(" Response: ");
    Serial.println(response);
  }
}
