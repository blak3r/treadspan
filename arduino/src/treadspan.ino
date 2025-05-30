/*****************************************************************************
 * Treadmill Session Tracker on ESP32
 *
 * History:
 *   2025-03-01   0.9.6 - First release, BLE + TFT
 *   2025-03-15   1.0.7 - HW RTC Options, time setting via mobile app.
 *   2025-03-23   1.1.8 - FTMS Device / Autodetect
 *.  2025-04-06.  1.1.9 - Working FTMS, worked with UREVO E1L
 *   2025-04-08.  1.1.10- NimBLE updated to 2.2.3.
 *   2025-04-19   1.2.11- Platformio layout / UREVO Protocol.
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
#include "TreadmillDevice.h"
#include "HasElapsed.h"


/******************************************************************************************
 *                                      CONFIGURATION
 ******************************************************************************************/

#define FW_VERSION "v1.2.11"

/******************************************************************************************
 * 🏃 TREADMILL MODE SELECTION 🏃
 * Uncomment the mode that matches your treadmill setup.
 ******************************************************************************************/

//#define OMNI_CONSOLE_MODE 1     // 🔵 Use BLE for Sessions (Requires OMNI Console)
//#define RETRO_MODE 1            // 🟢 Use Serial Port for Sessions (Requires special hardware)
//#define FTMS_MODE 1             // Most Common - supports all treadmills which implemented FTMS
#define UREVO_MODE 1            // UREVO's proprietary service that provides step count, uses FTMS control characteristic in tandem.

/******************************************************************************************
 * ⚙️ GENERAL SETTINGS ⚙️
 * Adjust these as needed for debugging, display, and RTC configurations.
 ******************************************************************************************/

#define ENABLE_DEBUG 1                          // 🔍 Allows logs to be printed to Serial
#define VERBOSE_LOGGING 1                       // 🔍 Enable verbose BLE/Serial logging (set to 1 for more logs)
#define HAS_TFT_DISPLAY 1                       // 🖥️ Enable TFT display (LilyGo hardware)
#define LOAD_WIFI_CREDENTIALS_FROM_EEPROM 1     // 📡 Load WiFi credentials from EEPROM
#define INCLUDE_IMPROV_SERIAL 1                 // ⚡ Configure WiFi via Flash Installer (must be disabled when GET_TIME_THROUGH_NTP is undefined)
#define GET_TIME_THROUGH_NTP 1                  // 📡 Enables WIFI to get time via NTP
//#define HAS_RTC_DS3231                        // ⏰ UNCOMMON: Enable support for DS3231 Real-Time Clock (RTC)
//#define SESSION_SIMULATION_BUTTONS_ENABLED 1  // 🕹️ Enable test buttons for session simulation
//#define LCD_4x20_ENABLED 1                    // 🖨️ UNCOMMON: Enable 4x20 I2C LCD screen support

#ifndef LOAD_WIFI_CREDENTIALS_FROM_EEPROM
  const char* ssid = "Angela";
  const char* password = "iloveblake";  // 🔒 Example WiFi credentials for demonstration
#endif

#ifdef SESSION_SIMULATION_BUTTONS_ENABLED
  #define BUTTON_PIN 2  // 🔘 D2 - Press to simulate a treadmill session
  #define CLEAR_PIN 0   // 🔘 D3 - Press to clear all sessions in EEPROM
#endif

/******************************************************************************************
 * 🚨 DO NOT MODIFY BELOW THIS LINE 🚨
 * Internal configurations. Modify at your own risk.
 ******************************************************************************************/

// DEPENDENT LIBRARIES
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

#if !defined(VERBOSE_LOGGING)
  #define VERBOSE_LOGGING 0
#endif

#if defined(GET_TIME_THROUGH_NTP)
  const bool getTimeThroughNtp = true;
#else
  #if defined(INCLUDE_IMPROV_SERIAL)
    #error "You can't have INCLUDE_IMPROV_SERIAL enabled when GET_TIME_THROUGH_NTP is not (you're essentially configuring wifi that isn't enabled)"
  #endif
  const bool getTimeThroughNtp = false;
