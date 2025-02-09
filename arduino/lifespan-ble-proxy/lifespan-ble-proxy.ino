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
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <EEPROM.h>
#include <sys/time.h>  // For settimeofday()
#include <time.h>  // For local time functions
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
LiquidCrystal_I2C lcd(0x27,20,4);


#define RETRO_MODE 1     // UNCOMMENT IF YOU WANT TO GET SESSIONS VIA SERIAL PORT.


// WiFi Credentials
const char* ssid = "Angela";
const char* password = "iloveblake";

// ARDUINO NANO PINOUT
#define BUTTON_PIN 2 // D2
#define CLEAR_PIN  3 // D3  - Clears all sessions in EEPROM

// EEPROM Configuration
#define EEPROM_SIZE        512
#define MAX_SESSIONS       42
#define SESSION_SIZE_BYTES 12


#ifdef RETRO_MODE
  // RETRO VERSION UART CONFIGURATION
  #define RX1PIN 20  // A0, GPIO1, D17
  #define TX1PIN 6  // NOT ACTUALLY NEEDED!
  #define RX2PIN 23  // A2, GPIO3, D19
  #define TX2PIN 8  // NOT ACTUALLY NEEDED
  int steps = 0;
  int shouldIUpdateCounter = 0;
  // Define the UART instances
  HardwareSerial uart1(1); // UART1 - RED = CONSOLE TX
  HardwareSerial uart2(2); // UART2 - ORANGE = TREADMILL TX
  // Command buffers
  String uart1Buffer = "";
  String uart2Buffer = "";
  int uart1RxCnt = 0;
  int uart2RxCnt = 0;
  #define CMD_BUF_SIZE 10
  byte uart1Buf[CMD_BUF_SIZE];
  byte uart2Buf[CMD_BUF_SIZE];
  int lastRequestType;
  #define LAST_REQUEST_IS_STEPS 1
  #define LAST_REQUEST_IS_SPEED 2
  #define LAST_REQUEST_IS_DISTANCE 3
  #define LAST_REQUEST_IS_TIME 4
  #define STEPS_STARTSWITH "1 3 0 15"
  #define SPEED_STARTSWITH "1 6 0 10"
  //---------------------------------------
#endif

struct TreadmillSession {
  uint32_t start;
  uint32_t stop;
  uint32_t steps;
};

// BLE UUIDs
static const char* BLE_SERVICE_UUID       = "12345678-1234-5678-1234-56789abcdef0";
static const char* BLE_DATA_CHAR_UUID     = "12345678-1234-5678-1234-56789abcdef1";
static const char* BLE_CONFIRM_CHAR_UUID  = "12345678-1234-5678-1234-56789abcdef2";

// BLE Variables
BLEServer* pServer = nullptr;
BLECharacteristic* dataCharacteristic = nullptr;
BLECharacteristic* confirmCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool subscribed = false;
bool haveNotifiedFirstPacket = false;

int currentSessionIndex = 0;
bool clearedSessions = false;
int loopCounter=0; // used for timing in main loop

int speedInt = 0;
float speedFloat = 0;
boolean isTreadmillActive = 0;

TreadmillSession currentSession;



// ---------------------------------------------------------------------------
// Wifi / NTP
// ---------------------------------------------------------------------------
void connectToWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
        toggleLedColor(LED_RED);
    }
    Serial.println("\nConnected to WiFi!");
}

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const int ntpPort = 123;
WiFiUDP ntpUDP;

#define NTP_PACKET_SIZE 48
byte ntpPacketBuffer[NTP_PACKET_SIZE];

// Timer variables
unsigned long lastNtpRequest = 0;
unsigned long ntpUpdateInterval = 3000;  // initially 3s, then 10 mins, had issues getting it to work first go raound. // 10 minutes

void sendNtpRequest() {
    Serial.println("Sending NTP request...");
    
    memset(ntpPacketBuffer, 0, NTP_PACKET_SIZE);
    ntpPacketBuffer[0] = 0b11100011;  // LI, Version, Mode
    ntpPacketBuffer[1] = 0;  // Stratum, or type of clock
    ntpPacketBuffer[2] = 6;  // Polling Interval
    ntpPacketBuffer[3] = 0xEC;  // Peer Clock Precision

    // Send packet to NTP server
    ntpUDP.beginPacket(ntpServer, ntpPort);
    ntpUDP.write(ntpPacketBuffer, NTP_PACKET_SIZE);
    ntpUDP.endPacket();

    lastNtpRequest = millis();  // Record request time
}

