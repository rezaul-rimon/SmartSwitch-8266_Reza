#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <RCSwitch.h>
#include <map>
#include <WiFiManager.h> 
#include <EEPROM.h>
#define EEPROM_SIZE 10  // Allocate enough bytes to store switch states

// ✅ WiFi Credentials
// const char* ssid = "DMA-IR-Bluster";
// const char* password = "dmabd987";

#define RESET_PIN 0  // GPIO 0 for WiFi reset
unsigned long buttonPressTime = 0;
bool resetFlag = false;

WiFiManager wm;

#define DEBUG_MODE true
#define DEBUG_PRINT(x)  if (DEBUG_MODE) { Serial.print(x); }
#define DEBUG_PRINTLN(x) if (DEBUG_MODE) { Serial.println(x); }

#define WIFI_ATTEMPT_COUNT 60
#define WIFI_ATTEMPT_DELAY 1000
#define WIFI_WAIT_COUNT 60
#define WIFI_WAIT_DELAY 1000
#define MAX_WIFI_ATTEMPTS 2
#define MQTT_ATTEMPT_COUNT 10
#define MQTT_ATTEMPT_DELAY 5000

int wifiAttemptCount = WIFI_ATTEMPT_COUNT;
int wifiWaitCount = WIFI_WAIT_COUNT;
int maxWifiAttempts = MAX_WIFI_ATTEMPTS;
int mqttAttemptCount = MQTT_ATTEMPT_COUNT;

// ✅ MQTT Configuration
const char* mqtt_server = "broker2.dma-bd.com";
const char* mqtt_user = "broker2";
const char* mqtt_password = "Secret!@#$1234";
const char* mqtt_pub_topic = "DMA/SmartSwitch/PUB";
const char* mqtt_sub_topic = "DMA/SmartSwitch/SUB";
const char* mqtt_hb_topic = "DMA/SmartSwitch/HB";

// ✅ Device ID
#define WORK_PACKAGE "1225"
#define GW_TYPE "10"
#define FIRMWARE_UPDATE_DATE "250212" 
#define DEVICE_SERIAL "0006"
#define DEVICE_ID WORK_PACKAGE GW_TYPE FIRMWARE_UPDATE_DATE DEVICE_SERIAL

#define HB_INTERVAL 5*60*1000

// ✅ Pin Definitions
#define RF433_RX_PIN 5  // GPIO5 (D1) - RF Receiver Data Pin
#define LED_PIN 2       // GPIO2 (D4) - LED Control
#define SW1_PIN 14       // GPIO4 (D2)
#define SW2_PIN 13      // GPIO13 (D7)
#define SW3_PIN 12      // GPIO14 (D5)
#define SW4_PIN 4      // GPIO15 (D8)

// ✅ RF433 & MQTT Setup
RCSwitch mySwitch = RCSwitch();
WiFiClient espClient;
PubSubClient client(espClient);

// ✅ Debounce Variables
unsigned long lastRFGlobalReceivedTime = 0;
std::map<unsigned long, unsigned long> lastRFReceivedTimeMap;

void resetWiFi() {
    if (digitalRead(RESET_PIN) == LOW) {  // Button pressed
        DEBUG_PRINTLN("Button Pressed... Waiting for 5 seconds");

        unsigned long pressStart = millis();
        while (millis() - pressStart < 5000) {  // Wait for 5 seconds
            if (digitalRead(RESET_PIN) == HIGH) {  
                DEBUG_PRINTLN("Button Released... Canceling Reset.");
                return;  // Exit if the button is released early
            }
            delay(100);
        }

        DEBUG_PRINTLN("Resetting WiFi...");
        digitalWrite(SW1_PIN, LOW);
        digitalWrite(SW2_PIN, LOW);
        digitalWrite(SW3_PIN, LOW);
        digitalWrite(SW4_PIN, LOW);
        wm.resetSettings();  // Clear saved WiFi credentials
        wm.autoConnect("DMA_Smart_Switch");
        ESP.restart();       // Restart ESP
    }
}


