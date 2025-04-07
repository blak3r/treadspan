#include <cstdint>
#pragma once

#include <NimBLEDevice.h>
#include "globals.h"
#include "TreadmillDevice.h"
#include "HasElapsed.h"

// ... existing includes / code ...

class TreadmillDeviceFTMS : public TreadmillDevice {
  public:
    TreadmillDeviceFTMS()
      : mConnectionRetryTimer(5000),
        mSpeedBelowThresholdStart(0),
        mIsConnected(false),
        mFoundTreadmill(false),
        mClient(nullptr),
        mTreadmillDataChar(nullptr),
        mFtmsStatusChar(nullptr),
        mControlPointChar(nullptr)  // << Added
    {
      // empty
    }

    void sendReset() override {
      sendResetCommand();
    }

    virtual ~TreadmillDeviceFTMS() {}

    void setupHandler() override {
      // no special hardware init
    }

    // Called repeatedly from main loop()
    void loopHandler() override {
      if (!mIsConnected) {
        connectionStateMachine();
      } else {
        // The treadmill might never send a "stop" status.
        // We use a fallback detection based on speed < 0.2 mph for 5s:
       // checkSpeedStopTimeout();

        if( gResetRequested ) {
          sendResetCommand();
          gResetRequested = false;
        }

        // Check for delayed reset
        if (mResetPending && (millis() - mResetStartTime >= 5000)) {
          Debug.println("5 seconds elapsed since session end — sending reset command.");
          sendResetCommand();
          mResetPending = false;
        }
      }
    }

    bool isConnected() override { return mIsConnected; }
    bool isBle() override { return true; }
    String getBleServiceUuid() override { return FTMS_SERVICE_UUID; }

  private:
  // -----------------------------------------------------------------------
  // Constants
  // -----------------------------------------------------------------------
  static constexpr const char* FTMS_SERVICE_UUID               = "00001826-0000-1000-8000-00805f9b34fb"; // 0x1826
  static constexpr const char* FTMS_CHARACTERISTIC_TREADMILL   = "00002ACD-0000-1000-8000-00805f9b34fb"; // 0x2ACD (main characteristic gives distance)
  static constexpr const char* FTMS_CHARACTERISTIC_STATUS      = "00002ADA-0000-1000-8000-00805f9b34fb"; // 0x2ADA (says if stopped, started)
  static constexpr const char* FTMS_CHARACTERISTIC_FEATURE     = "00002ACC-0000-1000-8000-00805f9b34fb"; // 0x2ACC (Fitness Machine Feature)
  static constexpr const char* FTMS_CHARACTERISTIC_TM_FEATURE  = "00002ACE-0000-1000-8000-00805f9b34fb"; // 0x2ACE (Treadmill Feature)
  static constexpr const char* FTMS_CHARACTERISTIC_CONTROLPOINT = "00002AD9-0000-1000-8000-00805f9b34fb"; // 0x2AD9 (Control Point)

  static constexpr float STOP_SPEED_THRESHOLD = 0.2f;  // below 0.2 mph => we consider "stopped"
  static constexpr unsigned long STOP_DETECT_TIMEOUT = 5000; // 5 seconds

  // We'll scan for up to 3s
  static const int SCAN_DURATION_MS = 3000;

  private:
  // BLE scanning results
  NimBLEAddress mFtmsAddress;
  bool mFoundTreadmill;

  // BLE client references
  NimBLEClient*               mClient;
  NimBLERemoteCharacteristic* mTreadmillDataChar;
  NimBLERemoteCharacteristic* mFtmsStatusChar;
  NimBLERemoteCharacteristic* mControlPointChar;  // << Added

  // State
  bool mIsConnected;
  HasElapsed mConnectionRetryTimer;

  bool mResetPending = false;
  unsigned long mResetStartTime = 0;

  // Treadmill capabilities flags from the Feature characteristic
  struct FtmsFeatures {
    // First 4 bytes - common features
    bool avgSpeedSupported = false;
    bool cadenceSupported = false;
    bool totalDistanceSupported = false;
    bool inclinationSupported = false;
    bool elevationGainSupported = false;
    bool paceSupported = false;
    bool stepCountSupported = false;
    bool resistanceLevelSupported = false;
    bool strideCountSupported = false;
    bool expendedEnergySupported = false;
    bool heartRateSupported = false;
    bool metabolicEquivalentSupported = false;
    bool elapsedTimeSupported = false;
    bool remainingTimeSupported = false;
    bool powerMeasurementSupported = false;
    bool forceOnBeltSupported = false;
    bool userDataRetentionSupported = false;

