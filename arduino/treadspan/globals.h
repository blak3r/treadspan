#pragma once

#include <Arduino.h>
#include "DebugWrapper.h"

struct TreadmillSession {
  uint32_t start;
  uint32_t stop;
  uint32_t steps;
};

// ---------------------------------------------------------------------------
// Global Variables
// ---------------------------------------------------------------------------
extern int steps;
extern int calories;
extern bool wasTimeSet;
extern TreadmillSession currentSession;

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
