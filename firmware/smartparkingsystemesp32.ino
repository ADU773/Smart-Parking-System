#include "secrets.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

// Create secure WiFi client and MQTT client
WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

// Function to connect Wi-Fi
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// Function to sync time using NTP
void syncTime() {
  configTime(19800, 0, "pool.ntp.org");
  Serial.print("Waiting for NTP time sync");

  time_t now = time(nullptr);
  while (now < 1672531200) { // Wait until time syncs
    Serial.print(".");
    now = time(nullptr);
    delay(500);
  }

  Serial.println("\nTime synced!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

// Function to connect to AWS IoT Core
void connectMQTT() {
  secureClient.setCACert(rootCA);
  secureClient.setCertificate(clientCRT);
  secureClient.setPrivateKey(privateKey);

  mqttClient.setServer(mqttEndpoint, 8883);

  Serial.print("Connecting to AWS IoT...");
  while (!mqttClient.connected()) {
    if (mqttClient.connect("esp32-parking")) {
      Serial.println(" Connected to AWS IoT!");
    } else {
      Serial.print(".");
      Serial.print(" MQTT state: ");
      Serial.println(mqttClient.state());
      delay(1000);
    }
  }
}

// Pins for 4 ultrasonic sensors
const int trigPins[4] = {2, 22, 5, 21};
const int echoPins[4] = {15, 23, 18, 19};
const char* slotIDs[4] = {"B2", "B10", "A4", "A2"};
const int numSensors = sizeof(trigPins) / sizeof(trigPins[0]);

const long distanceThresholdCM = 10;
bool lastOccupied[4] = {false};

// Measure distance in cm
long getDistanceCM(int index) {
  digitalWrite(trigPins[index], LOW);
  delayMicroseconds(2);
  digitalWrite(trigPins[index], HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPins[index], LOW);

  long duration = pulseIn(echoPins[index], HIGH, 30000); // timeout 30ms
  long distance = duration * 0.034 / 2;
  return distance;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Connecting to Wi-Fi...");
  connectWiFi();
  syncTime();
  connectMQTT();

  // Setup sensor pins
  for (int i = 0; i < numSensors; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
  }

  Serial.println("Setup complete.");
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }

  const char* topic = "parking/slots";

  for (int i = 0; i < numSensors; i++) {
    long distance = getDistanceCM(i);
    bool currentOccupied = (distance <= distanceThresholdCM);

    // Print distance in Serial Monitor
    Serial.print("Sensor ");
    Serial.print(slotIDs[i]);
    Serial.print(": ");
    Serial.print(distance);
    Serial.println(" cm");

    // Publish MQTT only if occupancy changes
    if (currentOccupied != lastOccupied[i]) {
      String payload = "{\"slotid\":\"" + String(slotIDs[i]) + "\",\"occupied\":" + (currentOccupied ? "true" : "false") + "}";
      Serial.println("MQTT Payload: " + payload);

      if (mqttClient.publish(topic, payload.c_str())) {
        Serial.println("Message published to AWS IoT!");
      } else {
        Serial.println("Publish failed!");
      }

      lastOccupied[i] = currentOccupied;
    }
  }

  mqttClient.loop();
  Serial.println("----------------------");
  delay(500);
}
