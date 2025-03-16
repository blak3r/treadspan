#pragma once

#include <NimBLEDevice.h>
#include "globals.h"
#include "TreadmillDevice.h"
#include "HasElapsed.h"

/**
 * Simple helper to estimate miles-per-hour from the integer “speed” value.
 * In your original code, you used "0.00435 * value - 0.009".
 */
inline float estimate_mph(int value) {
    return (0.00435f * value) - 0.009f;
}

// ---------------------------------------------------------------------------
// TreadmillDeviceLifespanOmniConsole
// ---------------------------------------------------------------------------
class TreadmillDeviceLifespanOmniConsole : public TreadmillDevice {
  public:
    TreadmillDeviceLifespanOmniConsole() : connectionRetryTimer(5000) {} // ✅ Initialize here
    virtual ~TreadmillDeviceLifespanOmniConsole() {}

    /**
     * Called once from setup()
     */
    void setupHandler() override {
      // No setup needed for Bluetooth Low Energy. 
      // Used by other implementations such as Retro Console which needs to configure Hardware Uarts
    }

    /**
     * Called repeatedly from main loop(), 
     * You need to handle reconnecting as well as getting actual data from device.
     */
    void loopHandler() override {
      if (!consoleIsConnected) {
        connectionStateMachine();
      } else {
        sendNextOpcodeIfAppropriate();
      }
    }

    bool isConnected() override {
      return consoleIsConnected;
    }

private:
    // -----------------------------------------------------------------------
    // Constants / OpCodes
    // -----------------------------------------------------------------------
    static constexpr const char* CONSOLE_NAME_PREFIX = "LifeSpan-TM";
    static constexpr const char* CONSOLE_SERVICE_UUID   = "0000fff0-0000-1000-8000-00805f9b34fb";
    static constexpr const char* CONSOLE_CHAR_UUID_FFF1 = "0000fff1-0000-1000-8000-00805f9b34fb"; 
    static constexpr const char* CONSOLE_CHAR_UUID_FFF2 = "0000fff2-0000-1000-8000-00805f9b34fb";

    static constexpr uint8_t OPCODE_STEPS     = 0x88;
    static constexpr uint8_t OPCODE_DURATION  = 0x89;
    static constexpr uint8_t OPCODE_STATUS    = 0x91;
    static constexpr uint8_t OPCODE_DISTANCE  = 0x85;
    static constexpr uint8_t OPCODE_CALORIES  = 0x87;
    static constexpr uint8_t OPCODE_SPEED     = 0x82;

    // We'll request these opcodes in a round-robin fashion, we request STEPS and STATUS
    // more frequently so we're more responsive 
    static constexpr uint8_t consoleCommandOrder[10] = {
        OPCODE_STEPS,    OPCODE_STATUS,   OPCODE_DURATION, OPCODE_STATUS,   OPCODE_DISTANCE,
        OPCODE_STEPS,    OPCODE_STATUS,   OPCODE_CALORIES, OPCODE_STATUS,   OPCODE_SPEED
    };
    static const int CONSOLE_COMMAND_COUNT = 10;

    // BLE scanning results
    NimBLEAddress foundConsoleAddress;
    bool foundConsole = false;

    // BLE client references for the console
    NimBLEClient* consoleClient = nullptr;
    NimBLERemoteCharacteristic* consoleNotifyCharacteristic = nullptr;
    NimBLERemoteCharacteristic* consoleWriteCharacteristic = nullptr;

    bool consoleIsConnected = false;
    int consoleCommandIndex = 0;
    unsigned long lastConsoleCommandSentAt = 0;
    const unsigned long consoleCommandUpdateIntervalMin = 300;   // minimal delay
    const unsigned long consoleCommandUpdateIntervalMax = 1400;  // fallback if no response

    uint8_t  lastConsoleCommandIndex = 0;
    uint8_t  lastConsoleCommandOpcode = 0;
    bool     commandResponseReceived = true;
    uint8_t  neverRecvCIDCount = 0;
    uint8_t  timesSessionStatusHasBeenTheSame = 0;
    int8_t   lastSessionStatus = -1;

private:
    // We'll store a single global pointer (singleton approach), so we can 
    // reference 'this' inside the static callback:
    static TreadmillDeviceLifespanOmniConsole* globalInstance;
    HasElapsed connectionRetryTimer;