    // Treadmill specific features (next 4 bytes)
    bool speedTargetSettingSupported = false;
    bool inclineTargetSettingSupported = false;
    bool resistanceTargetSettingSupported = false;
    bool heartRateTargetSettingSupported = false;
    bool targetedExpendedEnergyConfigSupported = false;
    bool targetedStepNumberConfigSupported = false;
    bool targetedStrideNumberConfigSupported = false;
    bool targetedDistanceConfigSupported = false;
    bool targetedTrainingTimeConfigSupported = false;
    bool targetedTimeInTwoHrZoneConfigSupported = false;
    bool targetedTimeInThreeHrZoneConfigSupported = false;
    bool targetedTimeInFiveHrZoneConfigSupported = false;
    bool indoorBikeSimulationSupported = false;
    bool wheelCircumferenceConfigSupported = false;
    bool spinDownControlSupported = false;
    bool targetedCadenceConfigSupported = false;
  } features;


  // For speed-based session end detection:
  unsigned long mSpeedBelowThresholdStart; // 0 if currently above threshold

  // A global pointer so static callbacks can delegate to 'this'
  static TreadmillDeviceFTMS* sSelf;

  private:
  // -----------------------------------------------------------------------
  // Connection Logic
  // -----------------------------------------------------------------------
  void connectionStateMachine() {
    if (mConnectionRetryTimer.isIntervalUp()) {
      if (!mFoundTreadmill) {
        startScan();
      } else {
        // Attempt to connect if not scanning
        if (!NimBLEDevice::getScan()->isScanning()) {
          connectToFoundTreadmill();
        }
      }
    }
  }

  void startScan() {
    Debug.println("Scanning for FTMS treadmill (Service 0x1826)...");
    mFoundTreadmill = false;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&mScanCallbacks, false /* not using duplicates */);
    scan->setActiveScan(true);
    scan->start(SCAN_DURATION_MS, false, true);
  }

