#pragma once

#include <NimBLEDevice.h>
#include "globals.h"
#include "TreadmillDevice.h"

/**
 * Simple helper to estimate miles-per-hour from the integer “speed” value.
 * In your original code, you used "0.00435 * value - 0.009".
 */
inline float estimate_mph(int value) {
    return (0.00435f * value) - 0.009f;
}

// ---------------------------------------------------------------------------
// LifespanOmniConsoleTreadmillDevice
// ---------------------------------------------------------------------------
class LifespanOmniConsoleTreadmillDevice : public TreadmillDevice {
public:
    LifespanOmniConsoleTreadmillDevice() = default;
    virtual ~LifespanOmniConsoleTreadmillDevice() {}

    /**
     * Called once from setup()
     */
    void setupHandler() override {
      // No setup needed.
    }

    /**
     * Called repeatedly from main loop(), it should handle reconnecting
     */
    void loopHandler() override {
        // If console is not connected, try to reconnect periodically
          if (!consoleIsConnected) {
            static unsigned long lastTry = 0;
            if (millis() - lastTry > 5000) {
              lastTry = millis();
              connectToConsoleViaBLE();
            }
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
    // -----------------------------------------------------------------------
    // 1) Scanning Callback
    // -----------------------------------------------------------------------
    class InternalScanCallbacks : public NimBLEScanCallbacks {
    public:
        InternalScanCallbacks(LifespanOmniConsoleTreadmillDevice* parent) : mParent(parent) {}
        
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
                }
            }
        }

        void onScanEnd(const NimBLEScanResults& results, int reason) override {
            if (VERBOSE_LOGGING) {
                Debug.printf("Scan Ended, reason: %d, devCount: %d\n", 
                                      reason, results.getCount());
            }
            // Optionally restart scanning 
            NimBLEDevice::getScan()->start(1000, false, true);
        }

    private:
        LifespanOmniConsoleTreadmillDevice* mParent;
    } mScanCallbacks{this};

    // -----------------------------------------------------------------------
    // 2) BLE Client Callback
    // -----------------------------------------------------------------------
    class InternalClientCallback : public NimBLEClientCallbacks {
    public:
        InternalClientCallback(LifespanOmniConsoleTreadmillDevice* parent) : mParent(parent) {}

        void onConnect(NimBLEClient* pclient) override {
            Debug.println("Console client connected.");
            mParent->consoleIsConnected = true;
        }

        void onDisconnect(NimBLEClient* pclient, int reason) override {
            Debug.println("!!! Console client disconnected.");
            mParent->consoleIsConnected = false;
        }

    private:
        LifespanOmniConsoleTreadmillDevice* mParent;
    } mClientCallbacks{this};

    // -----------------------------------------------------------------------
    // 3) Notification Callback from the console
    // -----------------------------------------------------------------------
    static void onConsoleNotify(
        NimBLERemoteCharacteristic* pCharacteristic,
        uint8_t* data, size_t length, bool isNotify) 
    {
        // We need to figure out which LifespanOmniConsoleTreadmillDevice instance. 
        // For simplicity, assume only one instance is used (common in Arduino).
       // LifespanOmniConsoleTreadmillDevice* self = (LifespanOmniConsoleTreadmillDevice*)pCharacteristic->getServer();
        // But NimBLE doesn't always store that. We'll keep a singleton approach or pass in user data. 
        // For a simpler route, we'll just use a static pointer:
        if (!globalInstance) {
            return;
        }
        globalInstance->handleConsoleNotification(data, length);
    }

    // We'll store a single global pointer (singleton approach), so we can 
    // reference 'this' inside the static callback:
    static LifespanOmniConsoleTreadmillDevice* globalInstance;

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

    // -----------------------------------------------------------------------
    // Implementation Helpers
    // -----------------------------------------------------------------------
    void connectToFoundConsole() {
        consoleClient = NimBLEDevice::createClient();
        consoleClient->setClientCallbacks(&mClientCallbacks);

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
        if (!service) {
            Debug.println("Failed to find FFF0 service. Disconnecting...");
            consoleClient->disconnect();
            consoleIsConnected = false;
            return;
        }

        // FFF1 = notify
        consoleNotifyCharacteristic = service->getCharacteristic(CONSOLE_CHAR_UUID_FFF1);
        if (!consoleNotifyCharacteristic) {
            Debug.println("Failed to find FFF1 char. Disconnecting...");
            consoleClient->disconnect();
            consoleIsConnected = false;
            return;
        }
        // We attach ourselves as a global instance for the callback
        globalInstance = this;

        if (consoleNotifyCharacteristic->canNotify()) {
            consoleNotifyCharacteristic->subscribe(true, onConsoleNotify);
            Debug.println("Subbed to notifications on FFF1.");
        }

        // FFF2 = write
        consoleWriteCharacteristic = service->getCharacteristic(CONSOLE_CHAR_UUID_FFF2);
        if (!consoleWriteCharacteristic || !consoleWriteCharacteristic->canWrite()) {
            Debug.println("FFF2 characteristic not found or not writable.");
            consoleClient->disconnect();
            consoleIsConnected = false;
            return;
        }
    }

    void connectToConsoleViaBLE() {
      Debug.printf("Scanning for LifeSpan Omni Console...\n");

      // Reset flags
      foundConsole = false;
      //foundConsoleAddress = NimBLEAddress(""); // TODO

      NimBLEScan* pBLEScan = NimBLEDevice::getScan();
      pBLEScan->setScanCallbacks(&mScanCallbacks, false);
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
};

// // Initialize the static pointer to null
LifespanOmniConsoleTreadmillDevice* LifespanOmniConsoleTreadmillDevice::globalInstance = nullptr;