void checkNtpResponse() {
    int packetSize = ntpUDP.parsePacket();
    if (packetSize >= NTP_PACKET_SIZE) {
        Serial.println("NTP response received!");

        ntpUDP.read(ntpPacketBuffer, NTP_PACKET_SIZE);
        unsigned long highWord = word(ntpPacketBuffer[40], ntpPacketBuffer[41]);
        unsigned long lowWord = word(ntpPacketBuffer[42], ntpPacketBuffer[43]);
        time_t epochTime = (highWord << 16 | lowWord) - 2208988800UL;  // Convert to UNIX time

        // Set system time
        struct timeval tv;
        tv.tv_sec = epochTime;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        Serial.print("System time updated: ");
        Serial.println(getFormattedTime());

        ntpUpdateInterval = 60000; // 10 minutes
    }
}

void checkNtpUpdate() {
    if ((millis() - lastNtpRequest) >= ntpUpdateInterval) {
        sendNtpRequest();
    }
    checkNtpResponse();
}

String getFormattedTime() {
    time_t now = time(nullptr);
    if (now < 100000) {
        return "TBD, NTP sync";
    }

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);  // Convert to local time

    char buffer[30];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return String(buffer);
}



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



// EEPROM Read/Write Helpers
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
    EEPROM.commit();
    updateLcd();
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

    EEPROM.write(startAddress,     (session.start >> 24) & 0xFF);
    EEPROM.write(startAddress + 1, (session.start >> 16) & 0xFF);
    EEPROM.write(startAddress + 2, (session.start >>  8) & 0xFF);
    EEPROM.write(startAddress + 3, (session.start      ) & 0xFF);

    EEPROM.write(startAddress + 4, (session.stop >> 24) & 0xFF);
    EEPROM.write(startAddress + 5, (session.stop >> 16) & 0xFF);
    EEPROM.write(startAddress + 6, (session.stop >>  8) & 0xFF);
    EEPROM.write(startAddress + 7, (session.stop      ) & 0xFF);

    EEPROM.write(startAddress + 8,  (session.steps >> 24) & 0xFF);
    EEPROM.write(startAddress + 9,  (session.steps >> 16) & 0xFF);
    EEPROM.write(startAddress + 10, (session.steps >>  8) & 0xFF);
    EEPROM.write(startAddress + 11, (session.steps      ) & 0xFF);

    EEPROM.commit();
}

void sessionStartedDetected() {
    isTreadmillActive = true;
    currentSession.start = (uint32_t) time(nullptr);
}

