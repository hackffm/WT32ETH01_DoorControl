#pragma once

#include <Arduino.h>

bool protocolInput(uint8_t *data, size_t len, uint8_t *answer, size_t *anslen, int sourceid);
