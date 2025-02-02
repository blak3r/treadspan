/*****************************************************************************
 * Treadmill Session Tracker on ESP32
 * 
 * Demonstrates:
 *  - Storing/reading treadmill sessions in EEPROM (12 bytes each).
 *  - Advertising a BLE service with two characteristics:
 *      * Data Characteristic (was Indicate, now using NOTIFY).
 *      * Confirmation Characteristic (Write) -> Receives 0x01 acknowledgments.
 *  - Sending sessions one-by-one after client acknowledgment.
 *  - Clearing stored sessions in EEPROM after all have been sent (and ack'd).
 *  - Handling button presses to simulate new sessions via interrupt.
 *  - Printing session data on Serial at startup.
 *  - Clearing sessions if GPIO12 is HIGH.
 *  - NEW: Using descriptor callbacks to detect subscription instead of a fixed delay.
 *****************************************************************************/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <EEPROM.h>
#include <time.h>  // For time() function (requires time sync for accurate local time)

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define EEPROM_SIZE        512
#define BUTTON_PIN         0     // Built-in flash button on many ESP32 dev boards
#define CLEAR_PIN          12    // If GPIO12 is HIGH, clear all sessions in EEPROM
#define MAX_SESSIONS       42    // Up to 42 sessions (12 bytes each) + 4 bytes for count
#define SESSION_SIZE_BYTES 12    // start(4) + stop(4) + steps(4)

static const char* BLE_SERVICE_UUID       = "12345678-1234-5678-1234-56789abcdef0";
static const char* BLE_DATA_CHAR_UUID     = "12345678-1234-5678-1234-56789abcdef1";
static const char* BLE_CONFIRM_CHAR_UUID  = "12345678-1234-5678-1234-56789abcdef2";

// ---------------------------------------------------------------------------
// Global Objects & Variables
// ---------------------------------------------------------------------------
BLEServer* pServer                = nullptr;
BLECharacteristic* dataCharacteristic    = nullptr;
BLECharacteristic* confirmCharacteristic = nullptr;

bool deviceConnected       = false;
bool oldDeviceConnected    = false;
bool subscribed           = false;    // If the central wrote 0x01 or 0x02 to the CCCD
bool haveNotifiedFirstPacket = false; // So we only do the initial send once

int currentSessionIndex    = 0; // Index of the next session to indicate

bool clearedSessions       = false; // Flag to ensure we only clear once if GPIO12 is HIGH

// ---------------------------------------------------------------------------
// EEPROM Layout & Session Struct
// ---------------------------------------------------------------------------
//  - [0..3]   : uint32_t sessionCount
//  - [4..end] : session data in blocks of 12 bytes each
//
// Each session block: 
//    Byte 0..3  : start time (Big-endian)
//    Byte 4..7  : stop time  (Big-endian)
//    Byte 8..11 : steps      (Big-endian)

struct TreadmillSession {
  uint32_t start;
  uint32_t stop;
  uint32_t steps;
};

// ---------------------------------------------------------------------------
// EEPROM Read/Write Helpers
// ---------------------------------------------------------------------------
uint32_t getSessionCountFromEEPROM() {
  uint32_t count = 0;
  count |= (EEPROM.read(0) << 24);
  count |= (EEPROM.read(1) << 16);
  count |= (EEPROM.read(2) << 8);
  count |= EEPROM.read(3);
  return count;
}

void setSessionCountInEEPROM(uint32_t count) {
  EEPROM.write(0, (count >> 24) & 0xFF);
  EEPROM.write(1, (count >> 16) & 0xFF);
  EEPROM.write(2, (count >> 8) & 0xFF);
  EEPROM.write(3, count & 0xFF);
}

TreadmillSession readSessionFromEEPROM(int index) {
  TreadmillSession session;
  int startAddress = 4 + (index * SESSION_SIZE_BYTES);

  session.start = ((uint32_t)EEPROM.read(startAddress)     << 24) |
                  ((uint32_t)EEPROM.read(startAddress + 1) << 16) |
                  ((uint32_t)EEPROM.read(startAddress + 2) <<  8) |
                   (uint32_t)EEPROM.read(startAddress + 3);

  session.stop  = ((uint32_t)EEPROM.read(startAddress + 4) << 24) |
                  ((uint32_t)EEPROM.read(startAddress + 5) << 16) |
                  ((uint32_t)EEPROM.read(startAddress + 6) <<  8) |
                   (uint32_t)EEPROM.read(startAddress + 7);

  session.steps = ((uint32_t)EEPROM.read(startAddress + 8)  << 24) |
                  ((uint32_t)EEPROM.read(startAddress + 9)  << 16) |
                  ((uint32_t)EEPROM.read(startAddress + 10) <<  8) |
                   (uint32_t)EEPROM.read(startAddress + 11);

  return session;
}

