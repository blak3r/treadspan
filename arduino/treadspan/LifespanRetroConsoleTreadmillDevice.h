#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "TreadmillDevice.h"
#include "globals.h"  // for sessionStartedDetected(), sessionEndedDetected(), etc.


/**
 * This class encapsulates the "Retro Console" logic that was previously
 * wrapped in #ifdef RETRO_MODE. 
 */
class LifespanRetroConsoleTreadmillDevice : public TreadmillDevice {
public:
    LifespanRetroConsoleTreadmillDevice() {
        // Constructor: you could parameterize pins or speeds if needed.
    }

    virtual ~LifespanRetroConsoleTreadmillDevice() {}

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

        // Clear buffers
        uart1Buffer.reserve(64);  // optional
        uart2Buffer.reserve(64);  // optional
        uart1Buffer = "";
        uart2Buffer = "";
        uart1RxCount = 0;
        uart2RxCount = 0;
        lastRequestType = 0;
    }

    // Called repeatedly in Arduino loop()
    void loopHandler() override {
        // This replaces retroModeMainLoopHandler() from your original code.

        // 1) Check if data is available on UART1
        while (uart1.available() > 0) {
            char receivedChar = (char)uart1.read();
            uart1Buf[uart1RxCount % CMD_BUF_SIZE] = receivedChar;
            uart1RxCount++;
            uart1Buffer += String(receivedChar, DEC) + " ";
            // If we see data on UART1, let's see if we also have data from UART2
            // to form a request/response pair. (Or we can wait until we read the entire request.)
        }

        // 2) Check if data is available on UART2
        while (uart2.available() > 0) {
            char receivedChar = (char)uart2.read();
            uart2Buf[uart2RxCount % CMD_BUF_SIZE] = receivedChar;
            uart2RxCount++;
            uart2Buffer += String(receivedChar, DEC) + " ";
        }

        // 3) If we got new data from UART1, process it
        if (uart1RxCount > 0) {
            processRequest();
        }

        // 4) If we got new data from UART2, process it
        if (uart2RxCount > 0) {
            processResponse();
        }
    }

private:
    // ------------------------------- 
    // Internal Constants & Buffers
    // -------------------------------
    static constexpr size_t CMD_BUF_SIZE = 10;

    // If you want to specify the pins used for UART1 / UART2:
    static constexpr int rx1Pin = 20;
    static constexpr int tx1Pin = 6;
    static constexpr int rx2Pin = 23;
    static constexpr int tx2Pin = 8;

    // Distinguish request types from the console
    enum {
        LAST_REQUEST_IS_STEPS = 1,
        LAST_REQUEST_IS_SPEED = 2,
        LAST_REQUEST_IS_DISTANCE = 3,
        LAST_REQUEST_IS_TIME = 4
    };

    // We match your original code:
    static constexpr const char* STEPS_STARTSWITH = "1 3 0 15"; 
    static constexpr const char* SPEED_STARTSWITH = "1 6 0 10";

    // ------------------------------- 
    // Class Member Variables
    // -------------------------------
    HardwareSerial uart1 = HardwareSerial(1); // UART1
    HardwareSerial uart2 = HardwareSerial(2); // UART2

    // Buffers for capturing UART data
    char  uart1Buf[CMD_BUF_SIZE];
    char  uart2Buf[CMD_BUF_SIZE];
    int   uart1RxCount = 0;
    int   uart2RxCount = 0;
    String uart1Buffer;
    String uart2Buffer;

    int   lastRequestType = 0;

private:
    // -------------------------------
    // Methods
    // -------------------------------
    void processRequest() {
        if (VERBOSE_LOGGING) {
            Debug.print("REQ: ");
            Debug.println(uart1Buffer);
        }

        // e.g. check if the command starts with STEPS_STARTSWITH
        if (uart1Buffer.startsWith(STEPS_STARTSWITH)) {
            lastRequestType = LAST_REQUEST_IS_STEPS;
        } else if (uart1Buffer.startsWith(SPEED_STARTSWITH)) {
            // parse speed
            // We'll just parse from the raw buffer directly
            getSpeedFromCommand((uint8_t*)uart1Buf);
        } else {
            lastRequestType = 0;
        }

        // Clear the buffer, reset counters
        uart1Buffer = "";
        uart1RxCount = 0;
    }

    void processResponse() {
        if (VERBOSE_LOGGING) {
            Debug.print("RESP: ");
            Debug.println(uart2Buffer);
        }

        // parse steps if the last request was steps
        if (lastRequestType == LAST_REQUEST_IS_STEPS) {
            // from original code: steps = [3]*256 + [4]
            steps = (uint8_t)uart2Buf[3] * 256 + (uint8_t)uart2Buf[4];
            lastRequestType = 0;
        }

        // Clear the buffer, reset counters
        uart2Buffer = "";
        uart2RxCount = 0;
    }

    /**
     * Called when we see the "SPEED_STARTSWITH" command from UART1.
     * We'll parse the speed from the given buffer. If speedInt>50 => treadmill active,
     * if speedInt==50 => treadmill stops, etc.
     */
    float getSpeedFromCommand(const uint8_t* buf) {
        // In your original code you had:
        //   if(buf[3] == 10) { 
        //       speedInt = buf[4]*256 + buf[5];
        //       if(speedInt == 50) {...} else if (speedInt>50) {...}
        //   }
        // We'll replicate that:
        if (buf[3] == 10) {
            int speedInt = (uint8_t)buf[4] * 256 + (uint8_t)buf[5];
            // In your code, 50 meant "off"
            if (speedInt == 50) {
                // treadmill off
                if (isTreadmillActive) {
                    sessionEndedDetected();
                }
            } else if (speedInt > 50) {
                // treadmill on
                if (!isTreadmillActive) {
                    sessionStartedDetected();
                }
            }
            // If you want to store the float speed somewhere, do so here:
            // float speedFloat = estimate_mph(speedInt);
            // ...
        }
        return -1.0f;
    }
};
