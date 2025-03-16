#pragma once

#include <Arduino.h>
#include "DebugWrapper.h"

// ---------------------------------------------------------------------------
// Global Variables
// ---------------------------------------------------------------------------
extern int steps;
extern int calories;
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