void writeSessionToEEPROM(int index, TreadmillSession session) {
  int startAddress = 4 + (index * SESSION_SIZE_BYTES);

  // Write start
  EEPROM.write(startAddress,     (session.start >> 24) & 0xFF);
  EEPROM.write(startAddress + 1, (session.start >> 16) & 0xFF);
  EEPROM.write(startAddress + 2, (session.start >>  8) & 0xFF);
  EEPROM.write(startAddress + 3, (session.start      ) & 0xFF);

  // Write stop
  EEPROM.write(startAddress + 4, (session.stop >> 24) & 0xFF);
  EEPROM.write(startAddress + 5, (session.stop >> 16) & 0xFF);
  EEPROM.write(startAddress + 6, (session.stop >>  8) & 0xFF);
  EEPROM.write(startAddress + 7, (session.stop      ) & 0xFF);

  // Write steps
  EEPROM.write(startAddress + 8,  (session.steps >> 24) & 0xFF);
  EEPROM.write(startAddress + 9,  (session.steps >> 16) & 0xFF);
  EEPROM.write(startAddress + 10, (session.steps >>  8) & 0xFF);
  EEPROM.write(startAddress + 11, (session.steps      ) & 0xFF);
}

// ---------------------------------------------------------------------------
// Print All Sessions
// ---------------------------------------------------------------------------
void printAllSessionsInEEPROM() {
  uint32_t count = getSessionCountFromEEPROM();
  uint32_t printCount = (count > MAX_SESSIONS) ? MAX_SESSIONS : count;

  Serial.printf("\n--- EEPROM Debug: Found %u session(s) stored ---\n", count);
  if (count > MAX_SESSIONS) {
    Serial.printf("Printing only the first %u session(s)...\n", MAX_SESSIONS);
  }

  for(uint32_t i = 0; i < printCount; i++) {
    TreadmillSession s = readSessionFromEEPROM(i);

    time_t startTime = (time_t)s.start;
    time_t stopTime  = (time_t)s.stop;

    char* startStr = ctime(&startTime);
    char* stopStr  = ctime(&stopTime);

    if (!startStr) startStr = (char*)"Unknown";
    if (!stopStr)  stopStr  = (char*)"Unknown";

    // Remove trailing newline for nicer printing
    if (startStr[strlen(startStr)-1] == '\n') startStr[strlen(startStr)-1] = '\0';
    if (stopStr[strlen(stopStr)-1]   == '\n') stopStr[strlen(stopStr)-1]   = '\0';

    Serial.printf("Session #%d:\n", i);
    Serial.printf("  Start: %s (Unix: %u)\n", startStr, s.start);
    Serial.printf("  Stop : %s (Unix: %u)\n", stopStr,  s.stop);
    Serial.printf("  Steps: %u\n", s.steps);
  }
  Serial.println("----------------------------------------------\n");
}

// ---------------------------------------------------------------------------
// Descriptor Callback (Detect CCCD Writes)
// ---------------------------------------------------------------------------
class MyDescriptorCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) override {
    // The CCCD value is typically 2 bytes: 
    //  - 0x01 0x00 for NOTIFY 
    //  - 0x02 0x00 for INDICATE 
    //  - 0x00 0x00 means unsubscribed
    uint8_t* cccdData = pDescriptor->getValue();
    if (cccdData == nullptr) return;

    if (cccdData[0] == 0x01) { // Notify
      subscribed = true;
      Serial.println("Client subscribed to NOTIFY!");
    }
    else if (cccdData[0] == 0x02) { // Indicate
      subscribed = true;
      Serial.println("Client subscribed to INDICATE!");
    }
    else {
      subscribed = false;
      Serial.printf("Client unsubscribed or unknown cccdData[0]=0x%02X\n", cccdData[0]);
    }
  }
};

// ---------------------------------------------------------------------------
// BLE Callbacks
// ---------------------------------------------------------------------------

// forward declaration
void indicateNextSession();

/**
 * Callback to handle client writes to the Confirmation Characteristic.
 * If the client writes 0x01, we send the next session data.
 */
class ConfirmCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    
    Serial.print("ConfirmCallback::onWrite called, got bytes: ");
    for (int i = 0; i < rxValue.length(); i++) {
      Serial.printf("%02X ", rxValue[i]);
    }
    Serial.println();

    if (rxValue.length() > 0) {
      // Expect 1 byte: 0x01 for acknowledgement
      if (rxValue[0] == 0x01) {
        // Client acknowledged. Send next session.
        indicateNextSession();
      }
    }
  }
};

/**
 * BLE Server event callbacks (connect / disconnect).
 */
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    haveNotifiedFirstPacket = false;
    Serial.println(">> Client connected!");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    subscribed = false; // no longer subscribed after disconnect
    Serial.println(">> Client disconnected!");
    delay(1000);
    pServer->startAdvertising();
    Serial.println(">> Advertising restarted...");
  }
};

