#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>

// ----- WiFi Credentials -----
const char* ssid = "Khushi's F15";       // <-- your WiFi SSID
const char* pass = "SAKSHAM1!";           // <-- your WiFi Password

// ----- Server Details (Your Flask App) -----
char serverAddress[] = "digital-touch-ui.onrender.com"; // <-- Your Flask app URL
int port = 443; // Use 443 for HTTPS (Render.com uses HTTPS)

// Use WiFiSSLClient for HTTPS
WiFiSSLClient wifi;
HttpClient client = HttpClient(wifi, serverAddress, port);

int status = WL_IDLE_STATUS;

// ----- TX digital output pins (direct from Nano) -----
const int TX_NUM = 7;
const int txPins[TX_NUM] = {2, 3, 4, 5, 6, 7, 8};

// ----- RX multiplexer control pins -----
const int RX_NUM = 7;
const int rx_s0 = 9;
const int rx_s1 = 10;
const int rx_s2 = 11;
const int analogPin = A0;

// ----- Low-pass filter config -----
const int FILTER_STRENGTH = 15; // Increased filter strength as per your initial working code
int rx_history[RX_NUM][FILTER_STRENGTH] = {0};

// ----- Time tracking -----
unsigned long startTime = 0;
// Define the interval for a full scan cycle (all TX measurements)
const unsigned long SEND_INTERVAL_MS = 200; // Target 0.2 second interval for a complete data set
static unsigned long lastFullScanTime = 0; // Tracks when the last full scan was sent

// Low-pass filter function
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

// Select RX channel through MUX
void selectRX(int rx) {
  digitalWrite(rx_s0, rx & 0x01);
  digitalWrite(rx_s1, (rx >> 1) & 0x01);
  digitalWrite(rx_s2, (rx >> 2) & 0x01);
}

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial port to connect. Needed for native USB port only

  // Configure TX pins
  for (int i = 0; i < TX_NUM; i++) {
    pinMode(txPins[i], OUTPUT);
    digitalWrite(txPins[i], LOW);
  }

  // Configure RX multiplexer select pins
  pinMode(rx_s0, OUTPUT);
  pinMode(rx_s1, OUTPUT);
  pinMode(rx_s2, OUTPUT);

  // Connect to Wi-Fi
  Serial.print("Connecting to WiFi...");
  status = WiFi.begin(ssid, pass);
  int attempts = 0;
  while (status != WL_CONNECTED && attempts < 20) { // Try for max 20 seconds
    delay(1000);
    Serial.print(".");
    status = WiFi.status();
    attempts++;
  }

  if (status == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Failed to connect to WiFi. Check credentials and signal.");
    while(true) delay(1000); // Halt if WiFi fails
  }

  // Start time for timestamping
  startTime = millis();
  lastFullScanTime = millis(); // Initialize for the loop timer
}

void loop() {
  // Check WiFi connection status periodically
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    status = WiFi.begin(ssid, pass);
    int reconnectAttempts = 0;
    while (status != WL_CONNECTED && reconnectAttempts < 10) {
      delay(1000);
      Serial.print(".");
      status = WiFi.status();
      reconnectAttempts++;
    }
    if (status == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nFailed to reconnect. Halting data send.");
      return; // Stop sending data if cannot reconnect
    }
  }

  unsigned long currentTime = millis();
  // Only proceed with a full scan and send if the interval has passed
  if (currentTime - lastFullScanTime >= SEND_INTERVAL_MS) {
    lastFullScanTime = currentTime; // Reset timer for the next full scan

    unsigned long relativeTime = currentTime - startTime; // Timestamp for this full scan

    // Prepare a String to build the CSV-formatted data for this full scan
    // This will be sent as a single POST request.
    // Format: "relativeTime,TX0_idx,RX0_0,RX0_1,...,RX0_6,TX1_idx,RX1_0,...,TX6_idx,RX6_0,..."
    // Example: "2506,0,42,59,56,68,68,77,69,1,103,130,120,142,143,159,141,..."
    String fullScanData = String(relativeTime); // Start with overall timestamp
    
    // Collect data for each TX pin
    for (int tx = 0; tx < TX_NUM; tx++) {
      digitalWrite(txPins[tx], HIGH); // Enable TX line
      delayMicroseconds(10); // Allow signal to settle

      int rx_values[RX_NUM];
      for (int rx = 0; rx < RX_NUM; rx++) {
        selectRX(rx);
        delayMicroseconds(10); // Allow signal to settle
        int rawValue = analogRead(analogPin);
        rx_values[rx] = lowPassFilter(rx, rawValue);
      }
      digitalWrite(txPins[tx], LOW); // Disable TX line

      // Append TX and RX values for this TX cycle to the fullScanData string
      fullScanData += "," + String(tx); // Append TX index
      for (int rx = 0; rx < RX_NUM; rx++) {
        fullScanData += "," + String(rx_values[rx]); // Append each RX value
      }
      // No extra comma at the very end of the string, it's a single long CSV
    }

    // --- Send Data via WiFi ---
    Serial.print("📤 Sending combined CSV data: ");
    Serial.println(fullScanData);

    client.beginRequest();
    client.post("/sensor"); // Target Flask's /sensor endpoint
    client.sendHeader("Content-Type", "text/plain"); // Crucial: Matches Flask's expectation
    client.sendHeader("Content-Length", fullScanData.length());
    client.beginBody();
    client.print(fullScanData);
    client.endRequest(); // Sends the request

    // Read and print server response (Crucial for debugging!)
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();

    Serial.print("🔁 HTTP Status: ");
    Serial.println(statusCode);
    Serial.print("📥 Response: ");
    Serial.println(response);

    // Give the client library a moment to process the response
    // This is important for stability, especially with HTTPS.
    delay(50);
  }
}
