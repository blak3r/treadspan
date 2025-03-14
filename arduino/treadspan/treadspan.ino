/*****************************************************************************
 * Treadmill Session Tracker on ESP32
 *
 * Author: Blake Robertson
 * License: MIT
 *****************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NimBLEDevice.h>
#include <EEPROM.h>
#include <sys/time.h>
#include <time.h>

//-------------------------------------------------------------------------------------------------------------- //
//----------------------------------[ CONFIGURATION SECTION ]--------------------------------------------------- //
//-------------------------------------------------------------------------------------------------------------- //

#define FW_VERSION "v0.9.6"

#define ENABLE_DEBUG 1
#define HAS_TFT_DISPLAY 1  // COMMENT if you aren't using the LilyGo Hardware.
//#define RETRO_MODE 1        // UNCOMMENT IF YOU WANT TO GET SESSIONS VIA SERIAL PORT (REQUIRES special hardware)
#define OMNI_CONSOLE_MODE 1                  // UNCOMMENT IF YOU WANT TO GET SESSIONS VIA BLE (requires OMNI CONSOLE)
#define LOAD_WIFI_CREDENTIALS_FROM_EEPROM 1  // COMMENT if you want to provide credentials for wifi below (easier when developing)
#define INCLUDE_IMPROV_SERIAL 1              // ALLOWS CONFIGURING WIFI THROUGH FLASH INSTALLER WEBPAGE
#define VERBOSE_LOGGING 0                    // Must be defined, change value to 1 to enable. Prints BLE/Serial Payloads to Port.
//#define SESSION_SIMULATION_BUTTONS_ENABLED 1
//#define LCD_4x20_ENABLED 1   // UNCOMMENT if you have a 4x20 I2C LCD Connected

#ifndef LOAD_WIFI_CREDENTIALS_FROM_EEPROM
const char* ssid = "Angela";
const char* password = "iloveblake";  // Example guest network password for demonstration
#endif

#ifdef RETRO_MODE
// RETRO VERSION UART CONFIGURATION
// I haven't tried these GPIO pins with Lily-go, when i built the retro version I had a Arduino ESP32 and these were
// pins i used.
#define RX1PIN 20  // A0, GPIO1, D17
#define TX1PIN 6   // NOT ACTUALLY NEEDED!
#define RX2PIN 23  // A2, GPIO3, D19
#define TX2PIN 8   // NOT ACTUALLY NEEDED
#endif

#ifdef SESSION_SIMULATION_BUTTONS_ENABLED
#define BUTTON_PIN 2  // D2  - Adds fake sessions on press
#define CLEAR_PIN 3   // D3  - Clears all sessions in EEPROM
#endif


//-------------------------------------------------------------------------------------------------------------- //
//----------------------------------[ DONT MODIFY BELOW THIS LINE ]--------------------------------------------- //
//-------------------------------------------------------------------------------------------------------------- //

// DEPENDENT LIBARIES
#ifdef LCD_4x20_ENABLED
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 20, 4);
#endif

#if INCLUDE_IMPROV_SERIAL
#include <ImprovWiFiLibrary.h>
ImprovWiFi improvSerial(&Serial);
#endif

#ifdef HAS_TFT_DISPLAY
#include <SPI.h>
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

#define TOP_BUTTON 35
#define BOT_BUTTON 0
#endif

#if !defined(RETRO_MODE) && !defined(OMNI_CONSOLE_MODE)
#error "At least one must be defined: RETRO_MODE or OMNI_CONSOLE_MODE"
#endif

// EEPROM Configuration
#define EEPROM_SIZE 512
#define MAX_SSID_LENGTH 32
#define SSID_INDEX 0
#define PASSWORDS_INDEX 32
#define SESSIONS_START_INDEX 64
#define MAX_SESSIONS ((512 - (64 + 4)) / 12)
#define SESSION_SIZE_BYTES 12

String getFormattedTimeWithMS();  // Forward Declaration

// I'm not entirely sure this is needed.  I added this because I was having trouble getting ImprovWifi working so i added
// this wrapper to be able to disable the Debug print statements.  Turned out it wasn't the print statements that were interfering.
class DebugWrapper {
public:
  // Begin the underlying Serial if debugging is enabled.
  void begin(unsigned long baud) {
#ifdef ENABLE_DEBUG
    Serial.begin(baud);
#endif
  }

  // Print with template overload.
  template<typename T>
  size_t print(const T& data) {
#ifdef ENABLE_DEBUG
    return Serial.print(data);
#else
    return 0;
#endif
  }

  template<typename T>
  size_t println(const T& data) {
#ifdef ENABLE_DEBUG
    return Serial.println(data);
#else
    return 0;
#endif
  }

  // Overload for println with no arguments.
  size_t println() {
#ifdef ENABLE_DEBUG
    return Serial.println();
#else
    return 0;
#endif
  }

  // A simple variadic printf implementation.
  int printf(const char* format, ...) {
#ifdef ENABLE_DEBUG
    char buffer[256];  // Adjust buffer size as needed
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.printf("[%s] %s", getFormattedTimeWithMS(), buffer);
    return ret;
#else
    return 0;
#endif
  }

  // Overload for write (byte array)
  size_t write(const uint8_t* buffer, size_t size) {
#ifdef ENABLE_DEBUG
    return Serial.write(buffer, size);
#else
    return 0;
#endif
  }

  size_t write(uint8_t data) {
#ifdef ENABLE_DEBUG
    return Serial.write(data);
#else
    return 0;
#endif
  }
};

DebugWrapper Debug;

#ifdef RETRO_MODE
#include <HardwareSerial.h>

int shouldIUpdateCounter = 0;
// Define the UART instances
HardwareSerial uart1(1);  // UART1 - RED = CONSOLE TX
HardwareSerial uart2(2);  // UART2 - ORANGE = TREADMILL TX
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
#endif

struct TreadmillSession {
  uint32_t start;
  uint32_t stop;
  uint32_t steps;
};
bool wasTimeSet = false;

// BLE UUIDs
static const char* BLE_SERVICE_UUID = "0000A51A-12BB-C111-1337-00099AACDEF0";
static const char* BLE_DATA_CHAR_UUID = "0000A51A-12BB-C111-1337-00099AACDEF1";
static const char* BLE_CONFIRM_CHAR_UUID = "0000A51A-12BB-C111-1337-00099AACDEF2";

// BLE Peripheral Variables (modified for NimBLE)
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* dataCharacteristic = nullptr;
NimBLECharacteristic* confirmCharacteristic = nullptr;
bool isMobileAppConnected = false;
bool isMobileAppSubscribed = false;
bool haveNotifiedMobileAppOfFirstSession = false;

int currentSessionIndex = 0;
int sessionsStored = 0;
bool clearedSessions = false;
bool areWifiCredentialsSet = false;

// COMMON STATE VARIABLES (RETRO / OMNI)
int steps = 0;
int calories = 0;         // only omni console mode.
int distance = 0;         // only omni console mode
int avgSpeedInt = 0;      // only omni console mode.
float avgSpeedFloat = 0;  // only omni console
int speedInt = 0;
float speedFloat = 0;
boolean isTreadmillActive = 0;
// NEW: Global variables for tracking today's steps
String lastRecordedDate = "";       //YYYY-MM-DD
unsigned long totalStepsToday = 0;  //

TreadmillSession currentSession;


// Function to estimate speed in MPH based on integer value (common between retro and omni ble protocol)
// TODO figure out how to do kph... but technically i'm only using this to display to a debug lcd so probably wouldn't bother.
float estimate_mph(int value) {
  return (0.00435 * value) - 0.009;
}

#ifdef OMNI_CONSOLE_MODE
// We'll scan for devices whose name starts with this prefix
#define CONSOLE_NAME_PREFIX "LifeSpan-TM"

// UUIDs for the console's custom service/characteristics
const char* CONSOLE_SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb";
const char* CONSOLE_CHARACTERISTIC_UUID_FFF1 = "0000fff1-0000-1000-8000-00805f9b34fb";
const char* CONSOLE_CHARACTERISTIC_UUID_FFF2 = "0000fff2-0000-1000-8000-00805f9b34fb";

// BLE client references
NimBLEClient* consoleClient = nullptr;
NimBLERemoteCharacteristic* consoleNotifyCharacteristic = nullptr;
NimBLERemoteCharacteristic* consoleWriteCharacteristic = nullptr;

bool consoleIsConnected = false;
int consoleCommandIndex = 0;
static unsigned long lastConsoleCommandSentAt = 0;           // last time we requested a command
const unsigned long consoleCommandUpdateIntervalMin = 300;   // Will send a new command after
const unsigned long consoleCommandUpdateIntervalMax = 1400;  // Will wait for up to 1.2 seconds, found with 500 there were occassional commands that took longer.
volatile uint8_t lastConsoleCommandIndex = 0;
volatile uint8_t lastConsoleCommandOpcode = 0;
volatile bool commandResponseReceived = true;
volatile uint8_t neverRecvCIDCount = 0;
volatile uint8_t timesSessionStatusHasBeenTheSame = 0;
volatile uint8_t lastSessionStatus = -1;

#define OPCODE_STEPS 0x88
#define OPCODE_DURATION 0x89
#define OPCODE_STATUS 0x91
#define OPCODE_DISTANCE 0x85
#define OPCODE_CALORIES 0x87
#define OPCODE_SPEED 0x82

// We want to get STATUS and STEPS more frequently so LCD updates quicker / sessions starting / stopping are detected faster.
const uint8_t consoleCommandOrder[] = { OPCODE_STEPS, OPCODE_STATUS, OPCODE_DURATION, OPCODE_STATUS, OPCODE_DISTANCE,
                                        OPCODE_STEPS, OPCODE_STATUS, OPCODE_CALORIES, OPCODE_STATUS, OPCODE_SPEED };
#define CONSOLE_COMMAND_COUNT sizeof(consoleCommandOrder);

// -----------------------------------------------------------------------
// BLE Scan -> Look for "LifeSpan-TM"
// -----------------------------------------------------------------------
static NimBLEAddress foundConsoleAddress;  // = NimBLEAddress("");//nullptr; //TODO
static bool foundConsole = false;

/** Define a class to handle the callbacks when scan events are received */
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    if (VERBOSE_LOGGING) { Debug.printf("Advertised Device found: %s\n", advertisedDevice->toString().c_str()); }
    if (advertisedDevice->haveName() /* advertisedDevice->isAdvertisingService(NimBLEUUID(CONSOLE_SERVICE_UUID))*/) {
      std::string devName = advertisedDevice->getName();
      if (devName.rfind(CONSOLE_NAME_PREFIX, 0) == 0) {
        Debug.printf("Found a 'LifeSpan' device at: %s\n", advertisedDevice->getAddress());
        /** stop scan before connecting */
        NimBLEDevice::getScan()->stop();
        foundConsoleAddress = advertisedDevice->getAddress();
        foundConsole = true;
      }
    }
  }

  /** Callback to process the results of the completed scan or restart it */
  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    Debug.printf("Scan Ended, reason: %d, device count: %d; Restarting scan\n", reason, results.getCount());
    NimBLEDevice::getScan()->start(1000, false, true);  // <-- this is important, not sure why the main loop reconnect doesn't work.
  }
} scanCallbacks;


