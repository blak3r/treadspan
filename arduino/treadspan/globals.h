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
extern uint32_t gSteps;
extern float gSpeedInKm; // Represents the current speed of the treadmill as a float.
extern uint16_t gCalories;
extern float gSpeedFloat;
extern uint32_t gDistance;
extern uint32_t gDistanceInMeters;
extern uint16_t gDurationInSecs;

extern bool wasTimeSet;
extern TreadmillSession gCurrentSession;

extern bool gIsTreadmillActive;
extern DebugWrapper Debug;

extern volatile bool gResetRequested; // Created as debug flag to force reset of FTMS treadmill with button press


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