// Function to reconnect to WiFi
void reconnectWiFi() {
    int attempt = 0;
    DEBUG_PRINTLN("Connecting to WiFi...");
    // WiFi.begin(ssid, password);
    WiFi.begin();  // Use saved credentials
    while (WiFi.status() != WL_CONNECTED && attempt < WIFI_ATTEMPT_COUNT) {
        DEBUG_PRINT("Remaining WiFi Attempt: ");
        DEBUG_PRINTLN(WIFI_ATTEMPT_COUNT - attempt - 1);
        delay(WIFI_ATTEMPT_DELAY);
        attempt++;
        if (digitalRead(RESET_PIN) == LOW){
            resetWiFi();
            break;
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTLN("WiFi Connected!");
    } else {
        DEBUG_PRINTLN("WiFi connection failed, retrying...");

        for (int waitAttempt = 0; waitAttempt < WIFI_WAIT_COUNT; waitAttempt++) {
            delay(WIFI_WAIT_DELAY);
            
            if (digitalRead(RESET_PIN) == LOW){
                resetWiFi();
                break;
            }

            if (WiFi.status() == WL_CONNECTED) {
                DEBUG_PRINTLN("WiFi Connected during wait time!");
                return;
            }
        }

        maxWifiAttempts--;
        if (maxWifiAttempts <= 0) {
            DEBUG_PRINTLN("Max WiFi attempt cycles exceeded, restarting...");
            ESP.restart();
        }
    }
}


// Function to reconnect MQTT
void reconnectMQTT() {
    char clientId[24];
    snprintf(clientId, sizeof(clientId), "dma_ssw_%04X%04X%04X", random(0xffff), random(0xffff), random(0xffff));
    DEBUG_PRINTLN("Attempting MQTT connection...");
    int attempt = 0;
    while (attempt < MQTT_ATTEMPT_COUNT) {
        if (client.connect(clientId, mqtt_user, mqtt_password)) {
            DEBUG_PRINTLN("MQTT connected");
            DEBUG_PRINT("MQTT Client ID: ");
            DEBUG_PRINT(clientId);
            
            char topic[48];
            snprintf(topic, sizeof(topic), "%s/%s", mqtt_sub_topic, DEVICE_ID);
            client.subscribe(topic);

            // client.subscribe(mqtt_sub_topic);
            digitalWrite(LED_PIN, HIGH);
            if (digitalRead(RESET_PIN) == LOW){
                resetWiFi();
            }
            return;
        } else {
            DEBUG_PRINTLN("MQTT connection failed");
            DEBUG_PRINT("Remaining MQTT attempts: ");
            DEBUG_PRINTLN(MQTT_ATTEMPT_COUNT - attempt - 1);
            attempt++;
            delay(MQTT_ATTEMPT_DELAY);

            if (digitalRead(RESET_PIN) == LOW){
                resetWiFi();
            }
        }
    }

    DEBUG_PRINTLN("Max MQTT attempts exceeded, restarting...");
    ESP.restart();
}


// ✅ Handle Incoming MQTT Messages
void callback(char* topic, byte* payload, unsigned int length) {
    payload[length] = '\0';  // Null-terminate payload
    String message = String((char*)payload);
    DEBUG_PRINT("Received Message: ");
    DEBUG_PRINTLN(message);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    
    if (message == "sw1:0") {
        digitalWrite(SW1_PIN, LOW);
        DEBUG_PRINTLN("Switch-1: off");
        EEPROM.write(0, 0);  // Store in EEPROM
        EEPROM.commit();  // Save changes
        char data[32];
        snprintf(data, sizeof(data), "%s,sw1:0", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw1:1") {
        digitalWrite(SW1_PIN, HIGH);
        DEBUG_PRINTLN("Switch-1: on");
        EEPROM.write(0, 1);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw1:1", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw2:0") {
        digitalWrite(SW2_PIN, LOW);
        DEBUG_PRINTLN("Switch-2: off");
        EEPROM.write(1, 0);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw2:0", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw2:1") {
        digitalWrite(SW2_PIN, HIGH);
        DEBUG_PRINTLN("Switch-2: on");
        EEPROM.write(1, 1);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw2:1", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw3:0") {
        digitalWrite(SW3_PIN, LOW);
        DEBUG_PRINTLN("Switch-3: off");
        EEPROM.write(2, 0);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw3:0", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw3:1") {
        digitalWrite(SW3_PIN, HIGH);
        DEBUG_PRINTLN("Switch-3: on");
        EEPROM.write(2, 1);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw3:1", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw4:0") {
        digitalWrite(SW4_PIN, LOW);
        DEBUG_PRINTLN("Switch-4: off");
        EEPROM.write(3, 0);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw4:0", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 
    else if (message == "sw4:1") {
        digitalWrite(SW4_PIN, HIGH);
        DEBUG_PRINTLN("Switch-4: on");
        EEPROM.write(3, 1);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw4:1", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    }   
    // Turn Off all of Switches
    else if (message == "sw1234:0") {
        digitalWrite(SW1_PIN, LOW);
        digitalWrite(SW2_PIN, LOW);
        digitalWrite(SW3_PIN, LOW);
        digitalWrite(SW4_PIN, LOW);
        DEBUG_PRINTLN("Switch-All: off");
        EEPROM.write(0, 0);
        EEPROM.write(1, 0);
        EEPROM.write(2, 0);
        EEPROM.write(3, 0);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw1234:0", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    }
    // Turn On all of Switches
    else if (message == "sw1234:1") {
        digitalWrite(SW1_PIN, HIGH);
        digitalWrite(SW2_PIN, HIGH);
        digitalWrite(SW3_PIN, HIGH);
        digitalWrite(SW4_PIN, HIGH);
        DEBUG_PRINTLN("Switch-All: on");
        EEPROM.write(0, 1);
        EEPROM.write(1, 1);
        EEPROM.write(2, 1);
        EEPROM.write(3, 1);
        EEPROM.commit();
        char data[32];
        snprintf(data, sizeof(data), "%s,sw1234:1", DEVICE_ID);
        client.publish(mqtt_pub_topic, data);
    } 

    if (message == "ping") {
        DEBUG_PRINTLN("Request for ping");
        char pingData[100]; // Increased size for additional info
        snprintf(pingData, sizeof(pingData), "%s,%s,%s,%d,%d",
        DEVICE_ID, WiFi.SSID().c_str(),
        WiFi.localIP().toString().c_str(), WiFi.RSSI(), HB_INTERVAL);
        client.publish(mqtt_pub_topic, pingData);
    
        DEBUG_PRINT("Sent ping response to MQTT: ");
        DEBUG_PRINTLN(pingData);
    }
    
}

// ✅ Setup Function
void setup() {
    Serial.begin(74880);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    pinMode(SW1_PIN, OUTPUT);
    pinMode(SW2_PIN, OUTPUT);
    pinMode(SW3_PIN, OUTPUT);
    pinMode(SW4_PIN, OUTPUT);

    pinMode(RESET_PIN, INPUT_PULLUP);

    EEPROM.begin(EEPROM_SIZE);  // Initialize EEPROM

    // Restore switch states
    digitalWrite(SW1_PIN, EEPROM.read(0) ? HIGH : LOW);
    digitalWrite(SW2_PIN, EEPROM.read(1) ? HIGH : LOW);
    digitalWrite(SW3_PIN, EEPROM.read(2) ? HIGH : LOW);
    digitalWrite(SW4_PIN, EEPROM.read(3) ? HIGH : LOW);

    // WiFi.mode(WIFI_STA);
    // if (!wm.autoConnect("DMA_Device")) {  // Try to connect, else start AP
    //     Serial.println("Failed to connect, restarting...");
    //     ESP.restart();
    // }
    
    // reconnectWiFi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    
    mySwitch.enableReceive(RF433_RX_PIN);
    DEBUG_PRINTLN("RF-433MHz Initialized!");
}

// ✅ Loop Function
void loop() {
    
    if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) {  // Only reconnect MQTT if disconnected
            digitalWrite(LED_PIN, LOW);
            reconnectMQTT();
        }
        client.loop();  // Always run the loop to maintain the connection
    } else {
        digitalWrite(LED_PIN, LOW);
        reconnectWiFi();
        if (WiFi.status() == WL_CONNECTED) {  // Check again after reconnecting WiFi
            if (!client.connected()) {  // Only reconnect MQTT if disconnected
                reconnectMQTT();
            }
            client.loop();
        }
    }

    if (digitalRead(RESET_PIN) == LOW){
        resetWiFi();
    }

    unsigned long now = millis();
    if (mySwitch.available()) {
      unsigned long receivedCode = mySwitch.getReceivedValue();
      int bitLength = mySwitch.getReceivedBitlength(); // Get bit length of the received signal
      digitalWrite(LED_PIN, LOW);
      delay(50);
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      // **Ignore signals that do not match the expected bit length (e.g., < 24 bits)**
      if (bitLength < 24) {  
        DEBUG_PRINTLN(String("Ignored RF Signal: ") + String(receivedCode) + " (Bits: " + String(bitLength) + ")");
        mySwitch.resetAvailable();
        return;;
      }

      // **Short-Term Global Debounce (Ignore if received within 100ms)**
      if (now - lastRFGlobalReceivedTime < 100) {
        mySwitch.resetAvailable();
        return;
      }

      // **Per-Sensor Debounce (Ignore same sensor within 2 sec)**
      if (lastRFReceivedTimeMap.find(receivedCode) == lastRFReceivedTimeMap.end() || 
          (now - lastRFReceivedTimeMap[receivedCode] > 2000)) {  

        lastRFReceivedTimeMap[receivedCode] = now;  // Update per-sensor time
        lastRFGlobalReceivedTime = now;  // Update global debounce

        // **Debug Output**
        DEBUG_PRINTLN(String("Valid RF Received: ") + String(receivedCode) + " (Bits: " + String(bitLength) + ")");
        
        // **Send Data to MQTT**
        char data[50];
        snprintf(data, sizeof(data), "%s,%lu", DEVICE_ID, receivedCode);
        client.publish(mqtt_pub_topic, data);
        DEBUG_PRINTLN(String("Data Sent to MQTT: ") + String(data));
        digitalWrite(LED_PIN, LOW);
        delay(50);
        digitalWrite(LED_PIN, HIGH);
        delay(50);
        digitalWrite(LED_PIN, LOW);
        delay(50);
        digitalWrite(LED_PIN, HIGH);
      }

      mySwitch.resetAvailable();
    }

    delay(10);
}