// Callback to handle incoming notifications from the console
void consoleNotifyCallback(NimBLERemoteCharacteristic* pCharacteristic,
                           uint8_t* data, size_t length, bool isNotify) {
#if VERBOSE_LOGGING
  Debug.printf("RESP %02X: ", lastConsoleCommandIndex);
  for (size_t i = 0; i < length; i++) {
    Debug.printf("%02X ", data[i]);
  }
  Debug.print("\n");
#endif

  // Parse the responses for each index
  if (lastConsoleCommandOpcode == OPCODE_STEPS) {
    steps = data[2] * 256 + data[3];
    Debug.printf("Steps: %d\n", steps);
  } else if (lastConsoleCommandOpcode == OPCODE_CALORIES) {
    calories = data[2] * 256 + data[3];
    Debug.printf("Calories: %d\n", calories);
  } else if (lastConsoleCommandOpcode == OPCODE_DISTANCE) {
    distance = data[2] * 256 + data[3];
    Debug.printf("Distance: %d\n", distance);
  } else if (lastConsoleCommandOpcode == OPCODE_SPEED) {
    avgSpeedInt = data[2] * 256 + data[3];
    avgSpeedFloat = estimate_mph(avgSpeedInt);
    Debug.printf("AvgSpeedInt: %d, %.1f MPH\n", avgSpeedInt, avgSpeedFloat);
  } else if (lastConsoleCommandOpcode == OPCODE_DURATION) {
    Debug.printf("DURATION: %d:%d:%d\n", data[2], data[3], data[4]);
    if (wasTimeSet) {
      // Fixes issue where device powers on after a session on treadmill had started (or if time wasn't set when session started)
      uint32_t sessionStartTime = (uint32_t)time(nullptr) - ((data[4]) + (data[3] * 60) + (data[2] * 60 * 60));
      //Debug.printf("Updating sessionStartTime to: %lu\n", sessionStartTime);
      currentSession.start = sessionStartTime;
    }
  } else if (lastConsoleCommandOpcode == OPCODE_STATUS) {
    uint8_t status = data[2];
#define STATUS_RUNNING 3
#define STATUS_PAUSED 5
#define STATUS_SUMMARY_SCREEN 4
#define STATUS_STANDBY 1

    // Encountered some strange behavior.  I think we were missing command responses from the console.  And this would cause us to
    // interpret incorrectly a different command.  Causing us to stop sessions and immediately restart them.  I'm not only ending
    // sessions if I get the same status at least twice.
    if (lastSessionStatus == status) {
      timesSessionStatusHasBeenTheSame += 1;
    } else {
      timesSessionStatusHasBeenTheSame = 0;
    }
    lastSessionStatus = status;

    Debug.printf("Treadmill Status: ");

    if (data[3] || data[4]) {
      // Extra check to ensure we aren't processing like wrong command here. (These are otherwise always 0);
      Debug.printf("Invalid CMD Resp for Status, [3]=%2X, [4]=%2X", data[3], data[4]);
      commandResponseReceived = true;
      return;
    }
    switch (status) {
      case STATUS_RUNNING:
        Debug.print("RUNNING\n");
        if (!isTreadmillActive && timesSessionStatusHasBeenTheSame >= 1) {
          sessionStartedDetected();
          isTreadmillActive = true;
        }
        break;
      case STATUS_PAUSED:
        Debug.print("PAUSED\n");
        if (isTreadmillActive && timesSessionStatusHasBeenTheSame >= 1) {
          sessionEndedDetected();
          isTreadmillActive = false;
        }
        break;
      case STATUS_SUMMARY_SCREEN:
        Debug.print("SUMMARY_SCREEN\n");
        if (isTreadmillActive && timesSessionStatusHasBeenTheSame >= 1) {
          sessionEndedDetected();
          isTreadmillActive = false;
        }
        break;
      case STATUS_STANDBY:
        Debug.print("STANDBY\n");
        if (isTreadmillActive && timesSessionStatusHasBeenTheSame >= 1) {
          sessionEndedDetected();
          isTreadmillActive = false;
        }
        break;
      default:
        Debug.printf("UNKNOWN: '%d'\n", status);
    }
  }
  commandResponseReceived = true;
}

// Modified BLE Client Callback for NimBLE
class MyClientCallback : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pclient) override {
    Debug.println("Console client connected.");
  }

  void onDisconnect(NimBLEClient* pclient, int reason) override {
    Debug.println(" !!! Console client disconnected.");
    consoleIsConnected = false;
  }
};