void printCharacteristicAndHandleMap(NimBLEClient* pClient) {
  Serial.println("Discovering services...");
  const std::vector<NimBLERemoteService*>& services = pClient->getServices(true);

  if (services.empty()) {
    Serial.println("No services found.");
    return;
  }

  for (size_t i = 0; i < services.size(); ++i) {
    NimBLERemoteService* service = services[i];
    Serial.print("Service: ");
    Serial.println(service->getUUID().toString().c_str());

    Serial.println("  Discovering characteristics...");
    const std::vector<NimBLERemoteCharacteristic*>& characteristics = service->getCharacteristics(true);

    if (characteristics.empty()) {
      Serial.println("  No characteristics found.");
    }

    for (size_t j = 0; j < characteristics.size(); ++j) {
      NimBLERemoteCharacteristic* characteristic = characteristics[j];
      Serial.print("    Characteristic: ");
      Serial.print(characteristic->getUUID().toString().c_str());
      Serial.printf("  Handle: 0x%04X", characteristic->getHandle());

      if (characteristic->canNotify()) {
        Serial.println("  [NOTIFY]");
      } else {
        Serial.println();
      }
    }
  }

  Serial.println("Done listing notifiable characteristics.");
}




  void connectToFoundTreadmill() {
    mFoundTreadmill = false;
    mIsConnected = false;

    mClient = NimBLEDevice::createClient();
    mClient->setClientCallbacks(&mClientCallbacks);

    Debug.printf("Attempting to connect to FTMS device at %s\n",
                mFtmsAddress.toString().c_str());
    if (!mClient->connect(mFtmsAddress)) {
      Debug.println("Failed to connect to FTMS Treadmill.");
      mClient->disconnect();
      return;
    }

    #if VERBOSE_LOGGING
      printCharacteristicAndHandleMap(mClient);
    #endif

    Debug.println("Connected to FTMS. Discovering service...");

    NimBLERemoteService* uRevoService = mClient->getService("FFF0");
    if( uRevoService ) {
      NimBLERemoteCharacteristic* uRevoChar = uRevoService->getCharacteristic("FFF1");
      if( uRevoChar ) {
        if (!uRevoChar->subscribe(true, onURevoDataNotify)) {
          Debug.println("Subscribe failed.");
          return;
        } else {
          Debug.println("Subbed to UREVO!");
        }
      } else {
        Debug.println("Didn't find FFF1 characteristic (the urevo service");
      }
    } else {
      Debug.println("Didn't find FFFO (the urevo service");
    }



    NimBLERemoteService* service = mClient->getService(FTMS_SERVICE_UUID);
    if (!service) {
      Debug.println("Failed to find FTMS service. Disconnecting...");
      mClient->disconnect();
      return;
    }

    // Print out the treadmill's features if present
    //readAndPrintFeature(service, FTMS_CHARACTERISTIC_FEATURE,    "Fitness Machine Feature");
    readTreadmillFeatures(service, FTMS_CHARACTERISTIC_FEATURE, "Fitness Machine Feature");

    // Get Treadmill Data (0x2ACD)
    mTreadmillDataChar = service->getCharacteristic(FTMS_CHARACTERISTIC_TREADMILL);
    if (mTreadmillDataChar && mTreadmillDataChar->canNotify()) {
      sSelf = this; // so static callback can call into our member
      //mTreadmillDataChar->canIndicate() i think sperax can't do indicate.
      // Fun FAc
      mTreadmillDataChar->subscribe(true, onTreadmillDataNotify, mTreadmillDataChar->canIndicate());
      Debug.printf("Subscribed to Treadmill Data (0x2ACD). Supports Indicate?: %d\n", mTreadmillDataChar->canIndicate());
    } else {
      Debug.println("Treadmill Data (0x2ACD) not found or not notifiable.");
    }





    // Get Fitness Machine Status (0x2ADA)
    mFtmsStatusChar = service->getCharacteristic(FTMS_CHARACTERISTIC_STATUS);
    if (mFtmsStatusChar && mFtmsStatusChar->canNotify()) {
      mFtmsStatusChar->subscribe(true, onFtmsStatusNotify);
      Debug.println("Subscribed to Fitness Machine Status (0x2ADA).");
    }

    // ** Control Point (2AD9) - for sending reset command, etc. **
    mControlPointChar = service->getCharacteristic(FTMS_CHARACTERISTIC_CONTROLPOINT);
    if (mControlPointChar) {
      Debug.println("Found FTMS Control Point (0x2AD9).");
    } else {
      Debug.println("No FTMS Control Point (0x2AD9) found on treadmill.");
    }

    mIsConnected = true;
    mFoundTreadmill = true;
  }

  void readTreadmillFeatures(NimBLERemoteService* service, const char* uuid, const char* label) {
    NimBLERemoteCharacteristic* ch = service->getCharacteristic(uuid);
    if (!ch) {
      // Not present on this device
      Debug.printf("%s (UUID:%s) not found on treadmill.\n", label, uuid);
      return;
    }
    std::string val = ch->readValue();
    if (val.empty()) {
      Debug.printf("%s: readValue() returned empty.\n", label);
      return;
    }

    parseFtmsFeatures((const uint8_t*)val.data(), val.length());
  }

  // -----------------------------------------------------------------------
  // Utility: Read a feature characteristic (like 0x2ACC or 0x2ACE) and print bits
  // -----------------------------------------------------------------------
  void readAndPrintFeature(NimBLERemoteService* service, const char* uuid, const char* label) {
    NimBLERemoteCharacteristic* ch = service->getCharacteristic(uuid);
    if (!ch) {
      // Not present on this device
      Debug.printf("%s (UUID:%s) not found on treadmill.\n", label, uuid);
      return;
    }
    std::string val = ch->readValue();
    if (val.empty()) {
      Debug.printf("%s: readValue() returned empty.\n", label);
      return;
    }

    Debug.printf("=== %s (UUID:%s) ===\n", label, uuid);

    // Typically these are 4 bytes (32 bits) describing what features are supported
    // The official FTMS spec breaks these bits down. We'll just do a hex dump here:
    Debug.printf(" Raw Feature Value (hex): ");
    for (size_t i = 0; i < val.size(); i++) {
      Debug.printf("%02X ", (uint8_t)val[i]);
    }
    Debug.println("");

    if (val.size() >= 4) {
      // If you want to parse bits individually:
      uint32_t rawFeature =
        ((uint8_t)val[0]) |
        (((uint8_t)val[1]) << 8) |
        (((uint8_t)val[2]) << 16) |
        (((uint8_t)val[3]) << 24);

      Debug.printf("  -> As 32-bit mask: 0x%08X\n", rawFeature);
      // Official FTMS spec details which bits correspond to features like:
      //   bit 0: Average Speed Supported
      //   bit 1: Inst. Pace Supported
      //   ...
      //   etc.
    }
    Debug.println("============================\n");
  }

  // -----------------------------------------------------------------------
  // Scan callbacks
  // -----------------------------------------------------------------------
  class InternalScanCallbacks : public NimBLEScanCallbacks {
    public:
      InternalScanCallbacks(TreadmillDeviceFTMS* parent) : mParent(parent) {}
      void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        if (VERBOSE_LOGGING) {
          Debug.printf("Advertised Device: %s\n", advertisedDevice->toString().c_str());
        }

        if (advertisedDevice->isAdvertisingService(NimBLEUUID(uint16_t(0x1826)))) {
          Debug.printf("Found FTMS device: %s\n",
                      advertisedDevice->getAddress().toString().c_str());
          NimBLEDevice::getScan()->stop();
          mParent->mFtmsAddress = advertisedDevice->getAddress();
          mParent->mFoundTreadmill = true;
          mParent->mConnectionRetryTimer.runNextTimeIn(100);
        }
      }
      void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Debug.printf("BLE Scan ended, reason=%d, found %d devices.\n", reason, results.getCount());
      }
    private:
      TreadmillDeviceFTMS* mParent;
  } mScanCallbacks{this};

  // -----------------------------------------------------------------------
  // Client connection callbacks
  // -----------------------------------------------------------------------
  class InternalClientCallbacks : public NimBLEClientCallbacks {
    public:
      InternalClientCallbacks(TreadmillDeviceFTMS* parent) : mParent(parent) {}
      void onConnect(NimBLEClient* pclient) override {
        Debug.println("FTMS treadmill connected (callback).");
        mParent->mIsConnected = true;
      }
      void onDisconnect(NimBLEClient* pclient, int reason) override {
        Debug.println("!!! FTMS treadmill disconnected.");
        mParent->mIsConnected = false;
      }
    private:
      TreadmillDeviceFTMS* mParent;
  } mClientCallbacks{this};

  // -----------------------------------------------------------------------
  // Notification callbacks
  // -----------------------------------------------------------------------
  static void onTreadmillDataNotify(NimBLERemoteCharacteristic* pChar,
                                    uint8_t* data, size_t length, bool isNotify) {
    if (sSelf) {
      sSelf->handleTreadmillData(data, length);
    }
  }

  static void onURevoDataNotify(NimBLERemoteCharacteristic* pChar,
                                    uint8_t* data, size_t length, bool isNotify) {
    Debug.printArray(data, length, "UREVO Proprietary Data");            
  }

  static void onFtmsStatusNotify(NimBLERemoteCharacteristic* pChar,
                                uint8_t* data, size_t length, bool isNotify) {
    if (sSelf) {
      sSelf->handleFtmsStatus(data, length);
    }
  }

  // -----------------------------------------------------------------------
  // Treadmill Data (0x2ACD) parser
  //
  // Sperax returns for flags: 0x0484 (binary 0000 0100 1000 0100)
  //                                                          ^-- total distance present
  //                                                    ^-- Expended Energy
  //                                                ^-- Metabolic Equivalent (MET) Present
  // FTMS Data (len=14): 84 04 1E 00 1E 00 00 02 00 FF FF FF A2 00
  //                           <-dis--> <-energy-----> <el-> <extra>
  //
  // UREVO E1L
  // FLAGS 0x2584 (binary 0010 0101 1000 0100 )
  //                                      ^- Total Distance 3
  //                                ^--- Expended Energy 5
  //                              ^--- Heart Rate 1
  //                            ^--- Duration 2
  //                        ^--- Power output (w) 2
  //
  // FTMS Data (len=18): 84 25 01 01 5E 01 00 14 00 00 00 00 00 8B 01 FA 00 00
  //                           <dist--> <-energy-----> HR <dur> <powe>
  //
  // Flags:       0x0E0A (binary 0000 1110 0000 1010)
  // Fields:      Speed, Total Distance, Incline, Heart Rate, Elapsed Time
  // -----------------------------------------------------------------------
  void handleTreadmillData(uint8_t* data, size_t length) {
    static uint32_t lastDistance = 0;
    uint32_t distance = 0;

    if (length < 2) return;

    if (VERBOSE_LOGGING) {
        Debug.printf("FTMS Data (len=%d): ", length);
        for (size_t i = 0; i < length; i++) {
            Debug.printf_noTs("%02X ", data[i]);
        }
        Debug.printf_noTs("\n");
    }

    // The first two bytes are flags indicating which data fields are present
    uint16_t flags = data[0] | (data[1] << 8);
    int offset = 2; // Start parsing after the flags

    // Store previous speed to detect changes
    float oldSpeed = gSpeedFloat;

    // Parse data based on which flags are set

    // Instantaneous Speed (bit 0)
    // This bit is the opposite of others, it's assumed to always be provided, when it's "1" it indicates more data.
    if (!(flags & 0x0001)) {
        uint16_t speedInMetersPerSecond = data[offset] | (data[offset + 1] << 8);
        uint16_t speedInKmPerHour = speedInMetersPerSecond * 3.6f;

  //           // Speed in FTMS is in units of 0.001 m/s
  //           float speedInMph = ftmsSpeedToMPH(speedRaw);
        Debug.printf("Speed: 0x%04X %.2f kph \n", speedInMetersPerSecond, speedInKmPerHour);
        offset += 2;
    }

    // Average Speed (bit 1)
    if (flags & 0x0002) {
        uint16_t avgSpeedRaw = data[offset] | (data[offset + 1] << 8);
        // Speed is in units of 0.001 m/s
        //float avgSpeedFloat = ftmsSpeedToMPH(avgSpeedRaw);
        //Debug.printf("Avg Speed: %.2f mph\n", avgSpeedFloat);
        offset += 2;
    }

    const float averageStepLengthMeters = 0.3;
    // Total Distance (bit 2)
    if (flags & 0x0004) {

        uint32_t distanceRaw = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16);
        // Distance is in units of meters
        distance = distanceRaw;
        Debug.printf("Distance: %d meters, %.2f km\n", distanceRaw, (distanceRaw*.001f));
        offset += 3;

        // I did 211 Steps in 0.08 miles = 128.74meters 
        // Meters * steps/meter
        // 211 steps/128.74 = 1.6389622495


        if( distanceRaw > 0 ) {
          gSteps = distanceRaw * 1.7233f;
        }
        gDistanceInMeters = distanceRaw;

        Debug.printf("Distance: %d meters, %.2f km, %lu Steps\n", distanceRaw, (distanceRaw*.001f), gSteps);

        // // FTMS doesn't provide steps directly, so we'll estimate based on distance
        // // (We'll also check for proprietary step data at the end)
        // if (distance > lastDistance) {
        //     int distanceChange = distance - lastDistance;
        //     int estimatedSteps = static_cast<int>(distanceChange / averageStepLengthMeters);
        //     gSteps += estimatedSteps;
        //     Debug.printf("Estimated steps from distance: +%d (total: %d)\n", estimatedSteps, gSteps);
        // }
        // lastDistance = distance;
    }

    // Inclination (bit 3)
    if (flags & 0x0008) {
        int16_t inclineRaw = data[offset] | (data[offset + 1] << 8);
        // Incline is in units of 0.1%
        float inclinePct = inclineRaw * 0.1f;
        Debug.printf("Incline: %.1f%%\n", inclinePct);
        offset += 2;
    }

    // Elevation Gain (bit 4)
    if (flags & 0x0010) {
        uint16_t elevationRaw = data[offset] | (data[offset + 1] << 8);
        // Elevation is in meters
        Debug.printf("Elevation Gain: %d meters\n", elevationRaw);
        offset += 2;
    }

    // Instantaneous Pace (bit 5) - in seconds per kilometer
    if (flags & 0x0020) {
        uint16_t paceRaw = data[offset] | (data[offset + 1] << 8);
        float paceSecPerKm = static_cast<float>(paceRaw) / 10.0f; // Units: 0.1 s/km
        Debug.printf("Pace: %.1f sec/km\n", paceSecPerKm);
        offset += 2;
    }

    // Average Pace (bit 6) - in seconds per kilometer
    if (flags & 0x0040) {
        uint16_t avgPaceRaw = data[offset] | (data[offset + 1] << 8);
        float avgPaceSecPerKm = static_cast<float>(avgPaceRaw) / 10.0f; // Units: 0.1 s/km
        Debug.printf("Avg Pace: %.1f sec/km\n", avgPaceSecPerKm);
        offset += 2;
    }

    // Expended Energy (bit 7)
    if (flags & 0x0080) {
        // Total Energy (2 bytes), Energy Per Hour (2 bytes), Energy Per Minute (1 byte)
        uint16_t totalCaloriesRaw = data[offset] | (data[offset + 1] << 8);
        gCalories = totalCaloriesRaw; // Update global calories variable
        // uint16_t caloriesPerHour = data[offset + 2] | (data[offset + 3] << 8);
        // uint8_t caloriesPerMinute = data[offset + 4];
        // 255 --> not supported for perMin/perHour according to spec... I don't need it so i removed.
        Debug.printf("Energy: %d kcal total\n", totalCaloriesRaw);
        offset += 5;
    }

    // Heart Rate (bit 8) - in BPM
    if (flags & 0x0100) {
        uint8_t heartRate = data[offset];
        Debug.printf("Heart Rate: %d BPM\n", heartRate);
        offset += 1;
    }

    // Metabolic Equivalent (bit 9) - in 0.1 METs
    if (flags & 0x0200) {
        uint8_t metsRaw = data[offset];
        float mets = static_cast<float>(metsRaw) / 10.0f;
        Debug.printf("METs: %.1f\n", mets);
        offset += 1;
    }

    // Elapsed Time (bit 10) - in seconds
    if (flags & 0x0400) {
        uint16_t elapsedTime = data[offset] | (data[offset + 1] << 8);
        if( elapsedTime == UINT16_MAX || elapsedTime == 0 ) {
          // NOT VALID.
        }
        else {
          Debug.printf("Elapsed Time: %d seconds\n", elapsedTime);
        }
        offset += 2;
    }

    // Remaining Time (bit 11) - in seconds
    if (flags & 0x0800) {
        uint16_t remainingTime = data[offset] | (data[offset + 1] << 8);
        Debug.printf("Remaining Time: %d seconds\n", remainingTime);
        offset += 2;
    }

    // Force on Belt (bit 12) - in Newtons
    if (flags & 0x1000) {
        int16_t force = data[offset] | (data[offset + 1] << 8);
        Debug.printf("Force on Belt: %d N\n", force);
        offset += 2;
    }

    // Power Output (bit 13) - in watts
    if (flags & 0x2000) {
        int16_t power = data[offset] | (data[offset + 1] << 8);
        Debug.printf("Power Output: %d W\n", power);
        offset += 2;
    }

    // Check for proprietary steps data at the end
    // Many treadmills add steps as the last two bytes, even though it's not in the FTMS spec
    // Log the remaining bytes for debugging
    if (VERBOSE_LOGGING && offset < (int)length) {
        Debug.printf("Extra data after standard fields: ");
        for (int i = offset; i < (int)length; i++) {
            Debug.printf_noTs("%02X ", data[i]);
        }
        Debug.println();
    } 
  }

  void sendResetCommand() {
    Debug.println("\n----------------------\n=-=-=-=-\nEntered. RESET wrapper\n=-=-=-=\n---------------------------------");
    // now attempt to reset treadmill
    if (mControlPointChar && mIsConnected) {
      // Typically you do "Request Control (0x00)" then "Reset (0x01)"
      std::vector<uint8_t> cmd;

      uint16_t handle = mControlPointChar->getHandle();
      Debug.printf("Control Point characteristic handle: 0x%04X\n", handle);

      // 0x01 = Reset
      cmd = { 0x08, 0x01 };
      mControlPointChar->writeValue(cmd);

      Debug.printf("Sent FTMS Request Control + Reset opcodes to treadmill via Handle (0x%04X)\n", handle);
    } else {
      Debug.println("Cannot reset treadmill - control point not available or not connected.");
    }
  }

  void sessionEndedDetectedWrapper() {
    Debug.printf("Entered. wrapper\n");
    sessionEndedDetected();
    mResetPending = true;
    mResetStartTime = millis();  // start countdown
   //sendResetCommand();
  }

  // -----------------------------------------------------------------------
  // Fitness Machine Status (0x2ADA)
  // -----------------------------------------------------------------------
  void handleFtmsStatus(uint8_t* data, size_t length) {
    if (length < 1) return;
    uint8_t opcode = data[0];

    if (VERBOSE_LOGGING) {
      Debug.printArray(data, length, "[2ADA] Treadmill Status Change: ");
    }

    // Some typical FTMS opcodes for treadmill start/stop:
    // 0x03 = STOP or PAUSED by safety key
    // 0x04 = START or RESUME
    switch (opcode) {
      case 0x02:  // RESET - seems to be what Sperax is using...
      case 0x03:  // STOPPED/PAUSEA
        Debug.printf("Treadmill: STOPPED (FTMS status 0x%02X).\n", opcode);
        if (gIsTreadmillActive) {
          sessionEndedDetectedWrapper();
        }
        break;
      case 0x04: // STARTED/RESUMED
        Debug.println("Treadmill: STARTED/RESUMED (FTMS status 0x04).");
        if (!gIsTreadmillActive) {
          sessionStartedDetected();
        }
        break;
      default:
        Debug.printf("Treadmill FTMS Status Change (ignored): 0x%02X.\n", opcode);
        // The rest are events like speed changed, target changed, etc.
        break;
    }
  }

  // -----------------------------------------------------------------------
  // If speed remains below threshold for STOP_DETECT_TIMEOUT, end session
  // -----------------------------------------------------------------------
  void checkSpeedStopTimeout() {
    if (gIsTreadmillActive && mSpeedBelowThresholdStart > 0) {
      unsigned long elapsed = millis() - mSpeedBelowThresholdStart;
      if (elapsed >= STOP_DETECT_TIMEOUT) {
        Debug.println("Treadmill: detected STOP due to low speed threshold timeout.");
        sessionEndedDetectedWrapper();
        mSpeedBelowThresholdStart = 0;
      }
    }
  }

  /**
  * Parse the Fitness Machine Feature characteristic data
  * This is defined in FTMS spec section 4.3.1.1
  */
  void parseFtmsFeatures(const uint8_t* data, size_t length) {
    if (length < 4) {
      Debug.println("Error: FTMS Feature data too short!");
      return;
    }

    // Common feature flags (first 4 bytes)
    uint32_t commonFeatures = data[0] |
                            (data[1] << 8) |
                            (data[2] << 16) |
                            (data[3] << 24);

    // Parse common features
    features.avgSpeedSupported = commonFeatures & (1 << 0);
    features.cadenceSupported = commonFeatures & (1 << 1);
    features.totalDistanceSupported = commonFeatures & (1 << 2);
    features.inclinationSupported = commonFeatures & (1 << 3);
    features.elevationGainSupported = commonFeatures & (1 << 4);
    features.paceSupported = commonFeatures & (1 << 5);
    features.stepCountSupported = commonFeatures & (1 << 6);
    features.resistanceLevelSupported = commonFeatures & (1 << 7);
    features.strideCountSupported = commonFeatures & (1 << 8);
    features.expendedEnergySupported = commonFeatures & (1 << 9);
    features.heartRateSupported = commonFeatures & (1 << 10);
    features.metabolicEquivalentSupported = commonFeatures & (1 << 11);
    features.elapsedTimeSupported = commonFeatures & (1 << 12);
    features.remainingTimeSupported = commonFeatures & (1 << 13);
    features.powerMeasurementSupported = commonFeatures & (1 << 14);
    features.forceOnBeltSupported = commonFeatures & (1 << 15);
    features.userDataRetentionSupported = commonFeatures & (1 << 16);

    Debug.println("FTMS Common Features:");
    Debug.printf("  Average Speed: %s\n", features.avgSpeedSupported ? "Yes" : "No");
    Debug.printf("  Cadence: %s\n", features.cadenceSupported ? "Yes" : "No");
    Debug.printf("  Total Distance: %s\n", features.totalDistanceSupported ? "Yes" : "No");
    Debug.printf("  Inclination: %s\n", features.inclinationSupported ? "Yes" : "No");
    Debug.printf("  Elevation Gain: %s\n", features.elevationGainSupported ? "Yes" : "No");
    Debug.printf("  Pace: %s\n", features.paceSupported ? "Yes" : "No");
    Debug.printf("  Step Count: %s\n", features.stepCountSupported ? "Yes" : "No");
    Debug.printf("  Resistance Level: %s\n", features.resistanceLevelSupported ? "Yes" : "No");
    Debug.printf("  Stride Count: %s\n", features.strideCountSupported ? "Yes" : "No");
    Debug.printf("  Expended Energy: %s\n", features.expendedEnergySupported ? "Yes" : "No");
    Debug.printf("  Heart Rate: %s\n", features.heartRateSupported ? "Yes" : "No");
    Debug.printf("  Metabolic Equivalent: %s\n", features.metabolicEquivalentSupported ? "Yes" : "No");
    Debug.printf("  Elapsed Time: %s\n", features.elapsedTimeSupported ? "Yes" : "No");
    Debug.printf("  Remaining Time: %s\n", features.remainingTimeSupported ? "Yes" : "No");
    Debug.printf("  Power Measurement: %s\n", features.powerMeasurementSupported ? "Yes" : "No");
    Debug.printf("  Force on Belt: %s\n", features.forceOnBeltSupported ? "Yes" : "No");
    Debug.printf("  User Data Retention: %s\n", features.userDataRetentionSupported ? "Yes" : "No");

    // Check if we have treadmill target setting features
    if (length >= 8) {
      uint32_t targetFeatures = data[4] |
                              (data[5] << 8) |
                              (data[6] << 16) |
                              (data[7] << 24);

      // Parse target setting features
      features.speedTargetSettingSupported = targetFeatures & (1 << 0);
      features.inclineTargetSettingSupported = targetFeatures & (1 << 1);
      features.resistanceTargetSettingSupported = targetFeatures & (1 << 2);
      features.heartRateTargetSettingSupported = targetFeatures & (1 << 3);
      features.targetedExpendedEnergyConfigSupported = targetFeatures & (1 << 4);
      features.targetedStepNumberConfigSupported = targetFeatures & (1 << 5);
      features.targetedStrideNumberConfigSupported = targetFeatures & (1 << 6);
      features.targetedDistanceConfigSupported = targetFeatures & (1 << 7);
      features.targetedTrainingTimeConfigSupported = targetFeatures & (1 << 8);
      features.targetedTimeInTwoHrZoneConfigSupported = targetFeatures & (1 << 9);
      features.targetedTimeInThreeHrZoneConfigSupported = targetFeatures & (1 << 10);
      features.targetedTimeInFiveHrZoneConfigSupported = targetFeatures & (1 << 11);
      features.indoorBikeSimulationSupported = targetFeatures & (1 << 12);
      features.wheelCircumferenceConfigSupported = targetFeatures & (1 << 13);
      features.spinDownControlSupported = targetFeatures & (1 << 14);
      features.targetedCadenceConfigSupported = targetFeatures & (1 << 15);

      Debug.println("FTMS Target Setting Features:");
      Debug.printf("  Speed Target Setting: %s\n", features.speedTargetSettingSupported ? "Yes" : "No");
      Debug.printf("  Incline Target Setting: %s\n", features.inclineTargetSettingSupported ? "Yes" : "No");
      Debug.printf("  Resistance Target Setting: %s\n", features.resistanceTargetSettingSupported ? "Yes" : "No");
      Debug.printf("  Heart Rate Target Setting: %s\n", features.heartRateTargetSettingSupported ? "Yes" : "No");
      Debug.printf("  Targeted Expended Energy Config: %s\n", features.targetedExpendedEnergyConfigSupported ? "Yes" : "No");
      Debug.printf("  Targeted Step Number Config: %s\n", features.targetedStepNumberConfigSupported ? "Yes" : "No");
      Debug.printf("  Targeted Distance Config: %s\n", features.targetedDistanceConfigSupported ? "Yes" : "No");
      Debug.printf("  Targeted Training Time Config: %s\n", features.targetedTrainingTimeConfigSupported ? "Yes" : "No");
    }
  }
};

// Initialize static pointer
TreadmillDeviceFTMS* TreadmillDeviceFTMS::sSelf = nullptr;
