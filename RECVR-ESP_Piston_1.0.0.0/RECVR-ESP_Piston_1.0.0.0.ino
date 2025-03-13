#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "test";
const char* password = "12345678";

char state_log[2][15] = {"...........OFF","ON"};

// MQTT Broker settings
const char* mqtt_server = "broker.emqx.io"; // Public broker
const int mqtt_port = 1883;
const char* mqtt_topic_sub = "esp32controller/surat_cmd"; // Topic to subscribe to surat

//SURAT: "esp32controller/surat_cmd",
//AHMEDABAD: "esp32controller/ahmedabad_cmd",
//DELHI: "esp32controller/delhi_cmd"

// GPIO pins for controlling relays for the 3 pistons (6 directions)
const int PIN_LEFT = 25;
const int PIN_RIGHT = 26;
const int PIN_UP1 = 27;
const int PIN_DOWN1 = 14;
const int PIN_UP2 = 12;
const int PIN_DOWN2 = 13;
const int PIN_POWER = 15; // New pin for power relay

// Direction constants
const int DIR_LEFT = 0;
const int DIR_RIGHT = 1;
const int DIR_UP1 = 2;
const int DIR_DOWN1 = 3;
const int DIR_UP2 = 4;
const int DIR_DOWN2 = 5;
const int DIR_POWER = 6; // New direction for power control

// Power command values
const byte POWER_ON = 0x86;  // 10000110
const byte POWER_OFF = 0x06; // 00000110

// Direct mapping array for fast lookup
const int RELAY_PINS[] = {PIN_LEFT, PIN_RIGHT, PIN_UP1, PIN_DOWN1, PIN_UP2, PIN_DOWN2, PIN_POWER};
const int DIRECTION_COUNT = 7; // Now including power

// Command structure:
// Single byte format:
// Bits 0-2: Direction index (0-5)
// Bit 7: State (0 for LOW/off, 1 for HIGH/on)
// Example: 0x81 = Direction 1 (RIGHT) + HIGH state (0x80)
// Special case: Power commands use 0x86 for ON and 0x06 for OFF

// MQTT connection variables
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastReconnectAttempt = 0;
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000; // 10 seconds

void setup_wifi() {
  WiFi.begin(ssid, password);
  
  // Wait up to 20 seconds for connection
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
  }
}

// Fast command processor for single-byte command format
void processCommand(byte commandByte) {
  // Check for power commands first
  if (commandByte == POWER_ON) {
    digitalWrite(PIN_POWER, LOW); // Power ON (active LOW)
    Serial.println("Power ON command received");
    return;
  } 
  else if (commandByte == POWER_OFF) {
    digitalWrite(PIN_POWER, HIGH); // Power OFF (inactive HIGH)
    Serial.println("Power OFF command received");
    return;
  }

  // For other commands, extract direction and state
  int dirIndex = commandByte & 0x07;  // Use bottom 3 bits for direction (0-7)
  bool activateRelay = (commandByte & 0x80) != 0;  // Bit 7 for state
  Serial.printf("   [%d] ,   %s \n", dirIndex, state_log[activateRelay]);  
  
  // Validate direction
  if (dirIndex < DIRECTION_COUNT - 1) { // -1 because power is handled separately
    // Check if we need to handle opposing directions
    if (dirIndex == 0 || dirIndex == 1) {  // LEFT/RIGHT pair
      // Latest command wins
      digitalWrite(RELAY_PINS[dirIndex], activateRelay ? LOW : HIGH);
    }
    else if (dirIndex == 2 || dirIndex == 3) {  // UP1/DOWN1 pair
      // Latest command wins
      digitalWrite(RELAY_PINS[dirIndex], activateRelay ? LOW : HIGH);
    }
    else if (dirIndex == 4 || dirIndex == 5) {  // UP2/DOWN2 pair
      // Latest command wins
      digitalWrite(RELAY_PINS[dirIndex], activateRelay ? LOW : HIGH);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // For single-byte command, we expect length = 1
  if (length == 1) {
    processCommand(payload[0]);
  }
}

boolean reconnect() {
  // Create a random client ID
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "ESP32Piston-%x", random(0xffff));
  
  // Connect with a clean session
  if (client.connect(clientId)) {
    // Subscribe to command topic
    client.subscribe(mqtt_topic_sub);
    Serial.println("MQTT connected");
    return true;
  }
  return false;
}

void setup() {
  // Initialize serial for minimal logging
  Serial.begin(115200);
  
  // Initialize GPIO pins for relays
  pinMode(PIN_LEFT, OUTPUT);
  pinMode(PIN_RIGHT, OUTPUT);
  pinMode(PIN_UP1, OUTPUT);
  pinMode(PIN_DOWN1, OUTPUT);
  pinMode(PIN_UP2, OUTPUT);
  pinMode(PIN_DOWN2, OUTPUT);
  pinMode(PIN_POWER, OUTPUT);
  
  // Set all relays to inactive initially
  // INVERTED LOGIC: Set HIGH to deactivate relays
  for (int i = 0; i < DIRECTION_COUNT; i++) {
    digitalWrite(RELAY_PINS[i], HIGH);
  }
  
  // Initialize WiFi
  setup_wifi();
  
  // Set MQTT server and callback
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  // Set client options for better performance
  client.setKeepAlive(60);
  client.setSocketTimeout(2); // Reduced for faster response
  
  // Initialize random seed
  randomSeed(micros());
  
  lastReconnectAttempt = 0;
  lastWifiCheck = 0;
  
  Serial.println("System ready");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Check WiFi less frequently
  if (currentMillis - lastWifiCheck > WIFI_CHECK_INTERVAL) {
    lastWifiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      setup_wifi();
    }
  }
  
  // Check MQTT connection
  if (!client.connected()) {
    if (currentMillis - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = currentMillis;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // Process MQTT messages - this is critical for latency
    client.loop();
  }
}