#endif

#if !defined(HAS_RTC_DS3231) && !defined(GET_TIME_THROUGH_NTP)
  #error "You need to have at least one defined TIME method, NTP or RTC"
#endif

#include "TreadmillDevice.h"
#include "globals.h"

#include "TreadmillDeviceLifespanOmniConsole.h"
#include "TreadmillDeviceLifespanRetroConsole.h"
#include "TreadmillDeviceFTMS.h"
  
#if defined(OMNI_CONSOLE_MODE)
  #include "TreadmillDeviceLifespanOmniConsole.h"
  TreadmillDevice *treadmillDevice =  new TreadmillDeviceLifespanOmniConsole();
#elif defined(RETRO_MODE)
  #include "TreadmillDeviceLifespanRetroConsole.h"
  TreadmillDevice *treadmillDevice =  new TreadmillDeviceLifespanRetroConsole();
#elif defined(FTMS_MODE)
  #include "TreadmillDeviceFTMS.h"
  TreadmillDevice *treadmillDevice =  new TreadmillDeviceFTMS();
#elif defined(UREVO_MODE)
  #include "TreadmillDeviceUrevoProtocol.h"
  TreadmillDevice *treadmillDevice =  new TreadmillDeviceUrevoProtocol();
#else
  #error "You have not selected a TreadmillDevice Implementation."
#endif

#ifdef HAS_RTC_DS3231
  #include "RTClib.h"
  RTC_DS3231 rtc;
  boolean rtcFound = false;
  void setSystemTime(time_t); // forward declaration
  
  void setSystemTimeFromRtc() {
    Debug.printf("Pulling time from RTC");
    DateTime now = rtc.now();
    time_t unixTime = now.unixtime();
    setSystemTime(unixTime);
  }
  void periodicRtcDS3231TimeRetriever() {
    static HasElapsed rtcTimeRetreiverTimer(10*60*1000);
    // Check if 10 minutes have elapsed
    if (rtcFound && rtcTimeRetreiverTimer.isIntervalUp()) {
        setSystemTimeFromRtc();
    }
  }
#endif

// EEPROM Configuration
#define EEPROM_SIZE 512
#define MAX_SSID_LENGTH 32
#define SSID_INDEX 0
#define PASSWORDS_INDEX 32
#define SESSIONS_START_INDEX 64
#define SESSION_SIZE_BYTES sizeof(TreadmillSession)
#define MAX_SESSIONS ((EEPROM_SIZE - (SESSIONS_START_INDEX + 4)) / SESSION_SIZE_BYTES)



#include "./DebugWrapper.h"
DebugWrapper Debug;
bool wasTimeSet = false;

// BLE UUIDs
static const char* BLE_SERVICE_UUID = "0000A51A-12BB-C111-1337-00099AACDEF0";
static const char* BLE_DATA_CHAR_UUID = "0000A51A-12BB-C111-1337-00099AACDEF1";
static const char* BLE_CONFIRM_CHAR_UUID = "0000A51A-12BB-C111-1337-00099AACDEF2";
static const char* BLE_TIME_READ_CHAR_UUID = "0000A51A-12BB-C111-1337-00099AACDEF3";
static const char* BLE_TIME_WRITE_CHAR_UUID = "0000A51A-12BB-C111-1337-00099AACDEF4";

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

// NEW: Pointers to the new time characteristics
NimBLECharacteristic* timeReadCharacteristic = nullptr;   // NEW
NimBLECharacteristic* timeWriteCharacteristic = nullptr;  // NEW

// COMMON STATE VARIABLES (RETRO / OMNI)
uint32_t gSteps = 0;
uint16_t gCalories = 0;         
uint32_t gDistance = 0;
uint32_t gDistanceInMeters = 0;        
bool gIsTreadmillActive = 0;
float gSpeedFloat = 0;
uint16_t gDurationInSecs = 0;