void connectToFoundConsole() {
  consoleClient = NimBLEDevice::createClient();
  consoleClient->setClientCallbacks(new MyClientCallback());

  Debug.printf("Attempting to connect to: %s\n", foundConsoleAddress.toString().c_str());

  if (!consoleClient->connect(foundConsoleAddress)) {
    Debug.println("Failed to connect to LifeSpan console.");
    consoleIsConnected = false;
    return;
  }

  consoleIsConnected = true;
  lastConsoleCommandSentAt = millis();
  Debug.println("Connected to console. Discovering services...");

  NimBLERemoteService* service = consoleClient->getService(CONSOLE_SERVICE_UUID);
  if (service == nullptr) {
    Debug.println("Failed to find FFF0 service. Disconnecting...");
    consoleClient->disconnect();
    consoleIsConnected = false;
    return;
  }

  // FFF1: Notify characteristic
  consoleNotifyCharacteristic = service->getCharacteristic(CONSOLE_CHARACTERISTIC_UUID_FFF1);
  if (consoleNotifyCharacteristic == nullptr) {
    Debug.println("Failed to find FFF1 characteristic. Disconnecting...");
    consoleClient->disconnect();
    consoleIsConnected = false;
    return;
  }

  if (consoleNotifyCharacteristic->canNotify()) {
    consoleNotifyCharacteristic->subscribe(true, consoleNotifyCallback);
    Debug.println("Subbed to notifications on FFF1.");
  }

  // FFF2: Write characteristic
  consoleWriteCharacteristic = service->getCharacteristic(CONSOLE_CHARACTERISTIC_UUID_FFF2);
  if (!consoleWriteCharacteristic || !consoleWriteCharacteristic->canWrite()) {
    Debug.println("FFF2 characteristic not found");
    consoleClient->disconnect();
    consoleIsConnected = false;
    return;
  }
}

// Public function to scan for "LifeSpan-TM" device & connect
void connectToConsoleViaBLE() {
  Debug.printf("Scanning for LifeSpan Omni Console...\n");

  // Reset flags
  foundConsole = false;
  //foundConsoleAddress = NimBLEAddress(""); // TODO

  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&scanCallbacks, false);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(3, false);  // Scan for 1 second

  // NimBLE requires a small delay after scanning
  delay(50);

  if (foundConsole) {
    connectToFoundConsole();
  } else {
    Debug.println("No LifeSpan-TM device found in scan window.");
  }
}

// Send read requests (hexStrings) in a round-robin fashion
void consoleBLEMainLoop() {
  // If console is not connected, try to reconnect periodically
  if (!consoleIsConnected) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 5000) {
      lastTry = millis();
      connectToConsoleViaBLE();
    }
  } else {
    // If enough time has passed, send the next read request
    uint32_t millisSinceLastCommandSentAt = millis() - lastConsoleCommandSentAt;
    bool isItMoreThanMinUpdateInterval = (millisSinceLastCommandSentAt >= consoleCommandUpdateIntervalMin);
    bool isItMoreThanMaxUpdateInterval = (millisSinceLastCommandSentAt >= consoleCommandUpdateIntervalMax);

    if ((commandResponseReceived && isItMoreThanMinUpdateInterval) || isItMoreThanMaxUpdateInterval) {
      lastConsoleCommandSentAt = millis();  // Reset timer

      uint8_t consoleCmdBuf[] = { 0xA1, consoleCommandOrder[consoleCommandIndex], 0x00, 0x00, 0x00, 0x00 };

#if VERBOSE_LOGGING
      Debug.printf("REQ %d: ", consoleCommandIndex);
      for (int i = 0; i < sizeof(consoleCmdBuf); i++) {
        Debug.printf("%02X ", consoleCmdBuf[i]);
      }
      Debug.print("\n");
#endif

      if (!commandResponseReceived) {
        Debug.printf("ERROR: Never RECV CID: %d, %2X\n", lastConsoleCommandIndex, lastConsoleCommandOpcode);
        neverRecvCIDCount++;
      }
      Debug.printf("Wrote CID %d, %2X\n", consoleCommandIndex, consoleCommandOrder[consoleCommandIndex]);
      consoleWriteCharacteristic->writeValue((uint8_t*)consoleCmdBuf, sizeof(consoleCmdBuf));
      lastConsoleCommandIndex = consoleCommandIndex;
      lastConsoleCommandOpcode = consoleCommandOrder[consoleCommandIndex];
      commandResponseReceived = false;

      consoleCommandIndex = (consoleCommandIndex + 1) % CONSOLE_COMMAND_COUNT;
    }
  }
}
#endif

// ------------------------------------------------------------------------------
// LEDs / Indicators - Not really used anymore, depends on HW being Arduino ESP32
// ------------------------------------------------------------------------------
#define RED_LED 14
#define BLUE_LED 15
#define GREEN_LED 16
void toggleLedColor(uint8_t color) {
  pinMode(color, OUTPUT);                    // Ensure the pin is set as an output
  digitalWrite(color, !digitalRead(color));  // Toggle the LED state
}
void setLed(uint8_t color, bool status) {
  pinMode(color, OUTPUT);
  digitalWrite(color, status);
}

// ---------------------------------------------------------------------------
// Wifi / NTP
// ---------------------------------------------------------------------------
// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const int ntpPort = 123;
WiFiUDP ntpUDP;

void trimString(char* str, int maxLength) {
  for (int i = 0; i < maxLength; i++) {
    if (str[i] == '\0') break;  // Stop at first null
  }
}

void loadWiFiCredentialsFromEEPROM(char* ssid, char* password) {
  for (int i = 0; i < MAX_SSID_LENGTH; i++) {
    ssid[i] = EEPROM.read(SSID_INDEX + i);
    if (ssid[i] == '\0') break;
  }
  ssid[MAX_SSID_LENGTH - 1] = '\0';

  for (int i = 0; i < MAX_SSID_LENGTH; i++) {
    password[i] = EEPROM.read(PASSWORDS_INDEX + i);
    if (password[i] == '\0') break;
  }
  password[MAX_SSID_LENGTH - 1] = '\0';

  Debug.print("Loaded SSID: ");
  Debug.println(ssid);
  Debug.print("Loaded Password: ");
  Debug.println(password);
}

bool loadWifiCredentialsIntoBuffers(char* ssidBuf, char* passwordBuf) {
#if LOAD_WIFI_CREDENTIALS_FROM_EEPROM
  loadWiFiCredentialsFromEEPROM(ssidBuf, passwordBuf);
#else
  strncpy(ssidBuf, ssid, sizeof(ssidBuf));  // Developer mode... ssid/password are hardcoded.
  strncpy(passwordBuf, password, sizeof(ssidBuf));
  //Debug.println("Using SSID from Program Memory named: %s", ssid);
#endif

  Debug.printf("ssidBuf[0]: %02X, len1 %d len2 %d\n", ssidBuf[0], strlen(ssidBuf), strlen(passwordBuf));

  return ssidBuf[0] != 0xFF && strlen(ssidBuf) > 0 && strlen(passwordBuf) > 0 && strlen(ssidBuf) < 31 && strlen(passwordBuf) < 31;
}

void screenWifiConnecting(void);  // forward declaration

bool connectWifi(const char* ssid, const char* password) {
#ifdef LCD_4x20_ENABLED
  lcd.clear();
  lcd.print("Connecting to WiFi");
#endif

  Debug.printf("Connecting to WiFi... %s\n", ssid);
  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 7000) {
#ifdef HAS_TFT_DISPLAY
    tftWifiConnectingScreen(ssid);
#endif
    delay(100);
  }

  return WiFi.status() == WL_CONNECTED;
}

// This callback is called when WiFi connects and gets an IP
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    // START UDP
    ntpUDP.begin(ntpPort);
    delay(100);
    sendNtpRequest();  // initial time request
  }
}

void setupWifi() {
  char ssid[32], password[32];

  // Register the WiFi event handler
  WiFi.onEvent(WiFiEvent);

  areWifiCredentialsSet = loadWifiCredentialsIntoBuffers(ssid, password);  // loads from SSID/PASS from file system or program memory depending on build configs.

  Debug.printf("areWifiCredentialsSet: %d\n", areWifiCredentialsSet);

  if (areWifiCredentialsSet) {
    connectWifi(ssid, password);
  } else {
  }
}