void sessionEndedDetected() {
    isTreadmillActive = false;
    currentSession.stop = (uint32_t) time(nullptr);
    currentSession.steps = steps;

    // TODO check if the start time and end time are both valid
    if( currentSession.steps < 50000 ) {

    }

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

void recordSessionToEEPROM( TreadmillSession session ) {
  uint32_t count = getSessionCountFromEEPROM();
  if (count >= MAX_SESSIONS) {
    Serial.println("EEPROM is full. Cannot store more sessions.");
    return;
  }

  writeSessionToEEPROM(count, session);
  setSessionCountInEEPROM(count + 1);
  EEPROM.commit();

  Serial.printf("Session stored at index=%u. Steps=%u\n", count, session.steps);
}

// ---------------------------------------------------------------------------
// Simulate a New Session (button-press handler)
// ---------------------------------------------------------------------------
volatile bool buttonPressed = false;

void IRAM_ATTR handleButtonInterrupt() {
  buttonPressed = true;
}

void simulateNewSession() {
  // Start/end = current time, steps = random(1..50)
  TreadmillSession newSession;
  uint32_t nowSec = (uint32_t)time(nullptr);
  newSession.start = nowSec;
  newSession.stop  = nowSec;
  newSession.steps = random(1, 51);
  recordSessionToEEPROM(newSession);
}


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
    std::string rawValue = pCharacteristic->getValue();
    String rxValue = String(rawValue.c_str()); // Convert to Arduino String
        
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
    updateLcd();
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

void updateLcd() {
  //lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("BetterSpan Fit");
  lcd.setCursor(0,1);
  lcd.print( getFormattedTime() );
  lcd.setCursor(0,2);
  lcd.printf("Stps:%5d Spd: %.1f", steps, speedFloat);
  lcd.setCursor(0,3);
  lcd.printf("Sessions: %3d", currentSessionIndex);
}

#define RED_LED 14
#define BLUE_LED 15
#define GREEN_LED 16
void toggleLedColor(uint8_t color) {
    pinMode(color, OUTPUT);  // Ensure the pin is set as an output
    digitalWrite(color, !digitalRead(color));  // Toggle the LED state
}


// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("=== Treadmill Session Tracker (ESP32) ===");
    // Print current sessions for debugging
    printAllSessionsInEEPROM();

    Wire.begin();
    Wire.setClock(400000);
  	// initialize the LCD
    lcd.begin(20,4,LCD_5x8DOTS);
    lcd.init();
    lcd.clear();
    lcd.backlight();

    #ifdef ARDUINO_ARDUINO_NANO_ESP32
      Serial.println("NANO_ESP32 Detected");
    #endif

    #ifdef RETRO_MODE
      Serial.println("RETRO Serial Enabled");
      uart1.begin(4800, SERIAL_8N1, RX1PIN, TX1PIN);
      uart2.begin(4800, SERIAL_8N1, RX2PIN, TX2PIN);

      connectToWiFi();
      delay(1000);
      ntpUDP.begin(ntpPort);
      delay(1000);
      sendNtpRequest();  // Initial time request
    #endif

    // Initialize EEPROM
    EEPROM.begin(EEPROM_SIZE);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(CLEAR_PIN, INPUT_PULLUP);

    // Connect to WiFi and sync time via NTP

    
    // Setup button interrupt
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

    // Initialize BLE
    BLEDevice::init("BetterSpan Treadmill");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

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
  static unsigned long lastLcdUpdate = 0;  // Stores last time LCD was updated
  const unsigned long lcdUpdateInterval = 5000;  // 5 seconds

  checkNtpUpdate();  // Non-blocking NTP handling

  if (millis() - lastLcdUpdate >= lcdUpdateInterval) {
      //Serial.println("LCDU");
      updateLcd();  // Call your LCD update function
      lastLcdUpdate = millis();  // Reset timer
  }

  // Check for button press
  if (buttonPressed) {
    buttonPressed = false;
    simulateNewSession();
  }

  // Clear sessions from EEPROM if CLEAR_PIN goes low (only once per boot)
  if(!clearedSessions && digitalRead(CLEAR_PIN) == LOW) {
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

  #ifdef RETRO_MODE
  // Check if data is available on UART1
    while (uart1.available() > 0) {
      char receivedChar = uart1.read();
    // Serial.printf("1>%02X\n", receivedChar);
      uart1Buf[uart1RxCnt%CMD_BUF_SIZE] = receivedChar;
      uart1RxCnt += 1;
      uart1Buffer += String(receivedChar, DEC) + " ";
      if( uart2RxCnt > 0 ) {
        processResponse();
      }
    }

    // // Check if data is available on UART2
    while (uart2.available() > 0) {
      char receivedChar = uart2.read();
    // Serial.printf("2>%02X\n", receivedChar);
      uart2Buf[uart2RxCnt%CMD_BUF_SIZE] = receivedChar;
      uart2Buffer += String(receivedChar, DEC) + " ";
      uart2RxCnt += 1;
      // Print the UART1 Command
      if(uart1RxCnt > 0 ) {
        processRequest();
      }
    }
  #endif


  delay(1);
}


#ifdef RETRO_MODE
  void processRequest() {
    Serial.print(getFormattedTime());
    Serial.print(" REQ : ");
    Serial.println(uart1Buffer);
   // toggleLedColor(BLUE_LED);

    if( uart1Buffer.startsWith(STEPS_STARTSWITH) ) {
      lastRequestType = LAST_REQUEST_IS_STEPS;
    }
    else if( uart1Buffer.startsWith(SPEED_STARTSWITH) ) {
        getSpeedFromCommand(uart1Buf);
    }
    else {
      lastRequestType = 0;
    }

    uart1Buffer = ""; // Clear the buffer
    uart1RxCnt = 0;
  }

  void processResponse() {
    Serial.print(getFormattedTime());
    Serial.print(" RESP: ");
    Serial.println(uart2Buffer);
    //toggleLedColor(GREEN_LED);

    if( lastRequestType == LAST_REQUEST_IS_STEPS ) {
      steps = uart2Buf[3]*256 + uart2Buf[4];
      //Serial.print("STEPS: ");
      //Serial.println(steps);
      lastRequestType = 0;
    }
    uart2Buffer = ""; // Clear the buffer
    uart2RxCnt = 0;
  }


  // Function to estimate speed in MPH based on integer value
  // TODO figure out how to do kph... but technically i'm only using this to display to a debug lcd so probably wouldn't bother.
  float estimate_mph(int value) {
      return (0.00435 * value) - 0.009;
  }

  /**
   * Pass in a command buffer.
   */
  float getSpeedFromCommand(uint8_t* buf) {
    if( buf[3] == 10 ) {
        speedInt = buf[4]*256+buf[5];
        if( speedInt == 50 ) {
            speedFloat = 0;
            if( isTreadmillActive ) {
               sessionEndedDetected();
            }
            isTreadmillActive = false;
        }
        else if( speedInt > 50 ) {
            speedFloat = estimate_mph(speedInt);
            if( !isTreadmillActive ) {
                sessionStartedDetected();
            }
        }
        return speedFloat;
    }
    return -1;

  }
#endif