volatile bool gResetRequested = 0;

//int avgSpeedInt = 0;      // only omni console mode.
//float avgSpeedFloat = 0;  // only omni console
//int speedInt = 0;

// NEW: Global variables for tracking today's steps
String lastRecordedDate = "";       //YYYY-MM-DD
unsigned long totalStepsToday = 0;  //
TreadmillSession gCurrentSession;


//------------------- COMMON TIME SETTING --------------------//
String getFormattedTimeYMD();
String getFormattedTimeHMS();
String getFormattedDate();
void tftWifiConnectingScreen(const char*);

/**
 * This is called by NTP Update and by
 * the Mobile App during syncs via the BLE Time Write Characteristic
 */
void setSystemTime( time_t epochTime) {
  #ifdef HAS_RTC_DS3231
    DateTime dt(epochTime);
    if( rtcFound ) {
      rtc.adjust(dt);
    }
  #endif

  struct timeval tv;
  tv.tv_sec = epochTime;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);

  Debug.printf("System time updated: %s %s %lu\n", getFormattedTimeYMD(), getFormattedTimeHMS(), (uint32_t) epochTime );

  wasTimeSet = true;
}

// ---------------------------------------------------------------------------
// Wifi / NTP
// ---------------------------------------------------------------------------
#ifdef GET_TIME_THROUGH_NTP
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

    Debug.printf("Loaded SSID: %s\n", ssid);
    Debug.printf("Loaded Password: %s\n", password);
  }

  bool loadWifiCredentialsIntoBuffers(char* ssidBuf, char* passwordBuf) {
    #if LOAD_WIFI_CREDENTIALS_FROM_EEPROM
      loadWiFiCredentialsFromEEPROM(ssidBuf, passwordBuf);
    #else
      strncpy(ssidBuf, ssid, sizeof(ssidBuf));  // Developer mode... ssid/password are hardcoded.
      strncpy(passwordBuf, password, sizeof(ssidBuf));
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

  void reconnectWifiIfNecessary() {
    static HasElapsed reconnectWifiTimer(30000);

    if( WiFi.status() != WL_CONNECTED && areWifiCredentialsSet ) {
      if( reconnectWifiTimer.isIntervalUp() ) {
        Debug.printf("Int is up\n");
        Debug.printf("Lost WiFi connection, attempting to reconnect...\n");
        char ssidBuf[32], passBuf[32];
        loadWifiCredentialsIntoBuffers(ssidBuf, passBuf);
        connectWifi(ssidBuf, passBuf);
      }
    } else {
      reconnectWifiTimer.reset(); // reset it back to 30 so we don't immediately retry after losing connection.
    }
  }

  void sendNtpRequest(); // forward declaration

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
  volatile unsigned long lastNtpRequest = 0;
  volatile unsigned long ntpUpdateInterval = 60*1000;  // eventually 600000 (10 mins), but start short for quick sync

  void sendNtpRequest() {
    Debug.printf("Sending NTP request...\n");

    memset(ntpPacketBuffer, 0, NTP_PACKET_SIZE);
    ntpPacketBuffer[0] = 0b11100011;  // LI, Version, Mode
    ntpPacketBuffer[1] = 0;           // Stratum
    ntpPacketBuffer[2] = 6;           // Polling Interval
    ntpPacketBuffer[3] = 0xEC;        // Peer Clock Precision

    // Send packet to NTP server
    ntpUDP.beginPacket(ntpServer, ntpPort);
    ntpUDP.write(ntpPacketBuffer, NTP_PACKET_SIZE);
    ntpUDP.endPacket();

   // configTime(0, 0, "pool.ntp.org");
    lastNtpRequest = millis();
  }

  void checkNtpResponse() {
    int packetSize = ntpUDP.parsePacket();
    if (packetSize >= NTP_PACKET_SIZE) {
      Debug.printf("NTP response received! size=%d\n", packetSize);

      ntpUDP.read(ntpPacketBuffer, NTP_PACKET_SIZE);
      ntpUDP.flush();
      unsigned long highWord = word(ntpPacketBuffer[40], ntpPacketBuffer[41]);
      unsigned long lowWord = word(ntpPacketBuffer[42], ntpPacketBuffer[43]);
      
      time_t epochTime = (highWord << 16 | lowWord) - 2208988800UL;  // Convert to UNIX time

      // Right after ntpUDP.read(ntpPacketBuffer, NTP_PACKET_SIZE);
      Debug.println("=== RAW NTP DATA ===");
      for (int i = 0; i < NTP_PACKET_SIZE; i++) {
        Debug.printf_noTs("%02X ", ntpPacketBuffer[i]);
      }
      Debug.println("\n====================");

      if( (uint32_t) epochTime == 0 || (uint32_t) epochTime == (uint32_t) 0xFFFFFFFF) {
        Debug.printf("Invalid packet received! %X", epochTime);
        return;
      }

      ntpUpdateInterval = (unsigned long) 60*10*1000;  // set to 10 minutes after first success
      setSystemTime(epochTime);
    }
  }

  void periodicNtpUpdateMainLoopHandler() {
    if (WiFi.status() == WL_CONNECTED) {
      if ((millis() - lastNtpRequest) >= ntpUpdateInterval) {
        ntpUpdateInterval = 10*60*1000;
        sendNtpRequest();
        delay(100);
      }
      else {
        checkNtpResponse();
      }
    }
    reconnectWifiIfNecessary();
  }
#endif // GET_TIME_THROUGH_NTP


String getFormattedTimeHMS() {
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

String getFormattedTimeYMD() {
  time_t now = time(nullptr);
  if (now < 100000) {
    return "???";
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
  return String(buffer);
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

  if (gIsTreadmillActive) {
    return totalStepsToday + gSteps;
  }

  return totalStepsToday;
}

// ---------------------------------------------------------------------------
// Session Start/End
// ---------------------------------------------------------------------------
void sessionStartedDetected() {
  Debug.printf("%s >> NEW SESSION Started!\n", getFormattedTimeHMS().c_str());
  gIsTreadmillActive = true;
  gCurrentSession.start = (uint32_t)time(nullptr);
}

void sessionEndedDetected() {
  gIsTreadmillActive = false;
  gCurrentSession.stop = (uint32_t)time(nullptr);
  gCurrentSession.steps = gSteps;

  if (gCurrentSession.steps > 50000) {
    Debug.println("ERROR: Session steps too large, skipping save.");
    return;
  }
  if (!gCurrentSession.start) {
    Debug.println("ERROR: Session had no start time, skipping save.");
    return;
  }

  Debug.printf("<< NEW SESSION Ended!\n");
  printSessionDetails(gCurrentSession, sessionsStored);
  recordSessionToEEPROM(gCurrentSession);
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
  if (gCurrentSession.start == 0 || now < gCurrentSession.start) {
    return "00:00:00";
  }
  uint32_t elapsed = now - gCurrentSession.start;
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
    Debug.printf("Clearing LCD, consIsConn: %d, isMobAppConn: %d, isMobSubs:%\n", treadmillDevice->isConnected(), isMobileAppConnected, isMobileAppSubscribed);
    lcd.clear();  // Causes additional blocking i didn't want in the Serial mode.
  }
#endif

  lcd.setCursor(0, 0);
  lcd.printf("TreadSpan %s ", FW_VERSION);
  lcdPrintBoolIndicator(treadmillDevice->isConnected());
  lcdPrintBoolIndicator(isMobileAppConnected);
  lcdPrintBoolIndicator(isMobileAppSubscribed);

  lcd.setCursor(0, 1);
  if (pageStyle < 2) {
    lcd.printf("Steps Today: %7lu", getTodaysSteps());
  } else {
    lcd.printf("Sessions To Sync: %2d", sessionsStored);
  }
  pageStyle = (pageStyle + 1) % 4;

  if (gIsTreadmillActive) {
    lcd.setCursor(0, 2);
    lcd.printf("%s %s   ", getFormattedTimeHMS().c_str(), getCurrentSessionElapsed().c_str());
    lcd.setCursor(0, 3);
    lcd.printf("Steps:%4d MPH: %.1f", gSteps, gSpeedFloat);
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

void tftUpdateDisplay();
void tftWifiConnectingScreen(const char*);


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
    //gResetRequested = true; // Likely temporary...
  }
}

void tftSetup() {
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

#ifdef GET_TIME_THROUGH_NTP
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
#else
void tftWifiConnectingScreen(const char* ssid) {
  // If GET_TIME_THROUGH_NTP is not defined, we can just leave this empty or do something minimal.
  sprite.setSwapBytes(true);
  sprite.fillScreen(TFT_BLACK);
  sprite.fillRect(0, 0, RES_X, RES_Y, TFT_BLACK);
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  sprite.drawString("NTP not enabled", 10, 10);
  sprite.pushSprite(0, 0);
}
#endif

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

  //Debug.printf("fullX=%d, centerX=%d, y1=%d, y2=%d, fullY=%d, 0x%04X\n", fullX, centerX, y1, y2, fullY, color);
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

  tftDrawBluetoothLogo(RES_X - 12, 0, 24, treadmillDevice->isConnected() ? BLUETOOTH_BLUE : TFT_DARKGREY);

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
      metricValue = gSteps;
      displayMetricLabel = false;
      break;
    case PDM_TODAYS_STEPS:
      // defaults are such that todays steps are already configured.
      break;
    case PDM_ALTERNATE:
    default:
      if(gIsTreadmillActive) {
        // Alternate every 3 seconds
        if( (altToggle++ % 6) > 3) {
          metricValue = gSteps;
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
  if (gIsTreadmillActive && recordIndicator) {
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
  sprite.drawString(getFormattedTimeHMS(), RES_X / 2, topLineHeight);
  sprite.setFreeFont(0);
  sprite.setTextSize(2);
  sprite.drawString(getFormattedTimeYMD(), RES_X / 2, RES_Y - 60);
  // TODO remove me, this is for debug purposes.
 // sprite.drawString(String(neverRecvCIDCount), RES_X - 15, RES_Y - 15);
  sprite.drawString("Pacific Time", RES_X / 2, RES_Y - 20);
  sprite.pushSprite(0, 0);
}


void tftUpdateDisplay() {
  uint8_t choice = tftPage % 5;

  //  Debug.printf("choice = %d, but: %d, bot: %d\n", choice, digitalRead(TOP_BUTTON), digitalRead(BOT_BUTTON) );

  if ( getTimeThroughNtp && !areWifiCredentialsSet) {
    tftWifiStatusScreen(0, "");
  } else if ( getTimeThroughNtp && WiFi.status() != WL_CONNECTED ) {
    char tempSsidBuf[32], tempPasswordBuf[32];
    #ifdef GET_TIME_THROUGH_NTP
      loadWifiCredentialsIntoBuffers(tempSsidBuf, tempPasswordBuf);
    #else
      strncpy(tempSsidBuf, "NoNTP", 31);
      strncpy(tempPasswordBuf, "NoNTP", 31);
    #endif
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

// Callback for the confirmation characteristic
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

// NEW: Callback for reading the current system time
class TimeReadCallbacks : public NimBLECharacteristicCallbacks { // NEW
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Debug.printf("Entered timeRead\n");
    time_t nowSec = time(nullptr);
    uint32_t now32 = (uint32_t) nowSec;
    // Convert to big-endian
    uint8_t buffer[4];
    buffer[0] = (uint8_t)((now32 >> 24) & 0xFF);
    buffer[1] = (uint8_t)((now32 >> 16) & 0xFF);
    buffer[2] = (uint8_t)((now32 >> 8) & 0xFF);
    buffer[3] = (uint8_t)( now32 & 0xFF);
    pCharacteristic->setValue(buffer, 4);
  }
};

// NEW: Callback for writing the current system time
class TimeWriteCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Debug.printf("Entered timeWrite\n");
    std::string rawValue = pCharacteristic->getValue();
    if (rawValue.size() == 4) {
      Debug.printf("Entered timeWrite2: %s\n", rawValue);
      time_t newTime = ((uint8_t)rawValue[0] << 24) |
                       ((uint8_t)rawValue[1] << 16) |
                       ((uint8_t)rawValue[2] << 8)  |
                       ((uint8_t)rawValue[3]);
      setSystemTime(newTime);
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

  #ifdef HAS_RTC_DS3231
    if (!rtc.begin()) {
      rtcFound = true;
      Debug.println("RTC not found!");
    } else {
      rtcFound = true;
      Debug.println("RTC initialized.");
      setSystemTimeFromRtc();
    }
  #endif

  #ifdef HAS_TFT_DISPLAY
    tftSetup();
  #endif

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  printAllSessionsInEEPROM();

  #ifdef GET_TIME_THROUGH_NTP
    // WiFi + NTP
    setupWifi();
  #endif

  #ifdef SESSION_SIMULATION_BUTTONS_ENABLED
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(CLEAR_PIN, INPUT_PULLUP);
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

  confirmCharacteristic = pService->createCharacteristic(
    BLE_CONFIRM_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE);
  confirmCharacteristic->setCallbacks(new ConfirmCallback());

  // NEW: Create the Time-Read characteristic
  timeReadCharacteristic = pService->createCharacteristic(
    BLE_TIME_READ_CHAR_UUID,
    NIMBLE_PROPERTY::READ
  );
  timeReadCharacteristic->setCallbacks(new TimeReadCallbacks());

  // NEW: Create the Time-Write characteristic
  timeWriteCharacteristic = pService->createCharacteristic(
    BLE_TIME_WRITE_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  timeWriteCharacteristic->setCallbacks(new TimeWriteCallbacks());

  pService->start();

  // Start advertising (Peripheral)
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  NimBLEDevice::startAdvertising();
  Debug.println("BLE Advertising started...");

  treadmillDevice->setupHandler();

  #ifdef INCLUDE_IMPROV_SERIAL
    improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, NimBLEDevice::getAddress().toString().c_str(), FW_VERSION, "TreadSpan");
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
    improvSerial.handleSerial();
  #endif

  #ifdef GET_TIME_THROUGH_NTP
    periodicNtpUpdateMainLoopHandler();

        // Simple reconnect logic if WiFi credentials exist but WiFi is lost
    // static unsigned long lastWifiCheck = 0;
    // const unsigned long wifiCheckInterval = 10000; // 10 seconds
    // if (millis() - lastWifiCheck >= wifiCheckInterval) {
    //   lastWifiCheck = millis();
    //   if (areWifiCredentialsSet && (WiFi.status() != WL_CONNECTED)) {
    //     Debug.println("Lost WiFi connection, attempting to reconnect...");
    //     char ssidBuf[32], passBuf[32];
    //     loadWifiCredentialsIntoBuffers(ssidBuf, passBuf);
    //     connectWifi(ssidBuf, passBuf);
    //   }
   // }
  #endif

  #ifdef HAS_RTC_DS3231
    periodicRtcDS3231TimeRetriever();
  #endif

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
      Debug.printf("CLEAR_PIN is LOW, clearing all sessions...\n");
      setSessionCountInEEPROM(0);
      EEPROM.commit();
      clearedSessions = true;
    }
  #endif

  if (isMobileAppConnected && isMobileAppSubscribed && !haveNotifiedMobileAppOfFirstSession) {
    haveNotifiedMobileAppOfFirstSession = true;
    currentSessionIndex = 0;
    Debug.printf("Mobile App subscribed, sending first session...\n");
    indicateNextSession();
  }

  treadmillDevice->loopHandler();

  delay(1);
}
