#pragma once

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

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