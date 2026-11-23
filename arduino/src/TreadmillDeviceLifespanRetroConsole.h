#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "TreadmillDevice.h"
#include "LifespanSerialParser.h"
#include "globals.h"  // for sessionStartedDetected(), sessionEndedDetected(), etc.

/**
 * Simple helper to estimate miles-per-hour from the integer “speed” value.
 * In your original code, you used "0.00435 * value - 0.009".
 */
inline float estimate_mph(int value) {
    return (0.00435f * value) - 0.009f;
}

/**
 * This class encapsulates the "Retro Console" logic that was previously
 * wrapped in #ifdef RETRO_MODE. 
 */
class TreadmillDeviceLifespanRetroConsole : public TreadmillDevice {
public:
    TreadmillDeviceLifespanRetroConsole() : parser(onSteps, onSpeed, this) {}

    virtual ~TreadmillDeviceLifespanRetroConsole() {}

    // Called once in Arduino setup()
    void setupHandler() override {
        Debug.println("Initializing Retro Console Treadmill...");

        // Initialize your UART1 / UART2
        // In your original code, you had pins defined as:
        //   #define RX1PIN 20
        //   #define TX1PIN 6
        //   #define RX2PIN 23
        //   #define TX2PIN 8
        // Adjust below as needed:
        uart1.begin(4800, SERIAL_8N1, rx1Pin, tx1Pin);
        uart2.begin(4800, SERIAL_8N1, rx2Pin, tx2Pin);

        parser.reset();
    }

    void loopHandler() override {
        // Decode complete requests before replies already buffered by the UARTs.
        // A bounded pending queue keeps their order without allocating Strings.
        while (uart1.available() > 0)
            parser.requestByte(static_cast<uint8_t>(uart1.read()), millis());
        while (uart2.available() > 0)
            parser.responseByte(static_cast<uint8_t>(uart2.read()), millis());
    }

  bool isConnected() {
    return false; // this is used to show a bluetooth icon, so we'll always return false.
  }

  bool isBle() override {
    return false; // this device is not BLE
  }

  String getBleServiceUuid() override {
    return String(); // not applicable for retro console
  }

private:
    static constexpr int rx1Pin = 20;
    static constexpr int tx1Pin = 6;
    static constexpr int rx2Pin = 23;
    static constexpr int tx2Pin = 8;
    HardwareSerial uart1 = HardwareSerial(1);
    HardwareSerial uart2 = HardwareSerial(2);
    treadspan::LifespanSerialParser parser;

    static void onSteps(void*, uint16_t value) { gSteps = value; }
    static void onSpeed(void*, uint16_t value) {
        // Preserve existing session inference from validated target commands.
        // This is not a confirmation of actual belt motion.
        if (value == 50 && gIsTreadmillActive) sessionEndedDetected();
        else if (value > 50 && !gIsTreadmillActive) sessionStartedDetected();
    }
};