#define NTP_PACKET_SIZE 48
byte ntpPacketBuffer[NTP_PACKET_SIZE];

// Timer variables
unsigned long lastNtpRequest = 0;
unsigned long ntpUpdateInterval = 3000;  // eventually 600000 (10 mins), but start short for quick sync

void sendNtpRequest() {
  Debug.println("Sending NTP request...");

  memset(ntpPacketBuffer, 0, NTP_PACKET_SIZE);
  ntpPacketBuffer[0] = 0b11100011;  // LI, Version, Mode
  ntpPacketBuffer[1] = 0;           // Stratum
  ntpPacketBuffer[2] = 6;           // Polling Interval
  ntpPacketBuffer[3] = 0xEC;        // Peer Clock Precision

  // Send packet to NTP server
  ntpUDP.beginPacket(ntpServer, ntpPort);
  ntpUDP.write(ntpPacketBuffer, NTP_PACKET_SIZE);
  ntpUDP.endPacket();

  lastNtpRequest = millis();
}

void checkNtpResponse() {
  int packetSize = ntpUDP.parsePacket();
  if (packetSize >= NTP_PACKET_SIZE) {
    Debug.println("NTP response received!");

    ntpUDP.read(ntpPacketBuffer, NTP_PACKET_SIZE);
    unsigned long highWord = word(ntpPacketBuffer[40], ntpPacketBuffer[41]);
    unsigned long lowWord = word(ntpPacketBuffer[42], ntpPacketBuffer[43]);
    time_t epochTime = (highWord << 16 | lowWord) - 2208988800UL;  // Convert to UNIX time

    // Set system time
    struct timeval tv;
    tv.tv_sec = epochTime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    Debug.print("System time updated: ");
    Debug.println(getFormattedTime());

    ntpUpdateInterval = 600000;  // set to 10 minutes after first success
    wasTimeSet = true;
  }
}

void checkNtpUpdate() {
  if (WiFi.status() == WL_CONNECTED) {
    if ((millis() - lastNtpRequest) >= ntpUpdateInterval) {
      sendNtpRequest();
    }
    checkNtpResponse();
  }
}

String getFormattedTime() {
  time_t now = time(nullptr);
  if (now < 100000) {
    return "TBD, NTP sync";
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  return String(buffer);
}


String getFormattedTimeWithMS() {
  time_t now = time(nullptr);
  if (now < 100000) {
    return "TBD, NTP sync";
  }

  // Use millis() to get the milliseconds component (note: millis() returns the time since program start)
  unsigned long ms = millis() % 1000;

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[40];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);

  char result[50];
  snprintf(result, sizeof(result), "%s.%03lu", buffer, ms);

  return String(result);
}

// ---------------------------------------------------------------------------
// EEPROM Layout & Session Struct
// ---------------------------------------------------------------------------
//  - [0...31]   : WiFi SSID
//  - [32..63]   : WiFi PASS
//  - [64..67]   : uint32_t sessionCount
//  - [68..end] : session data in blocks of 12 bytes each
//
// Each session block:
//    Byte 0..3  : start time (Big-endian)
//    Byte 4..7  : stop time  (Big-endian)
//    Byte 8..11 : steps      (Big-endian)



uint32_t getSessionCountFromEEPROM() {
  uint32_t count = 0;
  count |= (EEPROM.read(SESSIONS_START_INDEX + 0) << 24);
  count |= (EEPROM.read(SESSIONS_START_INDEX + 1) << 16);
  count |= (EEPROM.read(SESSIONS_START_INDEX + 2) << 8);
  count |= EEPROM.read(SESSIONS_START_INDEX + 3);

  if (count >= MAX_SESSIONS) {
    count = 0;
    Debug.println("DETECTED UNINITED EEPROM");
  }

  sessionsStored = count;
  return count;
}

void setSessionCountInEEPROM(uint32_t count) {
  sessionsStored = count;
  EEPROM.write(SESSIONS_START_INDEX + 0, (count >> 24) & 0xFF);
  EEPROM.write(SESSIONS_START_INDEX + 1, (count >> 16) & 0xFF);
  EEPROM.write(SESSIONS_START_INDEX + 2, (count >> 8) & 0xFF);
  EEPROM.write(SESSIONS_START_INDEX + 3, count & 0xFF);
  EEPROM.commit();
}

TreadmillSession readSessionFromEEPROM(int index) {
  TreadmillSession session;
  int startAddress = SESSIONS_START_INDEX + 4 + (index * SESSION_SIZE_BYTES);

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
  int startAddress = SESSIONS_START_INDEX + 4 + (index * SESSION_SIZE_BYTES);

  EEPROM.write(startAddress, (session.start >> 24) & 0xFF);
  EEPROM.write(startAddress + 1, (session.start >> 16) & 0xFF);
  EEPROM.write(startAddress + 2, (session.start >> 8) & 0xFF);
  EEPROM.write(startAddress + 3, (session.start) & 0xFF);

  EEPROM.write(startAddress + 4, (session.stop >> 24) & 0xFF);
  EEPROM.write(startAddress + 5, (session.stop >> 16) & 0xFF);
  EEPROM.write(startAddress + 6, (session.stop >> 8) & 0xFF);
  EEPROM.write(startAddress + 7, (session.stop) & 0xFF);

  EEPROM.write(startAddress + 8, (session.steps >> 24) & 0xFF);
  EEPROM.write(startAddress + 9, (session.steps >> 16) & 0xFF);
  EEPROM.write(startAddress + 10, (session.steps >> 8) & 0xFF);
  EEPROM.write(startAddress + 11, (session.steps) & 0xFF);

  EEPROM.commit();
}

void printSessionDetails(TreadmillSession s, int index) {
  time_t startTime = (time_t)s.start;
  time_t stopTime = (time_t)s.stop;

  char startStr[26] = "Unknown";
  char stopStr[26] = "Unknown";
  ctime_r(&startTime, startStr);
  ctime_r(&stopTime, stopStr);

  if (startStr[strlen(startStr) - 1] == '\n') startStr[strlen(startStr) - 1] = '\0';
  if (stopStr[strlen(stopStr) - 1] == '\n') stopStr[strlen(stopStr) - 1] = '\0';

  Debug.printf("Session #%d:\n", index);
  Debug.printf("  Start: %s (Unix: %u)\n", startStr, s.start);
  Debug.printf("  Stop : %s (Unix: %u)\n", stopStr, s.stop);
  Debug.printf("  Steps: %u\n", s.steps);
}

void printAllSessionsInEEPROM() {
  uint32_t count = getSessionCountFromEEPROM();
  uint32_t printCount = (count > MAX_SESSIONS) ? MAX_SESSIONS : count;

  Debug.printf("\n--- EEPROM Debug: Found %u session(s) stored ---\n", count);
  if (count > MAX_SESSIONS) {
    Debug.printf("Printing only the first %u session(s)...\n", MAX_SESSIONS);
  }

  for (uint32_t i = 0; i < printCount; i++) {
    TreadmillSession s = readSessionFromEEPROM(i);
    printSessionDetails(s, i);
  }
  Debug.println("----------------------------------------------\n");
}

void recordSessionToEEPROM(TreadmillSession session) {
  uint32_t count = getSessionCountFromEEPROM();
  if (count >= MAX_SESSIONS) {
    Debug.println("EEPROM is full. Cannot store more sessions.");
    return;
  }
  writeSessionToEEPROM(count, session);
  setSessionCountInEEPROM(count + 1);
  EEPROM.commit();

  Debug.printf("Session stored at index=%u. Steps=%u\n", count, session.steps);

  // Update today's steps total
  String today = getFormattedDate();
  if (lastRecordedDate != today) {
    lastRecordedDate = today;
    totalStepsToday = 0;
  }
  totalStepsToday += session.steps;
}


