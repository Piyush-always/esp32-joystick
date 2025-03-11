#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials - use the same as your receiver ESP32
const char* ssid = "test";
const char* password = "12345678";

// MQTT Broker settings - same as receiver
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_pub = "esp32controller/commands"; // Topic to publish to

// Digital Joystick 1 pins - controls LEFT/RIGHT/UP1/DOWN1
const int JS1_LEFT = 13;  // Joystick 1 LEFT direction
const int JS1_RIGHT = 12; // Joystick 1 RIGHT direction
const int JS1_UP = 14;    // Joystick 1 UP (UP1)
const int JS1_DOWN = 27;  // Joystick 1 DOWN (DOWN1)

// Digital Joystick 2 pins - controls LEFT/RIGHT/UP2/DOWN2
const int JS2_LEFT = 26;  // Joystick 2 LEFT direction
const int JS2_RIGHT = 25; // Joystick 2 RIGHT direction
const int JS2_UP = 33;    // Joystick 2 UP (UP2)
const int JS2_DOWN = 32;  // Joystick 2 DOWN (DOWN2)

// Direction constants (must match receiver)
const int DIR_LEFT = 0;
const int DIR_RIGHT = 1;
const int DIR_UP1 = 2;
const int DIR_DOWN1 = 3;
const int DIR_UP2 = 4;
const int DIR_DOWN2 = 5;

// State tracking
bool currentState[6] = {false, false, false, false, false, false}; // Track current state of each direction
unsigned long lastCommandTime[6] = {0, 0, 0, 0, 0, 0}; // Track last command time for each direction
const unsigned long COMMAND_INTERVAL = 200; // Minimum time between commands (ms)

// MQTT connection variables
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastReconnectAttempt = 0;
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000; // 10 seconds

void setup_wifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  // Wait up to 20 seconds for connection
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed, restarting...");
    ESP.restart();
  }
}

// Create command byte based on direction index and state
byte createCommandByte(int dirIndex, bool state) {
  byte commandByte = dirIndex & 0x07; // Use bottom 3 bits for direction
  if (state) {
    commandByte |= 0x80; // Set bit 7 for active state
  }
  return commandByte;
}

// Send MQTT command for specific direction
void sendCommand(int direction, bool state) {
  unsigned long currentMillis = millis();
  
  // Only send if state changed or sufficient time has passed
  if (state != currentState[direction] || 
      (state && currentMillis - lastCommandTime[direction] > COMMAND_INTERVAL)) {
    
    // Update tracking
    currentState[direction] = state;
    lastCommandTime[direction] = currentMillis;
    
    // Create and send command
    byte commandByte = createCommandByte(direction, state);
    if (client.connected()) {
      client.publish(mqtt_topic_pub, &commandByte, 1);
      Serial.printf("Command sent: Direction %d, State %d\n", direction, state);
    }
  }
}

boolean reconnect() {
  // Create a random client ID
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "ESP32JoystickCtrl-%x", random(0xffff));
  
  // Connect with a clean session
  if (client.connect(clientId)) {
    Serial.println("MQTT connected");
    return true;
  }
  return false;
}

void processJoysticks() {
  // For digital joysticks, we read the direct pin states
  // Note: Typical digital joysticks use active LOW (pressed = LOW)
  // Adjust the logic below if your joysticks work differently
  
  // Read Joystick 1 states (inverted because digital joysticks are typically active LOW)
  bool js1Left = !digitalRead(JS1_LEFT);
  bool js1Right = !digitalRead(JS1_RIGHT);
  bool js1Up = !digitalRead(JS1_UP);
  bool js1Down = !digitalRead(JS1_DOWN);
  
  // Read Joystick 2 states (inverted because digital joysticks are typically active LOW)
  bool js2Left = !digitalRead(JS2_LEFT);
  bool js2Right = !digitalRead(JS2_RIGHT);
  bool js2Up = !digitalRead(JS2_UP);
  bool js2Down = !digitalRead(JS2_DOWN);
  
  // Combine joystick inputs for LEFT/RIGHT (both joysticks can control these)
  bool leftActive = js1Left || js2Left;
  bool rightActive = js1Right || js2Right;
  
  // Handle LEFT/RIGHT (both joysticks can trigger these)
  sendCommand(DIR_LEFT, leftActive);
  sendCommand(DIR_RIGHT, rightActive);
  
  // Handle UP1/DOWN1 (Joystick 1 only)
  sendCommand(DIR_UP1, js1Up);
  sendCommand(DIR_DOWN1, js1Down);
  
  // Handle UP2/DOWN2 (Joystick 2 only)
  sendCommand(DIR_UP2, js2Up);
  sendCommand(DIR_DOWN2, js2Down);
}

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  Serial.println("ESP32 Digital Joystick MQTT Controller");
  
  // Set joystick pins as inputs with pull-up resistors
  // This assumes joysticks use active LOW (connect to GND when pressed)
  pinMode(JS1_LEFT, INPUT_PULLUP);
  pinMode(JS1_RIGHT, INPUT_PULLUP);
  pinMode(JS1_UP, INPUT_PULLUP);
  pinMode(JS1_DOWN, INPUT_PULLUP);
  
  pinMode(JS2_LEFT, INPUT_PULLUP);
  pinMode(JS2_RIGHT, INPUT_PULLUP);
  pinMode(JS2_UP, INPUT_PULLUP);
  pinMode(JS2_DOWN, INPUT_PULLUP);
  
  // Initialize WiFi
  setup_wifi();
  
  // Set MQTT server
  client.setServer(mqtt_server, mqtt_port);
  
  // Set client options
  client.setKeepAlive(60);
  
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
    // Process MQTT messages
    client.loop();
    
    // Process digital joystick inputs
    processJoysticks();
    
    // Small delay to prevent flooding the MQTT broker
    delay(50);
  }
}