// ---------------------------------------------------------------------------
// Indicate (Notify) Next Session
// ---------------------------------------------------------------------------
void indicateNextSession() {
  uint32_t totalSessions = getSessionCountFromEEPROM();
  
  // If we've reached or exceeded the total sessions, we're done
  if (currentSessionIndex >= (int)totalSessions) {
    Serial.println("All sessions have been indicated. Clearing sessions...");
    setSessionCountInEEPROM(0);
    EEPROM.commit();
    currentSessionIndex = 0;
    return;
  }

  // Read next session from EEPROM
  TreadmillSession s = readSessionFromEEPROM(currentSessionIndex);

  // Prepare 12-byte packet in big-endian
  uint8_t payload[12];
  payload[0]  = (s.start >> 24) & 0xFF;
  payload[1]  = (s.start >> 16) & 0xFF;
  payload[2]  = (s.start >>  8) & 0xFF;
  payload[3]  =  s.start        & 0xFF;
  
  payload[4]  = (s.stop >> 24) & 0xFF;
  payload[5]  = (s.stop >> 16) & 0xFF;
  payload[6]  = (s.stop >>  8) & 0xFF;
  payload[7]  =  s.stop        & 0xFF;
  
  payload[8]  = (s.steps >> 24) & 0xFF;
  payload[9]  = (s.steps >> 16) & 0xFF;
  payload[10] = (s.steps >>  8) & 0xFF;
  payload[11] =  s.steps        & 0xFF;

  // Send as Notify
  dataCharacteristic->setValue(payload, 12);
  dataCharacteristic->notify();
  
  Serial.printf(">> Notifying session %d\n", currentSessionIndex);
  currentSessionIndex++;
}

// ---------------------------------------------------------------------------
// Simulate a New Session (button-press handler)
// ---------------------------------------------------------------------------
volatile bool buttonPressed = false;

void IRAM_ATTR handleButtonInterrupt() {
  buttonPressed = true;
}

void simulateNewSession() {
  uint32_t count = getSessionCountFromEEPROM();
  if (count >= MAX_SESSIONS) {
    Serial.println("EEPROM is full. Cannot store more sessions.");
    return;
  }

  // Start/end = current time, steps = random(1..50)
  TreadmillSession newSession;
  uint32_t nowSec = (uint32_t)time(nullptr);
  newSession.start = nowSec;
  newSession.stop  = nowSec;
  newSession.steps = random(1, 51);

  writeSessionToEEPROM(count, newSession);
  setSessionCountInEEPROM(count + 1);
  EEPROM.commit();

  Serial.printf("Simulated new session stored at index=%u. Steps=%u\n", count, newSession.steps);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("=== Treadmill Session Tracker (ESP32) ===");

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CLEAR_PIN, INPUT);

  // Print current sessions for debugging
  printAllSessionsInEEPROM();

  // Clear sessions if GPIO12 is HIGH at startup
  if (digitalRead(CLEAR_PIN) == HIGH) {
    Serial.println("GPIO12 is HIGH. Clearing all sessions...");
    setSessionCountInEEPROM(0);
    EEPROM.commit();
    clearedSessions = true;
  }

  // Setup button interrupt
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

  // Initialize BLE
  BLEDevice::init("BetterSpan Treadmill");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE service
  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  // Create Data Characteristic (Notify)
  dataCharacteristic = pService->createCharacteristic(
      BLE_DATA_CHAR_UUID,
      BLECharacteristic::PROPERTY_NOTIFY
  );

  // Add CCCD descriptor (0x2902) and set our custom callback
  BLEDescriptor* cccd = new BLE2902(); 
  cccd->setCallbacks(new MyDescriptorCallbacks());
  dataCharacteristic->addDescriptor(cccd);

  // Create Confirmation Characteristic (Write)
  confirmCharacteristic = pService->createCharacteristic(
      BLE_CONFIRM_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
  );
  confirmCharacteristic->setCallbacks(new ConfirmCallback());

  // Start service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE Advertising started...");
}

void loop() {
  // Check for button press
  if (buttonPressed) {
    buttonPressed = false;
    simulateNewSession();
  }

  // Clear sessions if GPIO12 goes HIGH during runtime
  if(!clearedSessions && digitalRead(CLEAR_PIN) == HIGH) {
    Serial.println("GPIO12 is HIGH (runtime). Clearing all sessions...");
    setSessionCountInEEPROM(0);
    EEPROM.commit();
    clearedSessions = true;
  }

  // If we just connected (deviceConnected just turned true) or we've not yet sent the first packet
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("Device is connected. Waiting for subscription to CCCD...");
    // We'll wait for "subscribed" == true, then do the initial send
  }
  else if (!deviceConnected && oldDeviceConnected) {
    // Disconnected
    oldDeviceConnected = deviceConnected;
    Serial.println("Device disconnected, set subscribed=false");
  }

  // If the central is connected & subscribed but we haven't sent the first packet
  if (deviceConnected && subscribed && !haveNotifiedFirstPacket) {
    haveNotifiedFirstPacket = true;
    currentSessionIndex = 0;
    Serial.println("Central subscribed. Sending first session...");
    indicateNextSession();
  }

  delay(20);
}