void saveWiFiCredentials(const char* ssid, const char* password) {
  if (strlen(ssid) > 32 || strlen(password) > 32) {
    Debug.println("Error: SSID/Pass too long");
    return;
  }

  // Write SSID to EEPROM
  for (int i = 0; i < MAX_SSID_LENGTH; i++) {
    EEPROM.write(SSID_INDEX + i, (i < strlen(ssid)) ? ssid[i] : '\0');
  }

  // Write Password to EEPROM
  for (int i = 0; i < MAX_SSID_LENGTH; i++) {
    EEPROM.write(PASSWORDS_INDEX + i, (i < strlen(password)) ? password[i] : '\0');
  }

  EEPROM.commit();  // Ensure data is written (needed for ESP8266/ESP32)
  Debug.println("WiFi creds saved to EEPROM");
  areWifiCredentialsSet = true;
}

#ifdef INCLUDE_IMPROV_SERIAL
void onImprovWiFiErrorCb(ImprovTypes::Error err) {
  Debug.println("onImprovWifiErrorCb");
  Debug.println(err);
}

void onImprovWiFiConnectedCb(const char* ssid, const char* password) {
  Debug.println("IS: onImprovWiFiConnectedCb");
  saveWiFiCredentials(ssid, password);
  areWifiCredentialsSet = true;
}
#endif

// ---------------------------------------------------------------------------
// Today's steps
// ---------------------------------------------------------------------------
String getFormattedDate() {
  time_t now = time(nullptr);
  if (now < 100000) {
    return "NTP";
  }

  // Set timezone to Pacific Time
  setenv("TZ", "PST8PDT", 1);  // "PST8PDT" adjusts for daylight saving time
  tzset();

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buffer[11];  // "YYYY-MM-DD"
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
  return String(buffer);
}

unsigned long getTodaysSteps() {
  String today = getFormattedDate();
  if (wasTimeSet && lastRecordedDate != today) {
    lastRecordedDate = today;
    totalStepsToday = 0;
  }

  if (isTreadmillActive) {
    return totalStepsToday + steps;
  }

  return totalStepsToday;
}

// ---------------------------------------------------------------------------
// Session Start/End
// ---------------------------------------------------------------------------
void sessionStartedDetected() {
  Debug.printf("%s >> NEW SESSION Started!\n", getFormattedTime().c_str());
  isTreadmillActive = true;
  currentSession.start = (uint32_t)time(nullptr);
}

void sessionEndedDetected() {
  isTreadmillActive = false;
  currentSession.stop = (uint32_t)time(nullptr);
  currentSession.steps = steps;

  if (currentSession.steps > 50000) {
    // Debug.println("ERROR: Session steps too large, skipping save.");
    return;
  }
  if (!currentSession.start) {
    // Debug.println("ERROR: Session had no start time, skipping save.");
    return;
  }

  Debug.printf("%s << NEW SESSION Ended\n", getFormattedTime().c_str());
  printSessionDetails(currentSession, sessionsStored);
  recordSessionToEEPROM(currentSession);
}

// ---------------------------------------------------------------------------
// Simulate a New Session (button-press handler)
// ---------------------------------------------------------------------------
volatile bool buttonPressed = false;

void IRAM_ATTR handleButtonInterrupt() {
  buttonPressed = true;
}

void simulateNewSession() {
  TreadmillSession newSession;
  uint32_t nowSec = (uint32_t)time(nullptr);
  newSession.start = nowSec;
  newSession.stop = nowSec;
  newSession.steps = random(1, 51);
  recordSessionToEEPROM(newSession);
}

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------
#ifdef LCD_4x20_ENABLED
String getCurrentSessionElapsed() {
  time_t now = time(nullptr);
  if (currentSession.start == 0 || now < currentSession.start) {
    return "00:00:00";
  }
  uint32_t elapsed = now - currentSession.start;
  uint32_t hours = elapsed / 3600;
  uint32_t minutes = (elapsed % 3600) / 60;
  uint32_t seconds = elapsed % 60;

  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", hours, minutes, seconds);
  return String(buffer).c_str();
}

void lcdPrintBoolIndicator(boolean pValue) {
  if (pValue) {
    lcd.print("+");
  } else {
    lcd.print("-");
  }
}

void updateLcd() {
  static uint pageStyle = 0;

#if OMNI_CONSOLE_MODE
  if (pageStyle == 0) {
    Debug.printf("Clearing LCD, consIsConn: %d, isMobAppConn: %d, isMobSubs:%\n", consoleIsConnected, isMobileAppConnected, isMobileAppSubscribed);
    lcd.clear();  // Causes additional blocking i didn't want in the Serial mode.
  }
#endif

  lcd.setCursor(0, 0);
  lcd.printf("TreadSpan %s ", FW_VERSION);
  lcdPrintBoolIndicator(consoleIsConnected);
  lcdPrintBoolIndicator(isMobileAppConnected);
  lcdPrintBoolIndicator(isMobileAppSubscribed);

  lcd.setCursor(0, 1);
  if (pageStyle < 2) {
    lcd.printf("Steps Today: %7lu", getTodaysSteps());
  } else {
    lcd.printf("Sessions To Sync: %2d", sessionsStored);
  }
  pageStyle = (pageStyle + 1) % 4;

  if (isTreadmillActive) {
    lcd.setCursor(0, 2);
    lcd.printf("%s %s   ", getFormattedTime().c_str(), getCurrentSessionElapsed().c_str());
    lcd.setCursor(0, 3);
    lcd.printf("Steps:%4d MPH: %.1f", steps, speedFloat);
  } else {
    lcd.setCursor(0, 2);
    lcd.print("Save Sessions On App");
    lcd.setCursor(0, 3);
    lcd.print(" OR Start Walking!  ");
  }
}

void periodicLcdUpdateMainLoopHandler() {
  // Periodic LCD refresh
  static unsigned long lastLcdUpdate = 0;
  const unsigned long lcdUpdateInterval = 1000;
  if (millis() - lastLcdUpdate >= lcdUpdateInterval) {
    updateLcd();
    lastLcdUpdate = millis();
  }
}
#endif


#ifdef HAS_TFT_DISPLAY

#define RES_Y 135
#define RES_X 240

#include "icons.h"
#include "AGENCYB22pt7b.h"

volatile bool topButtonPressed = false;
volatile uint8_t tftPage = 0;
volatile unsigned long lastDebounceTimeTop = 0;  // Track last button press time
const unsigned long debounceDelay = 200;         // Debounce time in milliseconds
void IRAM_ATTR handleTopButtonInterrupt() {
  unsigned long currentTime = millis();                     // Get current time
  if (currentTime - lastDebounceTimeTop > debounceDelay) {  // Check if enough time has passed
    topButtonPressed = true;
    tftPage += 1;
    lastDebounceTimeTop = currentTime;  // Update debounce timer
  }
}

volatile bool botButtonPressed = false;
volatile unsigned long lastDebounceTimeBot = 0;  // Track last button press time
void IRAM_ATTR handleBotButtonInterrupt() {
  unsigned long currentTime = millis();                     // Get current time
  if (currentTime - lastDebounceTimeBot > debounceDelay) {  // Check if enough time has passed
    botButtonPressed = true;
    lastDebounceTimeBot = currentTime;  // Update debounce timer
  }
}

void tftSetup() {
#ifdef HAS_TFT_DISPLAY
  tft.init();
  delay(25);
  tft.setRotation(1);  // 2-portrait, usb up, 0-portrait (opposite)
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  sprite.createSprite(RES_X, RES_Y);

  pinMode(TOP_BUTTON, INPUT_PULLUP);
  pinMode(BOT_BUTTON, INPUT_PULLUP);

  // Setup button interrupt
  attachInterrupt(digitalPinToInterrupt(TOP_BUTTON), handleTopButtonInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BOT_BUTTON), handleBotButtonInterrupt, FALLING);

  tftUpdateDisplay();  // Initial display update
#endif
}

