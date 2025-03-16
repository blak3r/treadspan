#pragma once

#include <Arduino.h>

class HasElapsed {
  public:
    HasElapsed(unsigned long defaultInterval) : defaultInterval(defaultInterval), interval(defaultInterval), lastCheck(0) {}

    /**
     * Checks if the given interval has elapsed.
     * If true, it resets the timer automatically.
     */
    bool isIntervalUp() {
      unsigned long now = millis();
      if (now - lastCheck >= interval) {
        interval = defaultInterval;
        lastCheck = now;
        return true;
      }
      return false;
    }

    /**
     * Sometimes you want to do a different interval amount, this lets you override.
     */ 
    void runNextTimeIn( unsigned long nextInterval ) {
      unsigned long now = millis();
      lastCheck = now;
      interval = nextInterval;
    }

    /**
     * Manually reset the timer.
     */
    void reset() {
      lastCheck = millis();
    }

    /**
     * Get the time since the last reset.
     */
    unsigned long timeSinceLast() const {
      return millis() - lastCheck;
    }

  private:
    unsigned long interval;
    unsigned long defaultInterval;
    unsigned long lastCheck;
};