    // -----------------------------------------------------------------------
    // Connection Step 0: Orchestrates everything.
    // 1. Starts Scans
    // 2. If found, we connect
    // -----------------------------------------------------------------------
    void connectionStateMachine() {
      if (connectionRetryTimer.isIntervalUp()) {
        if (!foundConsole) {
          startScanForBLEPeripherals();
        }
        else {
          // Only connect if scanning has completely stopped (probably not needed)
          if (!NimBLEDevice::getScan()->isScanning()) {
            Debug.printf("Scan complete, attempting to connect...\n");
            connectToFoundConsole();
          } else {
            Debug.println("Waiting for scan to finish...");
          }
        }
      }
    }

    // -----------------------------------------------------------------------
    // Connection Step 1: Start a Scan, set callbacks for found devices
    // -----------------------------------------------------------------------
    void startScanForBLEPeripherals() {
      Debug.printf("Scanning for LifeSpan Omni Console...\n");
      foundConsole = false;
      NimBLEDevice::getScan()->setScanCallbacks(&mScanCallbacks, false);
      NimBLEDevice::getScan()->start(3000, false, true);
    }

    // -----------------------------------------------------------------------
    // Connection Step 2: 
    // For each device found, see if it matches by device name
    // If found, set foundConsole and foundConsoleAddress.
    // -----------------------------------------------------------------------
    class InternalScanCallbacks : public NimBLEScanCallbacks {
      public:
        InternalScanCallbacks(TreadmillDeviceLifespanOmniConsole* parent) : mParent(parent) {}
        void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
          if (VERBOSE_LOGGING) {
              Debug.printf("Advertised Device: %s\n", 
                  advertisedDevice->toString().c_str());
          }
          if (advertisedDevice->haveName()) {
              std::string devName = advertisedDevice->getName();
              if (devName.rfind(CONSOLE_NAME_PREFIX, 0) == 0) {
                  Debug.printf("Found 'LifeSpan-TM' at %s\n", 
                      advertisedDevice->getAddress().toString().c_str());
                  // Mark found so we can connect
                  NimBLEDevice::getScan()->stop();
                  mParent->foundConsoleAddress = advertisedDevice->getAddress();
                  mParent->foundConsole = true;

                  // IMPORTANT: I do not know why... but trying to connect immediately while the scan
                  // is happening does not work.  Causes all kinds of issues such as onScanEnd never being triggered.
                  // unhandled exceptions, etc.  I even tried delay(100);
                  // mParent->connectToFoundConsole();
                  mParent->connectionRetryTimer.runNextTimeIn(100); // not required, makes it try to connect quicker than 5s.
              }
          }
        }
        void onScanEnd(const NimBLEScanResults& results, int reason) override {
          Debug.printf("BLE Scan Ended, reason: %d, devices seen: %d\n", reason, results.getCount());
        }
    private:
        TreadmillDeviceLifespanOmniConsole* mParent;
    } mScanCallbacks{this};

    // -----------------------------------------------------------------------
    // Connection Step 3: 
    // Now that we've found the device, we connect and subscribe to notifications
    // We setup some callbacks, onConsoleNotify that is called if subscribe success
    // -----------------------------------------------------------------------
    void connectToFoundConsole() {
      foundConsole = false; consoleIsConnected = false; //mark them as fails, gets fixed if makes till end.
      consoleClient = NimBLEDevice::createClient();
      consoleClient->setClientCallbacks(&mClientCallbacks);

      Debug.printf("Attempting to connect to: %s\n", foundConsoleAddress.toString().c_str());
      if (!consoleClient->connect(foundConsoleAddress)) {
        Debug.printf("Failed to connect to LifeSpan console.\n");
        return;
      }

      Debug.printf("Connected to Omni Console. Discovering services..\n");
      NimBLERemoteService* service = consoleClient->getService(CONSOLE_SERVICE_UUID);
      if (!service) {
        Debug.printf("Failed to find FFF0 service. Disconnecting...\n");
        consoleClient->disconnect();
        return;
      }

      // FFF1 = notify
      consoleNotifyCharacteristic = service->getCharacteristic(CONSOLE_CHAR_UUID_FFF1);
      if (!consoleNotifyCharacteristic) {
        Debug.println("Failed to find FFF1 char. Disconnecting...");
        consoleClient->disconnect();
        return;
      }
      // We attach ourselves as a global instance for the callback
      globalInstance = this;

      if (consoleNotifyCharacteristic->canNotify()) {
        consoleNotifyCharacteristic->subscribe(true, onConsoleNotify);
        Debug.printf("Subbed to notifications on FFF1.\n");
      }

      // FFF2 = write
      consoleWriteCharacteristic = service->getCharacteristic(CONSOLE_CHAR_UUID_FFF2);
      if (!consoleWriteCharacteristic || !consoleWriteCharacteristic->canWrite()) {
        Debug.printf("FFF2 characteristic not found or not writable.\n");
        consoleClient->disconnect();
        return;
      }

      consoleIsConnected = true;
      foundConsole = true;
    }

    // -----------------------------------------------------------------------
    // Connection Step 4: 
    // Upon successful characteristic subscription, we set mParent->consoleIsConnected
    // We also setup callbacks in case we get disconnected.
    // -----------------------------------------------------------------------
    class InternalClientCallback : public NimBLEClientCallbacks {
      public:
        InternalClientCallback(TreadmillDeviceLifespanOmniConsole* parent) : mParent(parent) {}
        void onConnect(NimBLEClient* pclient) override {
            Debug.printf("Console client connected.\n");
            mParent->consoleIsConnected = true;
        }
        void onDisconnect(NimBLEClient* pclient, int reason) override {
            Debug.printf("!!! Console client disconnected.\n");
            mParent->consoleIsConnected = false;
        }
      private:
        TreadmillDeviceLifespanOmniConsole* mParent;
    } mClientCallbacks{this};


    // -----------------------------------------------------------------------
    // GETTING STEP DATA FROM THE TREADMILL
    // -----------------------------------------------------------------------
    // 
    // To get data from the Omni Console. You follow this procedure.
    // 1. Subscribe to the notification characteristic (FFF1) (see: connectToFoundConsole)
    // 2. Write a command payload to the WRITE Characteristic (FFF2). (see: requestDataFromOmniConsole)
    // 3. Receive data on the notification callback (see: onConsoleNotify)
    // -----------------------------------------------------------------------

  void sendNextOpcodeIfAppropriate() {
      uint32_t millisSinceLast = millis() - lastConsoleCommandSentAt;
      bool canSend = (commandResponseReceived && (millisSinceLast >= consoleCommandUpdateIntervalMin));
      bool forcedSend = (millisSinceLast >= consoleCommandUpdateIntervalMax);

      if (canSend || forcedSend) {
        lastConsoleCommandSentAt = millis();

        uint8_t opcode = consoleCommandOrder[consoleCommandIndex];
        uint8_t consoleCmdBuf[6] = { 0xA1, opcode, 0x00, 0x00, 0x00, 0x00 };

        if (!commandResponseReceived) {
            Debug.printf("ERROR: No response from opcode 0x%02X\n", lastConsoleCommandOpcode);
            neverRecvCIDCount++;
        }

        Debug.printf("Sending opcode 0x%02X (idx=%d)\n", opcode, consoleCommandIndex);
        consoleWriteCharacteristic->writeValue((const uint8_t*)consoleCmdBuf, sizeof(consoleCmdBuf));

        lastConsoleCommandIndex  = consoleCommandIndex;
        lastConsoleCommandOpcode = opcode;
        commandResponseReceived  = false;
        consoleCommandIndex = (consoleCommandIndex + 1) % CONSOLE_COMMAND_COUNT;
      }
    }

    static void onConsoleNotify(
        NimBLERemoteCharacteristic* pCharacteristic,
        uint8_t* data, size_t length, bool isNotify) 
    {
        if (!globalInstance) {
            return;
        }
        globalInstance->handleConsoleNotification(data, length); // allow us to get access to class variables.
    }

    void handleConsoleNotification(uint8_t* data, size_t length) {
      if (VERBOSE_LOGGING) {
          Debug.printf("RESP %02X: ", lastConsoleCommandIndex);
          for (size_t i = 0; i < length; i++) {
              Debug.printf("%02X ", data[i]);
          }
          Debug.println("");
      }

      // Parse the response
      switch (lastConsoleCommandOpcode) {
      case OPCODE_STEPS:
          steps = data[2] * 256 + data[3];
          Debug.printf("Steps: %d\n", steps);
          break;

      case OPCODE_CALORIES:
          // e.g. int calories = data[2] * 256 + data[3];
          // Debug.printf("Calories: %d\n", calories);
          break;

      case OPCODE_DISTANCE:
          // e.g. int distance = data[2] * 256 + data[3];
          // Debug.printf("Distance: %d\n", distance);
          break;

      case OPCODE_SPEED: {
          int avgSpeedInt = data[2] * 256 + data[3];
          float avgSpeedFloat = estimate_mph(avgSpeedInt);
          Debug.printf("Avg Speed: %d => %.1f MPH\n", avgSpeedInt, avgSpeedFloat);
          break;
      }
      case OPCODE_DURATION:
          Debug.printf("DURATION: %d:%d:%d\n", data[2], data[3], data[4]);
          // If you want to adjust session start time, do so here.
          break;

      case OPCODE_STATUS: {
          uint8_t status = data[2];
          if (data[3] || data[4]) {
              // Possibly an invalid status response
              commandResponseReceived = true;
              return;
          }
          // See your original code:
          #define STATUS_RUNNING 3
          #define STATUS_PAUSED 5
          #define STATUS_SUMMARY_SCREEN 4
          #define STATUS_STANDBY 1

          // In order to improve reliability of session start/stop detection, we ensure we get the same value
          // at least twice.  It's not uncommon to miss command or get the wrong response.  I found without this
          // we were detecting sessions ending early resulting in lots of duplicate overlapping sessions.
          if (lastSessionStatus == status) {
              timesSessionStatusHasBeenTheSame++;
          } else {
              timesSessionStatusHasBeenTheSame = 0;
          }
          lastSessionStatus = status;

          switch (status) {
          case STATUS_RUNNING:
              Debug.println("Treadmill: RUNNING");
              if (!isTreadmillActive && timesSessionStatusHasBeenTheSame >= 1) {
                  sessionStartedDetected();
              }
              break;
          case STATUS_PAUSED:
          case STATUS_SUMMARY_SCREEN:
          case STATUS_STANDBY:
              Debug.printf("Treadmill: %s\n", 
                  (status==STATUS_PAUSED ? "PAUSED" :
                  (status==STATUS_SUMMARY_SCREEN ? "SUMMARY_SCREEN" : "STANDBY")));
              if (isTreadmillActive && timesSessionStatusHasBeenTheSame >= 1) {
                  sessionEndedDetected();
              }
              break;
          default:
              Debug.printf("Unknown status: %d\n", status);
          }
          break;
      }
      default:
          break;
      }
      commandResponseReceived = true;
    }

    void requestDataFromOmniConsole() {

      // Most commands come back in 300ms, but sometimes it just randomly takes longer.
      // Rather then solving for the slowest case which, I try more often.
      // If we get to the max interval, then we assume the command got lost in transit.
      static HasElapsed minUpdateInterval(consoleCommandUpdateIntervalMin);
      static HasElapsed maxUpdateInterval(consoleCommandUpdateIntervalMax);
      
      uint32_t millisSinceLast = millis() - lastConsoleCommandSentAt;
      bool canSend = (commandResponseReceived && minUpdateInterval.isIntervalUp());
      bool forcedSend = maxUpdateInterval.isIntervalUp();

      if (canSend || forcedSend) {
        lastConsoleCommandSentAt = millis();

        uint8_t opcode = consoleCommandOrder[consoleCommandIndex];
        uint8_t consoleCmdBuf[6] = { 0xA1, opcode, 0x00, 0x00, 0x00, 0x00 };

        if (!commandResponseReceived) {
          Debug.printf("ERROR: No response from opcode 0x%02X\n", lastConsoleCommandOpcode);
          neverRecvCIDCount++;
        }

        Debug.printf("Sending opcode 0x%02X (idx=%d)\n", opcode, consoleCommandIndex);
        consoleWriteCharacteristic->writeValue((const uint8_t*)consoleCmdBuf, sizeof(consoleCmdBuf));

        lastConsoleCommandIndex  = consoleCommandIndex;
        lastConsoleCommandOpcode = opcode;
        commandResponseReceived  = false;

        consoleCommandIndex = (consoleCommandIndex + 1) % CONSOLE_COMMAND_COUNT;
      }
    }
};

// Initialize the static pointer to null
TreadmillDeviceLifespanOmniConsole* TreadmillDeviceLifespanOmniConsole::globalInstance = nullptr;