void tftSplashScreen() {
  sprite.setSwapBytes(true);
  sprite.setTextDatum(TL_DATUM);
  sprite.fillScreen(TFT_BLACK);
  sprite.fillRect(0, 0, RES_X, RES_Y, TFT_BLACK);
  // 19 was centered
  sprite.pushImage(0, 9, 96, 96, treadspan96);

  sprite.setTextDatum(MC_DATUM);
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_GOLD, TFT_BLACK);
  const int centerTextX = 96 + ((RES_X - (96)) / 2);
  const int size2Height = sprite.fontHeight();
  const int textLine1Y = 15;
  sprite.drawString("TREADSPAN", centerTextX, textLine1Y + 0 * size2Height + 10);
  sprite.setFreeFont(0);
  sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sprite.drawString("by", centerTextX, textLine1Y + 1 * size2Height + 10);
  sprite.setFreeFont(0);
  sprite.setTextColor(TFT_SILVER, TFT_BLACK);
  sprite.drawString("Blake", centerTextX, textLine1Y + 2 * size2Height + 10);
  sprite.drawString("Robertson", centerTextX, textLine1Y + 3 * size2Height + 10);
  sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sprite.drawString(FW_VERSION, centerTextX, textLine1Y + 4 * size2Height + 10);
  sprite.setTextSize(1);
  sprite.setTextDatum(TL_DATUM);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  sprite.drawString("https://github.com/blak3r/treadspan", 0, (RES_Y - sprite.fontHeight()) - 2);

  sprite.setTextDatum(TL_DATUM);
  sprite.pushSprite(0, 0);
}

void tftWifiStatusScreen(uint8_t configVsNotConnected, const char* ssid) {
  sprite.setSwapBytes(true);
  sprite.setTextDatum(TL_DATUM);
  sprite.fillScreen(TFT_BLACK);
  sprite.fillRect(0, 0, RES_X, RES_Y, TFT_BLACK);
  sprite.pushImage((RES_X - 101) / 2, 0, 101, 96, no_wifi_solid_101x96);
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);

  const char* wifi1 = configVsNotConnected == 0 ? "Configure WiFi" : "Unable to Connect To:";
  const char* wifi2 = configVsNotConnected == 0 ? "Using Web Installer" : ssid;

  int wifi1Width = sprite.textWidth(wifi1);
  int wifi2Width = sprite.textWidth(wifi2);
  sprite.setCursor((RES_X - wifi1Width) / 2, 100);
  sprite.print(wifi1);
  sprite.setCursor((RES_X - wifi2Width) / 2, 100 + sprite.fontHeight());
  sprite.print(wifi2);
  sprite.pushSprite(0, 0);
}

void tftWifiConnectingScreen(const char* ssid) {
  static uint8_t dotCounter = 0;
  sprite.setSwapBytes(true);
  sprite.fillScreen(TFT_BLACK);
  sprite.fillRect(0, 0, RES_X, RES_Y, TFT_BLACK);
  sprite.pushImage(0, 19, 96, 96, treadspan96);

  sprite.setFreeFont(0);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_GOLD, TFT_BLACK);
  const int centerTextX = 96 + ((RES_X - (96)) / 2);
  const int size2Height = sprite.fontHeight();
  const int textLine1Y = 15;
  sprite.drawString("TREADSPAN", centerTextX, textLine1Y + 0 * size2Height + 10);
  sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sprite.drawString("WiFi", centerTextX, textLine1Y + 1 * size2Height + 10);
  sprite.drawString("Connecting", centerTextX, textLine1Y + 2 * size2Height + 10);
  sprite.drawString("to", centerTextX, textLine1Y + 3 * size2Height + 10);
  sprite.setTextColor(TFT_MAGENTA, TFT_BLACK);
  sprite.drawString(ssid, centerTextX, textLine1Y + 5 * size2Height + 10);

  // Use a lookup table to get the corresponding string
  static const char* dotStrings[] = { ".", "..", "..." };
  sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
  sprite.drawString(dotStrings[dotCounter++ % 3], centerTextX, textLine1Y + 6 * size2Height + 10);
  sprite.setTextDatum(TL_DATUM);
  sprite.pushSprite(0, 0);
}


/**
 * for sizeY = 32, X will take up 14 pixels
 * for sizeY = 24, width of icon is 10 pixels
 * @param x - startX upper left corner
 * @param y - startY 
 * @param sizeY - how big you want the icon such as 24 pixels high.
 * @param color - color, default is tft.color565(0, 130, 252);
 */
void tftDrawBluetoothLogo(int x, int y, int sizeY, uint32_t color) {
  const uint16_t fullX = x + ((14 * sizeY) / 32);
  const uint16_t centerX = x + (fullX - x) / 2;
  const uint16_t x0 = x;
  const uint16_t y0 = y;
  const uint16_t y1 = y + ((9 * sizeY) / 32);
  const uint16_t y2 = y + ((22 * sizeY) / 32);
  const uint16_t fullY = sizeY;
  sprite.drawLine(centerX, y0, centerX, fullY, color);  // Vertical center line
  sprite.drawLine(x0, y1, fullX, y2, color);            // Left down
  sprite.drawLine(x0, y2, fullX, y1, color);            // Upper middle cross
  sprite.drawLine(centerX, y0, fullX, y1, color);       //Small line top of B
  sprite.drawLine(centerX, fullY, fullX, y2, color);    // Small Line, Bottom of B

  Debug.printf("fullX=%d, centerX=%d, y1=%d, y2=%d, fullY=%d, 0x%4X\n", fullX, centerX, y1, y2, fullY, color);
}

#define BLUETOOTH_BLUE 0x041F  //tft.color565(0, 130, 252)

void tftDrawBluetoothLogo24(int x, int y) {
  const uint16_t bluetoothBlue = BLUETOOTH_BLUE;
  return tftDrawBluetoothLogo(x, y, 24, bluetoothBlue);
}

// Define an enum for the display metrics

#define PDM_ALTERNATE 0 
#define PDM_SESSION_STEPS 1
#define PDM_TODAYS_STEPS 2

/**
 * Update the TFT display with step count and unsynced session count in a horizontal format.
 * @param primaryDisplayMetric - 0 is alternate, 1 is session steps, 2 always shows cummulative todays
 */
void tftRunningScreen(uint8_t primaryDisplayMetric) {
  static bool recordIndicator = false;
  // Rotation = 2, puts usbc cable on right.
  // 240 horizontal
  // 135 in Y
  sprite.setSwapBytes(true);
  sprite.setTextDatum(TL_DATUM);
  sprite.fillScreen(TFT_BLACK);
  sprite.fillRect(0, 0, RES_X, RES_Y, TFT_BLACK);

#ifdef OMNI_CONSOLE_MODE
  tftDrawBluetoothLogo(RES_X - 12, 0, 24, consoleIsConnected ? BLUETOOTH_BLUE : TFT_DARKGREY);
#endif

  // Display Step Count (Large, Centered)
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);

  const int topLineHeight = 24;
  const int botLineHeight = 35;
  const int botLineStartY = RES_Y - 35;

  sprite.setTextSize(2);
  sprite.setFreeFont(&AGENCYB22pt7b);
  sprite.setTextDatum(TC_DATUM);

  unsigned long metricValue = getTodaysSteps();
  bool displayMetricLabel = true;
  const char* todayLabel = "Steps Today";
  const char* sessionLabel = "Steps Session";
  const char* metricLabel = todayLabel;
  static uint8_t altToggle = 0;

  switch( primaryDisplayMetric ) {
    case PDM_SESSION_STEPS: 
      metricValue = steps;
      displayMetricLabel = false;
      break;
    case PDM_TODAYS_STEPS:
      // defaults are such that todays steps are already configured.
      break;
    case PDM_ALTERNATE: 
    default:
      if(isTreadmillActive) {
        // Alternate every 3 seconds
        if( (altToggle++ % 6) > 3) {
          metricValue = steps;
          metricLabel = sessionLabel;
        }
      }
      break;
  }

  // PRINT STEPS METRIC - based on config.
  uint8_t stepsHeight = sprite.fontHeight();
  sprite.drawString(String(metricValue), RES_X / 2, topLineHeight);
  sprite.setFreeFont(0);
  sprite.setTextColor(TFT_MAGENTA, TFT_BLACK);
  sprite.setTextSize(2);
  if( displayMetricLabel && metricLabel ) {
    sprite.drawString(String(metricLabel), RES_X / 2, stepsHeight + 1);
  }

  // ADDS Unsync'd session count to bottom right corner.
  sprite.setFreeFont(0);
  sprite.setTextColor(TFT_DARKCYAN, TFT_BLACK);
  sprite.setTextSize(3);
  int sessionsStoredWidth = sprite.textWidth(String(sessionsStored));
  int sessionsStoredHeight = sprite.fontHeight();
  int sessionsStoredX = RES_X - (sessionsStoredWidth - 3);
  int sessionsStoredY = RES_Y - (sessionsStoredHeight + 5);
  sprite.drawString(String(sessionsStored), sessionsStoredX, sessionsStoredY);

  // Displays a red dot in upper right.
  if (isTreadmillActive && recordIndicator) {
    //sprite.fillCircle(10,110+5, 5, TFT_RED); // BOTTOM_LEFT LOCATION
    sprite.fillCircle(5, 5, 5, TFT_RED);  // TOP_RIGHT LOCATION
  }
  recordIndicator = !recordIndicator;

  sprite.pushSprite(0, 0);
}

