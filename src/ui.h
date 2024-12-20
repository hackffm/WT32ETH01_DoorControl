#pragma once
#include <Arduino.h>

void uiInit();
void uiHandle();
void uiSetLED(uint32_t rgb);

// Steps 0 = no stop, Steps 1 = stop with rgbA
void uiBlinkLED(uint32_t rgbA, uint16_t msA, uint32_t rgbB, uint16_t msB, uint16_t steps);
// Get current pressed button and how long it is pressed
int uiGetPressedButton(uint16_t *sinceMs = NULL);

// Get (and clears) last released button and how long it was pressed
int uiGetReleasedButton(uint16_t *durationMs = NULL);
 