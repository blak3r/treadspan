#pragma once

#include <Arduino.h>
#include "DebugWrapper.h"

// ---------------------------------------------------------------------------
// Global Variables
// ---------------------------------------------------------------------------
extern int steps;
extern bool isTreadmillActive;
extern DebugWrapper Debug;


// ---------------------------------------------------------------------------
// Global Functions
// ---------------------------------------------------------------------------

/**
 * Called by your treadmill device when a new session starts
 */
void sessionStartedDetected();

/**
 * Called by your treadmill device when a session ends
 */
void sessionEndedDetected();

/**
 * Simple helper to estimate miles-per-hour from the integer “speed” value.
 * In your original code, you used "0.00435 * value - 0.009".
 */
inline float estimate_mph(int value) {
    return (0.00435f * value) - 0.009f;
}