void tftClockScreen() {
  static bool recordIndicator = false;
  sprite.setSwapBytes(true);
  sprite.setTextDatum(TC_DATUM);
  sprite.fillScreen(TFT_BLACK);
  sprite.fillRect(0, 0, RES_X, RES_Y, TFT_BLACK);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);

  sprite.setTextSize(1);
  sprite.setFreeFont(&AGENCYB22pt7b);
  const int topLineHeight = 20;
  sprite.drawString(getFormattedTime(), RES_X / 2, topLineHeight);
  sprite.setFreeFont(0);
  sprite.setTextSize(2);
  // TODO remove me, this is for debug purposes.
  sprite.drawString(String(neverRecvCIDCount), RES_X - 15, RES_Y - 15);
  sprite.drawString("UTC", RES_X / 2, RES_Y - 20);
  sprite.pushSprite(0, 0);
}


void tftUpdateDisplay() {
  uint8_t choice = tftPage % 5;

  //  Debug.printf("choice = %d, but: %d, bot: %d\n", choice, digitalRead(TOP_BUTTON), digitalRead(BOT_BUTTON) );

  if (!areWifiCredentialsSet) {
    tftWifiStatusScreen(0, "");
  } else if (WiFi.status() != WL_CONNECTED) {
    char tempSsidBuf[32], tempPasswordBuf[32];
    loadWifiCredentialsIntoBuffers(tempSsidBuf, tempPasswordBuf);
    tftWifiStatusScreen(1, tempSsidBuf);
  } else {
    switch (choice) {
      case 0: tftRunningScreen(PDM_ALTERNATE); break;
      case 1: tftRunningScreen(PDM_TODAYS_STEPS); break;
      case 2: tftRunningScreen(PDM_SESSION_STEPS); break;
      case 3: tftSplashScreen(); break;
      case 4: tftClockScreen(); break;
      //case 2: tftWifiStatusScreen(0, ""); break;  // REMOVE
      //case 3: tftWifiStatusScreen(1, ""); break;  // REMOVE
      default:
        tftSplashScreen();
    }
  }
}


void tftPeriodicMainLoopHandler() {
  static unsigned long lastTftUpdate = 0;
  const unsigned long tftUpdateInterval = 1000;  // 1 second

  if (millis() - lastTftUpdate >= tftUpdateInterval) {
    tftUpdateDisplay();
    lastTftUpdate = millis();
  }
}
#endif


// ---------------------------------------------------------------------------
// BLE Server Callbacks
// ---------------------------------------------------------------------------
// Modified Server Callbacks for NimBLE
class MyServerCallbacks : public NimBLEServerCallbacks {
  /**
   * This onConnect is called when the mobile app connects to the main Service
   * There is an onSubscribe which is called when the mobile app subscribes to notifications on the data characteristic.
   */
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    isMobileAppConnected = true;
    haveNotifiedMobileAppOfFirstSession = false;
    Debug.println(">> Mobile app connected!");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    isMobileAppConnected = false;
    isMobileAppSubscribed = false;
    haveNotifiedMobileAppOfFirstSession = false;
    Debug.println(F(">> Mobile app disconnected."));
    delay(500);
    pServer->startAdvertising();
    Debug.println(F(">> Advertising restarted..."));
  }
};

// Characteristic callbacks for the data characteristic
class DataCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    if (subValue == 0) {
      isMobileAppSubscribed = false;
      Debug.println(" << Mobile app unsubscribed");
    } else {
      isMobileAppSubscribed = true;
      Debug.printf("  >> Mobile app SUBSCRIBED, %d\n", subValue);
    }
  }

  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    // Debug.printf("%s : onRead(), value: %s\n",
    //        pCharacteristic->getUUID().toString().c_str(),
    //        pCharacteristic->getValue().c_str());
  }

  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    // Debug.printf("%s : onWrite(), value: %s\n",
    //        pCharacteristic->getUUID().toString().c_str(),
    //        pCharacteristic->getValue().c_str());
  }

  /**
     *  The value returned in code is the NimBLE host return code.
     */
  void onStatus(NimBLECharacteristic* pCharacteristic, int code) override {
    Debug.printf("Notifi/Indi onStatus(), retc: %d, %s\n", code, NimBLEUtils::returnCodeToString(code));
  }
};

// ---------------------------------------------------------------------------
// BLE Confirmation Characteristic Callback
// ---------------------------------------------------------------------------
void indicateNextSession();

// Modified Characteristic Callbacks for NimBLE
class ConfirmCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string rawValue = pCharacteristic->getValue();
    String rxValue = String(rawValue.c_str());

    Debug.print("ConfirmCallback::onWrite got bytes: ");
    for (int i = 0; i < rxValue.length(); i++) {
      Debug.printf("%02X ", rxValue[i]);
    }
    Debug.println();

    if (rxValue.length() > 0 && rxValue[0] == 0x01) {
      indicateNextSession();
    }
  }
};



// ---------------------------------------------------------------------------
// Indicate (Notify) Next Session
// ---------------------------------------------------------------------------
void indicateNextSession() {
  uint32_t totalSessions = getSessionCountFromEEPROM();
  if (currentSessionIndex >= (int)totalSessions) {
    setSessionCountInEEPROM(0);
    EEPROM.commit();
    currentSessionIndex = 0;
    //haveNotifiedMobileAppOfFirstSession = false;

    // Send a done marker
    uint8_t donePayload[1] = { 0xFF };
    dataCharacteristic->setValue(donePayload, 1);
    dataCharacteristic->notify();
    Debug.println("All sessions indicated, cleared/set done marker.");
    return;
  }

  // Read next session from EEPROM
  TreadmillSession s = readSessionFromEEPROM(currentSessionIndex);

  // Prepare 12-byte packet in big-endian
  uint8_t payload[12];
  payload[0] = (s.start >> 24) & 0xFF;
  payload[1] = (s.start >> 16) & 0xFF;
  payload[2] = (s.start >> 8) & 0xFF;
  payload[3] = s.start & 0xFF;
  payload[4] = (s.stop >> 24) & 0xFF;
  payload[5] = (s.stop >> 16) & 0xFF;
  payload[6] = (s.stop >> 8) & 0xFF;
  payload[7] = s.stop & 0xFF;
  payload[8] = (s.steps >> 24) & 0xFF;
  payload[9] = (s.steps >> 16) & 0xFF;
  payload[10] = (s.steps >> 8) & 0xFF;
  payload[11] = s.steps & 0xFF;

  dataCharacteristic->setValue(payload, 12);
  dataCharacteristic->notify();
  Debug.printf(">> Notifying session %d\n", currentSessionIndex);
  currentSessionIndex++;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Debug.println("=== Treadmill Session Tracker (ESP32) ===");

#if LCD_4x20_ENABLED
  Wire.begin();
  Wire.setClock(400000);
  lcd.begin(20, 4, LCD_5x8DOTS);
  lcd.init();
  lcd.clear();
  lcd.backlight();
#endif

#ifdef HAS_TFT_DISPLAY
  tftSetup();
#endif

  // Retro or Omni
#ifdef RETRO_MODE
  Debug.println("RETRO Serial Enabled");
  uart1.begin(4800, SERIAL_8N1, RX1PIN, TX1PIN);
  uart2.begin(4800, SERIAL_8N1, RX2PIN, TX2PIN);
#endif

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  printAllSessionsInEEPROM();

  // WiFi + NTP
  setupWifi();

#ifdef SESSION_SIMULATION_BUTTONS_ENABLED
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CLEAR_PIN, INPUT_PULLUP);

  // Setup button interrupt
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);
#endif

  // Initialize NimBLE
  NimBLEDevice::init("TreadSpan");

  // Get and print the device's Bluetooth MAC address
  Debug.print("Bluetooth MAC Address: ");
  Debug.println(NimBLEDevice::getAddress().toString().c_str());

  // Optional: Set transmission power
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  dataCharacteristic = pService->createCharacteristic(
    BLE_DATA_CHAR_UUID,
    NIMBLE_PROPERTY::NOTIFY);
  dataCharacteristic->setCallbacks(new DataCharacteristicCallbacks());
  // AUTHOR NOTE: The NimBLE documentation regarding notify characteristics doesn't really make any sense to me.
  // Seems like you do not need to do anything special for 2904 stuff the above is sufficient.
  // Uncommenting the code below doesn't hurt anything but... isn't needed.
  /**
    *  2902 and 2904 descriptors are a special case, when createDescriptor is called with
    *  either of those uuid's it will create the associated class with the correct properties
    *  and sizes. However we must cast the returned reference to the correct type as the method
    *  only returns a pointer to the base NimBLEDescriptor class.
    */
  //NimBLE2904* pBeef2904 = dataCharacteristic->create2904();
  //pBeef2904->setFormat(NimBLE2904::FORMAT_UTF8);
  //pBeef2904->setCallbacks(&dscCallbacks);

  confirmCharacteristic = pService->createCharacteristic(
    BLE_CONFIRM_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE);
  confirmCharacteristic->setCallbacks(new ConfirmCallback());

  pService->start();

  // Start advertising (Peripheral)
  // Start advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  //pAdvertising->setScanResponse(true);
  //pAdvertising->setMinPreferred(0x06);
  //pAdvertising->setMaxPreferred(0x12);
  NimBLEDevice::startAdvertising();
  Debug.println("BLE Advertising started...");

#ifdef OMNI_CONSOLE_MODE
  // As a BLE client, we also want to scan for the console
  // We'll do the initial connect attempt here:
  connectToConsoleViaBLE();
#endif

#ifdef INCLUDE_IMPROV_SERIAL
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "My-Device-9a4c2b", FW_VERSION, "TreadSpan");
  improvSerial.onImprovError(onImprovWiFiErrorCb);
  improvSerial.onImprovConnected(onImprovWiFiConnectedCb);
  improvSerial.setCustomConnectWiFi(connectWifi);  // Optional
#endif
  //   TreadmillSession tsession;
  //   tsession.start = 1739826223;
  //   tsession.stop = 1739831820;
  //   tsession.steps = 4856;
  //   recordSessionToEEPROM(tsession);
}

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------
void loop() {

#ifdef INCLUDE_IMPROV_SERIAL
//asd
#warning "handle serial is live"
  improvSerial.handleSerial();
#endif

  // Non-blocking NTP check
  checkNtpUpdate();

#ifdef LCD_4x20_ENABLED
  periodicLcdUpdateMainLoopHandler();
#endif

#ifdef HAS_TFT_DISPLAY
  tftPeriodicMainLoopHandler();
#endif

#ifdef SESSION_SIMULATION_BUTTONS_ENABLED
  // Check button press
  if (buttonPressed) {
    buttonPressed = false;
    simulateNewSession();  // For dev testing only
  }

  // Clear sessions if CLEAR_PIN is LOW (do once)
  if (!clearedSessions && digitalRead(CLEAR_PIN) == LOW) {
    Debug.println("CLEAR_PIN is LOW, clearing all sessions...");
    setSessionCountInEEPROM(0);
    EEPROM.commit();
    clearedSessions = true;
  }
#endif

  if (isMobileAppConnected && isMobileAppSubscribed && !haveNotifiedMobileAppOfFirstSession) {
    haveNotifiedMobileAppOfFirstSession = true;
    currentSessionIndex = 0;
    Debug.println("Mobile App subscribed, sending first session...");
    indicateNextSession();
  }

#ifdef OMNI_CONSOLE_MODE
  consoleBLEMainLoop();
#endif

#ifdef RETRO_MODE
  retroModeMainLoopHandler();
#endif

  delay(1);
}

#ifdef RETRO_MODE
float getSpeedFromCommand(uint8_t*);

void retroModeMainLoopHandler() {
  // Check if data is available on UART1
  while (uart1.available() > 0) {
    char receivedChar = uart1.read();
    // Debug.printf("1>%02X\n", receivedChar);
    uart1Buf[uart1RxCnt % CMD_BUF_SIZE] = receivedChar;
    uart1RxCnt += 1;
    uart1Buffer += String(receivedChar, DEC) + " ";
    if (uart2RxCnt > 0) {
      processResponse();
    }
  }

  // // Check if data is available on UART2
  while (uart2.available() > 0) {
    char receivedChar = uart2.read();
    // Debug.printf("2>%02X\n", receivedChar);
    uart2Buf[uart2RxCnt % CMD_BUF_SIZE] = receivedChar;
    uart2Buffer += String(receivedChar, DEC) + " ";
    uart2RxCnt += 1;
    // Print the UART1 Command
    if (uart1RxCnt > 0) {
      processRequest();
    }
  }
}

void processRequest() {
  if (VERBOSE_LOGGING) {
    Debug.print(getFormattedTime());
    Debug.print(" REQ : ");
    Debug.println(uart1Buffer);
  } else {
    toggleLedColor(BLUE_LED);
  }

  if (uart1Buffer.startsWith(STEPS_STARTSWITH)) {
    lastRequestType = LAST_REQUEST_IS_STEPS;
  } else if (uart1Buffer.startsWith(SPEED_STARTSWITH)) {
    getSpeedFromCommand(uart1Buf);
  } else {
    lastRequestType = 0;
  }

  uart1Buffer = "";  // Clear the buffer
  uart1RxCnt = 0;
}

void processResponse() {
  if (VERBOSE_LOGGING) {
    Debug.print(getFormattedTime());
    Debug.print(" RESP: ");
    Debug.println(uart2Buffer);
  } else {
    toggleLedColor(GREEN_LED);
  }

  if (lastRequestType == LAST_REQUEST_IS_STEPS) {
    steps = uart2Buf[3] * 256 + uart2Buf[4];
    //Debug.print("STEPS: ");
    //Debug.println(steps);
    lastRequestType = 0;
  }
  uart2Buffer = "";  // Clear the buffer
  uart2RxCnt = 0;
}

/**
   * Pass in a command buffer.
   */
float getSpeedFromCommand(uint8_t* buf) {
  if (buf[3] == 10) {
    speedInt = buf[4] * 256 + buf[5];
    if (speedInt == 50) {
      speedFloat = 0;
      if (isTreadmillActive) {
        sessionEndedDetected();
      }
      isTreadmillActive = false;
    } else if (speedInt > 50) {
      speedFloat = estimate_mph(speedInt);
      if (!isTreadmillActive) {
        sessionStartedDetected();
      }
      isTreadmillActive = true;
    }
    return speedFloat;
  }
  return -1;
}
#endif
